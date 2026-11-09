#include "Shell.hpp"
#include "core/Recents.hpp"
#include "Log.hpp"

#include <gtkmm/label.h>
#include <gtkmm/alertdialog.h>
#include <gtkmm/editable.h>
#include <gtkmm/textview.h>
#include <glibmm/main.h>
#include <cmath>
#include <giomm/menuitem.h>
#include <glibmm/miscutils.h>
#include <glibmm/variant.h>

#include <filesystem>
#include <gtkmm/image.h>
#include <gtkmm/listboxrow.h>
#include "core/Tabs.hpp"
#include "Covers.hpp"
#include <gtkmm/drawingarea.h>
#include <cairomm/context.h>
#include <cairomm/surface.h>
#include <cmath>
#include <gtkmm/flowboxchild.h>
#include <gtkmm/gestureclick.h>

// Shell_helpers.cpp -- HELPERS. Reusable orchestration called from zones,
// bindings, and handlers: the recents seam (path resolution, persist, and the
// two-surface repaint).

namespace kaflip {

std::string Shell::recents_file() const {  // helper: XDG path (UI-side resolution)
    // The UI resolves the XDG dir (Glib here) and hands the core a plain path --
    // the core stays GTK-free (docs/two-layer-seam.md).
    return (std::filesystem::path(Glib::get_user_data_dir()) / "kaflip" / "recent.json")
        .string();
}

void Shell::note_recent(const std::string& path) {  // helper: push + persist + repaint
    if (path.empty()) return;
    core::recents_add(m_recents, path);
    core::save_recents(m_recents_file, m_recents);
    rebuild_recents_menu();
    rebuild_recents_page();
}

void Shell::rebuild_recents_menu() {  // helper: repaint the submenu
    if (!m_recents_menu) return;
    m_recents_menu->remove_all();

    // Display name = folder basename; identical basenames get their parent dir
    // appended so they're distinguishable (Curvz's disambiguation).
    std::vector<std::string> names(m_recents.size());
    for (std::size_t i = 0; i < m_recents.size(); ++i)
        names[i] = std::filesystem::path(m_recents[i]).filename().string();
    for (std::size_t i = 0; i < names.size(); ++i)
        for (std::size_t j = i + 1; j < names.size(); ++j)
            if (names[i] == names[j]) {
                const std::string pi =
                    std::filesystem::path(m_recents[i]).parent_path().filename().string();
                const std::string pj =
                    std::filesystem::path(m_recents[j]).parent_path().filename().string();
                if (!pi.empty()) names[i] += "  (" + pi + ")";
                if (!pj.empty()) names[j] += "  (" + pj + ")";
            }

    for (std::size_t i = 0; i < m_recents.size(); ++i) {
        auto item = Gio::MenuItem::create(names[i], "");
        item->set_action_and_target(
            "recents.open", Glib::Variant<Glib::ustring>::create(m_recents[i]));
        m_recents_menu->append_item(item);
    }

    auto clear_section = Gio::Menu::create();
    clear_section->append("Clear recent", "recents.clear");
    m_recents_menu->append_section("", clear_section);
    if (m_recents_clear) m_recents_clear->set_enabled(!m_recents.empty());
}

void Shell::rebuild_recents_page() {  // helper: repaint the live list page
    // Clear existing rows.
    while (auto* row = m_recents_list.get_row_at_index(0))
        m_recents_list.remove(*row);

    if (m_recents.empty()) {
        auto* empty = Gtk::make_managed<Gtk::Label>("No books yet -- open one and it appears here.");
        empty->set_xalign(0.0f);
        empty->set_margin(16);
        empty->add_css_class("dim-label");
        m_recents_list.append(*empty);
        return;
    }
    // s004: a row per book -- glyph, title (the file's stem), where it lives.
    // The row's index is its place in m_recents (row_activated reads it).
    for (const auto& p : m_recents) {
        const std::filesystem::path fp(p);
        std::error_code ec;
        const bool is_book = std::filesystem::is_regular_file(fp, ec);
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
        row->add_css_class("recent-row");
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(is_book ? "kaflip-tab-reader-symbolic" : "folder-symbolic");
        icon->set_pixel_size(32);
        auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        auto* title = Gtk::make_managed<Gtk::Label>(is_book ? fp.stem().string() : fp.filename().string());
        title->set_xalign(0.0f);
        title->add_css_class("recent-title");
        title->set_ellipsize(Pango::EllipsizeMode::END);
        auto* where = Gtk::make_managed<Gtk::Label>(fp.parent_path().string());
        where->set_xalign(0.0f);
        where->add_css_class("recent-where");
        where->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        text->append(*title);
        text->append(*where);
        row->append(*icon);
        row->append(*text);
        m_recents_list.append(*row);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// s046 -- the undo stone's helpers.
// ─────────────────────────────────────────────────────────────────────────────

// One repaint per idle, AFTER the writes that caused it. A verb can write
// several records (a move writes two); repainting per write would rebuild the
// list mid-verb, and a row rebuilt under a live click or a focused row is torn
// down (jot s045: "open UI after the rebuild you caused").
void Shell::queue_stones_repaint() {  // helper
    if (m_repaint_queued) return;
    m_repaint_queued = true;
    Glib::signal_idle().connect_once([this]() {
        m_repaint_queued = false;
        repaint_stones();
    });
}

// The list and the detail fields, from the model. Setting a field here is not a
// user choice: m_painting keeps the field's own signal from writing it back.
void Shell::repaint_stones() {  // helper
    m_painting = true;
    while (auto* c = m_stone_list.get_first_child()) m_stone_list.remove(*c);
    Gtk::ListBoxRow* chosen = nullptr;
    for (const auto& id : m_stones.ids()) {
        const auto v = m_stones.get(id);
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name(id);   // the row carries its stone's id
        auto label = Gtk::make_managed<Gtk::Label>(
            v->name + "   ·   size " + std::to_string(v->size));
        label->set_xalign(0.0f);
        label->set_margin(6);
        row->set_child(*label);
        m_stone_list.append(*row);
        if (id == m_sel) chosen = row;
    }
    if (chosen) m_stone_list.select_row(*chosen);
    else        m_stone_list.unselect_all();

    const auto v = m_sel.empty() ? std::nullopt : m_stones.get(m_sel);
    m_stone_name.set_sensitive(v.has_value());
    m_stone_size.set_sensitive(v.has_value());
    // Never overwrite a field the user is typing in.
    if (!m_stone_name.has_focus() || !v) m_stone_name.set_text(v ? v->name : std::string{});
    if (!m_journal.in_gesture()) m_stone_size.set_value(v ? v->size : 1);
    m_painting = false;

    if (m_keys_to_list) {
        m_keys_to_list = false;
        if (chosen) chosen->grab_focus();
        else        m_stone_list.grab_focus();
    }
}

// Select a stone (and, with `keys`, give its row the keyboard) once the
// repaint the last write queued has run.
void Shell::select_stone(const std::string& id, bool keys) {  // helper
    m_sel = id;
    m_keys_to_list = m_keys_to_list || keys;
    queue_stones_repaint();
}

// Undo / Redo say what they would do, and grey when there is nothing: the
// actions (so buttons and menu items follow), the button tooltips, and the
// menu's labels -- "Undo Delete “Jade”" names the step before you take it.
void Shell::update_undo_ui() {  // helper
    // s006: two journals, one Ctrl+Z -- the Undo page's stones there, the
    // library everywhere else. The menu and the buttons say which.
    const bool stones = stones_active();
    const bool cu = stones ? m_journal.can_undo() : m_lib_journal.can_undo();
    const bool cr = stones ? m_journal.can_redo() : m_lib_journal.can_redo();
    if (m_act_undo) m_act_undo->set_enabled(cu);
    if (m_act_redo) m_act_redo->set_enabled(cr);
    const std::string u = stones ? m_journal.undo_label() : m_lib_journal.undo_label();
    const std::string r = stones ? m_journal.redo_label() : m_lib_journal.redo_label();
    m_undo_button.set_tooltip_text(u.empty() ? "Nothing to undo" : "Undo " + u + "  (Ctrl+Z)");
    m_redo_button.set_tooltip_text(r.empty() ? "Nothing to redo" : "Redo " + r + "  (Ctrl+Shift+Z)");
    if (m_edit_menu) {
        m_edit_menu->remove_all();
        m_edit_menu->append(u.empty() ? "Undo" : "Undo " + u, "win.undo");
        m_edit_menu->append(r.empty() ? "Redo" : "Redo " + r, "win.redo");
    }
}

// Is the keyboard in something that types? Then Ctrl+Z is that box's own,
// whether or not it had anything to undo.
bool Shell::focus_is_text() const {  // helper
    for (const Gtk::Widget* w = get_focus(); w; w = w->get_parent())
        if (dynamic_cast<const Gtk::TextView*>(w) || dynamic_cast<const Gtk::Editable*>(w)) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// s002 -- the reader
// ─────────────────────────────────────────────────────────────────────────────
bool Shell::open_book(const std::string& path) {  // helper: open + remember + repaint
    std::string error;
    if (!m_reader.open(path, error)) {
        auto alert = Gtk::AlertDialog::create("Could not open this book");
        alert->set_detail(std::filesystem::path(path).filename().string() + "\n\n" + error);
        alert->show(*this);
        return false;
    }
    note_recent(path);
    // s006: an opened book joins the library (a side effect, like recents --
    // not an undo step), with its cover thumbnail made now if it is new.
    if (const core::Book* b = m_reader.book())
        core::library::note_opened(m_library, covers::describe(*b, path));
    m_hero.set_visible(false);         // s004: the card replaces the hero
    m_reader.set_visible(true);
    m_reader_nav.set_visible(true);
    select_tab("reader");
    return true;
}

void Shell::update_reader_ui() {  // helper: title, "3 of 12", grey prev/next
    if (m_act_next) m_act_next->set_enabled(m_reader.can_next());
    if (m_act_prev) m_act_prev->set_enabled(m_reader.can_prev());
    if (m_act_page_next) m_act_page_next->set_enabled(m_reader.can_turn_forward());   // s003
    if (m_act_page_prev) m_act_page_prev->set_enabled(m_reader.can_turn_back());
    if (m_act_focus) m_act_focus->set_enabled(m_reader.has_book());   // s005
    const core::Book* b = m_reader.book();
    if (!b) {
        set_title("Ka-flip");
        m_chapter_label.set_text("");
        return;
    }
    const std::string title = b->meta().title.empty()
        ? std::filesystem::path(b->file()).stem().string()
        : b->meta().title;
    set_title(title + " \u2014 Ka-flip");
    // s003: "Chapter 3 of 12  ·  page 4 of 11" (the page part once it reports).
    std::string where = "Chapter " + std::to_string(m_reader.chapter() + 1) + " of " +
                        std::to_string(m_reader.chapter_count());
    if (m_reader.page_count() > 0)
        where += "  \u00b7  page " + std::to_string(m_reader.page() + 1) + " of " +
                 std::to_string(m_reader.page_count());
    if (!b->meta().creators.empty()) where = b->meta().creators.front() + "  \u00b7  " + where;
    m_chapter_label.set_text(where);
    m_hud_label.set_text(where);   // s005: the focus HUD says the same
}

// s004: one place moves between pages -- the stack, the rail's selection and
// the window's tint class (.page-<id>) always agree.
void Shell::select_tab(const std::string& id) {  // helper
    if (!core::find_tab(id)) return;
    m_stack.set_visible_child(id);
    for (const auto& t : core::tab_registry()) remove_css_class("page-" + std::string(t.id));
    add_css_class("page-" + id);
    m_syncing = true;
    for (auto* w = m_rail.get_first_child(); w; w = w->get_next_sibling())
        if (auto* row = dynamic_cast<Gtk::ListBoxRow*>(w); row && std::string(row->get_name()) == id)
            m_rail.select_row(*row);
    m_syncing = false;
    update_undo_ui();   // s006: the page decides which journal Ctrl+Z means
}

// ─────────────────────────────────────────────────────────────────────────────
// s006 -- the library
// ─────────────────────────────────────────────────────────────────────────────
std::string Shell::library_file() const {  // helper: the XDG path (UI-side resolution)
    return (std::filesystem::path(Glib::get_user_data_dir()) / "kaflip" / "library.json").string();
}

bool Shell::stones_active() const {  // helper
    return m_stack.get_visible_child_name() == "undo";
}

// One save + one repaint per idle, AFTER the writes (a folder is many puts).
void Shell::queue_library_repaint() {  // helper
    if (m_lib_repaint_queued) return;
    m_lib_repaint_queued = true;
    Glib::signal_idle().connect_once([this] {
        m_lib_repaint_queued = false;
        if (!m_library.save(m_library_file))
            if (auto lg = log::get(log::Area::Library)) lg->warn("could not save {}", m_library_file);
        rebuild_library();
    });
}

namespace {
// A cover is a fixed 150 x 225 DrawingArea that paints the cached PNG with
// Cairo (cover-fit, rounded corners). Not a Gtk::Picture: a Picture asks for
// its image's natural size and every tile filled its grid column, and a
// measure() override stopped it drawing at all (two failures, s006 -- so the
// route changed rather than a third variation). Thumbnails are 300 x 450, so
// at 2x the cover is drawn pixel for pixel.
Gtk::Widget* make_cover(const std::string& png) {
    auto area = Gtk::make_managed<Gtk::DrawingArea>();
    area->set_content_width(150);
    area->set_content_height(225);
    area->add_css_class("cover");
    Cairo::RefPtr<Cairo::ImageSurface> img;
    try { img = Cairo::ImageSurface::create_from_png(png); } catch (...) {}
    area->set_draw_func([img](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        const double r = 8.0;
        cr->begin_new_sub_path();
        cr->arc(w - r, r, r, -M_PI / 2, 0);
        cr->arc(w - r, h - r, r, 0, M_PI / 2);
        cr->arc(r, h - r, r, M_PI / 2, M_PI);
        cr->arc(r, r, r, M_PI, 3 * M_PI / 2);
        cr->close_path();
        cr->clip();
        if (!img || img->get_width() <= 0 || img->get_height() <= 0) {
            cr->set_source_rgba(1, 1, 1, 0.08);
            cr->paint();
            return;
        }
        const double k = std::max(double(w) / img->get_width(), double(h) / img->get_height());
        cr->translate((w - img->get_width() * k) / 2, (h - img->get_height() * k) / 2);
        cr->scale(k, k);
        cr->set_source(img, 0, 0);
        cr->paint();
    });
    return area;
}

// A cover tile: the thumbnail, or a tinted card with the title on it.
Gtk::Widget* make_tile(const core::LibItem& b) {
    auto tile = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    tile->add_css_class("book-tile");
    tile->set_halign(Gtk::Align::CENTER);
    tile->set_valign(Gtk::Align::START);
    std::error_code ec;
    if (!b.cover.empty() && std::filesystem::exists(b.cover, ec)) {
        tile->append(*make_cover(b.cover));
    } else {
        auto blank = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        blank->set_size_request(150, 225);
        blank->add_css_class("cover");
        blank->add_css_class("cover-blank");
        std::size_t h = std::hash<std::string>{}(b.title);
        blank->add_css_class("tint-" + std::to_string(h % 5));
        auto t = Gtk::make_managed<Gtk::Label>(b.title);
        t->set_wrap(true);
        t->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
        t->set_justify(Gtk::Justification::CENTER);
        t->set_max_width_chars(12);
        t->set_lines(5);
        t->set_ellipsize(Pango::EllipsizeMode::END);
        t->set_vexpand(true);
        t->set_valign(Gtk::Align::CENTER);
        t->add_css_class("cover-blank-title");
        blank->append(*t);
        tile->append(*blank);
    }
    auto title = Gtk::make_managed<Gtk::Label>(b.title);
    title->set_xalign(0.0f);
    title->set_ellipsize(Pango::EllipsizeMode::END);
    title->set_max_width_chars(16);
    title->set_size_request(150, -1);
    title->add_css_class("tile-title");
    tile->append(*title);
    if (!b.author.empty()) {
        auto author = Gtk::make_managed<Gtk::Label>(b.author);
        author->set_xalign(0.0f);
        author->set_ellipsize(Pango::EllipsizeMode::END);
        author->set_max_width_chars(16);
        author->add_css_class("tile-author");
        tile->append(*author);
    }
    tile->set_tooltip_text(b.title + (b.author.empty() ? "" : "\n" + b.author) + "\n" + b.path);
    return tile;
}
}  // namespace

void Shell::rebuild_library() {  // helper: the grid from the store
    while (auto* c = m_lib_grid.get_child_at_index(0)) m_lib_grid.remove(*c);
    m_lib_paths.clear();
    const auto books = m_library.books();
    for (const auto& b : books) {
        auto* tile = make_tile(b);
        m_lib_grid.append(*tile);
        m_lib_paths.push_back(b.path);
        // Right-click (or a long press on a touch screen) -> Read / Remove.
        if (auto* child = m_lib_grid.get_child_at_index(static_cast<int>(m_lib_paths.size() - 1))) {
            auto click = Gtk::GestureClick::create();
            click->set_button(GDK_BUTTON_SECONDARY);
            const std::string path = b.path;
            click->signal_pressed().connect([this, child, path](int, double x, double y) {
                on_library_menu(*child, path, x, y);
            });
            child->add_controller(click);
        }
    }
    const auto n = books.size();
    m_lib_count.set_text(n == 1 ? "1 book" : std::to_string(n) + " books");
    m_lib_scroll.set_visible(n > 0);
    m_lib_empty.set_visible(n == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// s005 -- focus mode: fullscreen, the rail, header and nav bar away, the page
// alone on near-black (.focus in kaflip.css). The HUD fades in on mouse
// movement or a centre tap and away after a pause. A view state, not an undo
// step.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::set_focus_mode(bool on) {  // helper
    if (on == m_focus) return;
    if (on && !m_reader.has_book()) return;
    m_focus = on;
    if (auto lg = log::get(log::Area::Shell)) lg->info("focus mode {}", on ? "on" : "off");
    if (on) {
        select_tab("reader");
        add_css_class("focus");
        m_rail_scroll.set_visible(false);
        if (m_header) m_header->set_visible(false);
        m_reader_nav.set_visible(false);
        m_reader.set_dark_page(true);   // s006: focus mode honours the app's dark
        fullscreen();
        poke_hud();   // show where the controls are, then fade
    } else {
        m_hud_timer.disconnect();
        show_hud(false);
        remove_css_class("focus");
        m_rail_scroll.set_visible(true);
        if (m_header) m_header->set_visible(true);
        m_reader_nav.set_visible(m_reader.has_book());
        m_reader.set_dark_page(false);
        unfullscreen();
    }
    m_focus_button.set_icon_name(on ? "view-restore-symbolic" : "view-fullscreen-symbolic");
}

void Shell::show_hud(bool on) {  // helper
    m_hud_revealer.set_reveal_child(on);
}

void Shell::poke_hud() {  // helper: show, then hide after a pause
    show_hud(true);
    m_hud_timer.disconnect();
    m_hud_timer = Glib::signal_timeout().connect([this] {
        show_hud(false);
        return false;
    }, 2200);
}

}  // namespace kaflip
