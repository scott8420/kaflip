#include "Shell.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "StyleDialog.hpp"
#include "Log.hpp"
#include "Registry.hpp"
#include "core/Recents.hpp"

#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/popover.h>
#include <gtkmm/listboxrow.h>   // s008: Contents
#include <gtkmm/adjustment.h>
#include <gtkmm/togglebutton.h>
#include "Covers.hpp"
#include <functional>
#include <memory>
#include <algorithm>
#include <glibmm/main.h>
#include <giomm/liststore.h>
#include <giomm/asyncresult.h>
#include <giomm/file.h>

#include <filesystem>

// Shell_handlers.cpp -- HANDLERS. Slot bodies: what a user action actually does.
// Each is small and single-concern; orchestration it shares lives in helpers.

namespace kaflip {

void Shell::on_pick_folder() {  // handler: folder chooser -> remember it
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Pick a folder to remember");
    dialog->select_folder(*this,
        [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto dir = dialog->select_folder_finish(result);
                if (!dir) return;
                const std::string folder = dir->get_path();
                if (!folder.empty()) note_recent(folder);
            } catch (const Glib::Error& e) {
                if (auto lg = log::get(log::Area::Shell))
                    lg->info("pick-folder: dialog dismissed ({})", e.what());
            }
        });
}

// s002 -- open an EPUB. The chooser filters to EPUB but offers "All files"
// too: a book saved as .zip, or with no extension, still opens if it is one.
void Shell::on_open_book() {  // handler: EPUB file chooser -> open_book
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Open a book");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto epub = Gtk::FileFilter::create();
    epub->set_name("EPUB books");
    epub->add_mime_type("application/epub+zip");
    epub->add_suffix("epub");
    filters->append(epub);
    auto all = Gtk::FileFilter::create();
    all->set_name("All files");
    all->add_pattern("*");
    filters->append(all);
    dialog->set_filters(filters);
    dialog->set_default_filter(epub);
    dialog->open(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto file = dialog->open_finish(result);
            if (file && !file->get_path().empty()) open_book(file->get_path());
        } catch (const Glib::Error& e) {
            if (auto lg = log::get(log::Area::Shell))
                lg->info("open-book: dialog dismissed ({})", e.what());
        }
    });
}

void Shell::on_chapter_next() { m_reader.next(); }   // handler: next linear spine item
void Shell::on_chapter_prev() { m_reader.prev(); }   // handler: previous linear spine item
void Shell::on_page_next() { m_reader.turn_forward(); }   // handler: next page (s003)
void Shell::on_page_prev() { m_reader.turn_back(); }      // handler: previous page (s003)

// s006 -- add a folder: pick it, scan it off the main thread (Scanner), and
// the scan's end makes the one undo step (build_library_page wires that).
void Shell::on_add_folder() {  // handler
    if (m_scanner.busy()) return;
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Add a folder of books");
    dialog->select_folder(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto dir = dialog->select_folder_finish(result);
            if (!dir || dir->get_path().empty()) return;
            select_tab("library");
            m_lib_said.set_visible(false);
            m_scanner.start(dir->get_path());
        } catch (const Glib::Error& e) {
            if (auto lg = log::get(log::Area::Library)) lg->info("add-folder: dismissed ({})", e.what());
        }
    });
}

// s007 -- the Reading style window: built once, hidden on close (the About
// window's lifetime). It opens on the style in use: focus mode's when in it.
void Shell::on_reading_style() {  // handler
    if (!m_style_dialog) {
        m_style_dialog = std::make_unique<StyleDialog>(m_styles, m_style_journal);
        m_style_dialog->set_transient_for(*this);
    }
    // s010: the book on screen, so the window can give it its own style.
    if (const core::Book* b = m_reader.book())
        m_style_dialog->set_book(m_style_book, b->meta().title.empty()
            ? std::filesystem::path(m_style_book).stem().string() : b->meta().title);
    m_style_dialog->edit(m_focus ? core::Styles::kFocus : core::Styles::kReading);
    m_style_dialog->present();
}

void Shell::on_library_remove(const std::string& path) {  // handler: one undo step
    if (core::library::remove_book(m_lib_journal, m_library, path)) {
        m_lib_said.set_text("Removed from the library  (Ctrl+Z puts it back; the file is untouched)");
        m_lib_said.set_visible(true);
    }
}

// Right-click a cover: a one-item menu. Built per click and parented to the
// tile; unparented once it closes (a popover must not outlive its parent).
// s011 -- Ctrl+F: the Library, the search box focused, its text selected so
// typing replaces the last search.
void Shell::on_library_search() {  // handler
    if (m_focus) set_focus_mode(false);
    select_tab("library");
    m_lib_search.grab_focus();
    m_lib_search.select_region(0, -1);
}

void Shell::on_library_menu(Gtk::Widget& tile, const std::string& path, double x, double y) {  // handler
    auto pop = Gtk::make_managed<Gtk::Popover>();
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    auto read = Gtk::make_managed<Gtk::Button>("Read");
    read->set_has_frame(false);
    auto remove = Gtk::make_managed<Gtk::Button>("Remove from library");
    remove->set_has_frame(false);
    box->append(*read);
    box->append(*remove);
    pop->set_child(*box);
    pop->set_parent(tile);
    pop->set_pointing_to(Gdk::Rectangle(int(x), int(y), 1, 1));
    pop->set_has_arrow(true);
    read->signal_clicked().connect([this, pop, path] { pop->popdown(); open_book(path); });
    remove->signal_clicked().connect([this, pop, path] { pop->popdown(); on_library_remove(path); });
    pop->signal_closed().connect([pop] {
        Glib::signal_idle().connect_once([pop] { pop->unparent(); });
    });
    pop->popup();
}

// s008: Contents and Bookmarks -- one popover, two tabs, built per open
// (the library menu's shape), hung off whichever list button is showing: the
// bar's, or the focus HUD's (the HUD stays up while it is open). A click (or
// Enter) jumps -- a view position, like a page turn, not an undo step -- and
// the keyboard goes back to the page. Removing a bookmark IS a step.
void Shell::on_contents()  { open_nav(false); }   // handler: F9
void Shell::on_bookmarks() { open_nav(true); }    // handler: Ctrl+Shift+B

void Shell::open_nav(bool marks_tab) {  // helper
    const core::Book* b = m_reader.book();
    if (!b || m_toc_open) return;
    if (b->toc().empty()) marks_tab = true;   // nothing else to show
    Gtk::Widget* anchor = &m_toc_button;
    if (m_focus) {
        anchor = m_hud_toc;
        m_hud_timer.disconnect();
        show_hud(true);
    } else if (m_stack.get_visible_child_name() != "reader") {
        select_tab("reader");
    }
    if (!anchor) return;
    const std::string book_path = b->file();

    auto pop = Gtk::make_managed<Gtk::Popover>();
    pop->add_css_class("toc-popover");
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    auto tabs = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    tabs->add_css_class("linked");
    tabs->add_css_class("nav-tabs");
    tabs->set_halign(Gtk::Align::START);
    tabs->set_margin_start(6);
    auto t_toc = Gtk::make_managed<Gtk::ToggleButton>("Contents");
    auto t_marks = Gtk::make_managed<Gtk::ToggleButton>("Bookmarks");
    t_marks->set_group(*t_toc);
    t_toc->set_sensitive(!b->toc().empty());
    tabs->append(*t_toc);
    tabs->append(*t_marks);
    box->append(*tabs);
    auto stack = Gtk::make_managed<Gtk::Stack>();
    stack->set_vhomogeneous(false);
    stack->set_interpolate_size(true);
    box->append(*stack);

    const auto make_scroll = [this]() {
        auto sc = Gtk::make_managed<Gtk::ScrolledWindow>();
        sc->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        sc->set_propagate_natural_height(true);
        sc->set_max_content_height(std::max(240, get_height() * 2 / 3));
        sc->set_size_request(360, -1);
        return sc;
    };

    // ── Contents ────────────────────────────────────────────────────────────
    auto scroll = make_scroll();
    auto list = Gtk::make_managed<Gtk::ListBox>();
    list->add_css_class("toc-list");
    list->set_selection_mode(Gtk::SelectionMode::SINGLE);
    list->set_activate_on_single_click(true);
    const auto& toc = b->toc();
    const int here = core::toc_index_for(toc, m_reader.chapter());
    Gtk::ListBoxRow* here_row = nullptr;
    for (std::size_t i = 0; i < toc.size(); ++i) {
        const auto& e = toc[i];
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto lab = Gtk::make_managed<Gtk::Label>(e.title.empty() ? "(untitled)" : e.title);
        lab->set_xalign(0.0f);
        lab->set_ellipsize(Pango::EllipsizeMode::END);
        lab->set_margin_start(10 + 18 * std::min(e.depth, 4));
        lab->set_margin_end(10);
        lab->set_margin_top(5);
        lab->set_margin_bottom(5);
        if (e.depth == 0) lab->add_css_class("toc-top");
        row->set_child(*lab);
        row->set_tooltip_text(e.title);
        if (e.spine < 0) {   // a heading, or a link outside the book
            row->set_activatable(false);
            row->set_selectable(false);
            lab->add_css_class("dim-label");
        }
        list->append(*row);
        if (static_cast<int>(i) == here) here_row = row;
    }
    list->signal_row_activated().connect([this, pop, b](Gtk::ListBoxRow* row) {
        const int i = row->get_index();
        if (i < 0 || static_cast<std::size_t>(i) >= b->toc().size()) return;
        const auto e = b->toc()[static_cast<std::size_t>(i)];   // copy: the jump may repaint
        pop->popdown();
        m_reader.go_to_path(e.path, e.fragment);
    });
    scroll->set_child(*list);
    stack->add(*scroll, "toc");

    // ── Bookmarks ───────────────────────────────────────────────────────────
    auto mscroll = make_scroll();
    auto mlist = Gtk::make_managed<Gtk::ListBox>();
    mlist->add_css_class("toc-list");
    mlist->set_selection_mode(Gtk::SelectionMode::SINGLE);
    mlist->set_activate_on_single_click(true);
    mlist->set_placeholder(*Gtk::make_managed<Gtk::Label>("No bookmarks yet.\nCtrl+B marks the page you are on."));
    mscroll->set_child(*mlist);
    stack->add(*mscroll, "marks");
    // Rebuilt in place after a remove (the popover stays open).
    auto shown = std::make_shared<std::vector<core::Bookmark>>();
    auto fill = std::make_shared<std::function<void()>>();
    *fill = [this, mlist, shown, book_path, fill_w = std::weak_ptr<std::function<void()>>(fill)]() {
        while (auto* c = mlist->get_first_child()) mlist->remove(*c);
        shown->clear();
        if (auto item = m_library.get(core::Library::book_id(book_path))) *shown = item->marks;
        for (const auto& m : *shown) {
            auto row = Gtk::make_managed<Gtk::ListBoxRow>();
            auto line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            auto text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
            text->set_hexpand(true);
            auto t = Gtk::make_managed<Gtk::Label>(m.title.empty() ? "Bookmark" : m.title);
            t->set_xalign(0.0f);
            t->set_ellipsize(Pango::EllipsizeMode::END);
            t->add_css_class("toc-top");
            auto sn = Gtk::make_managed<Gtk::Label>(m.snippet);
            sn->set_xalign(0.0f);
            sn->set_ellipsize(Pango::EllipsizeMode::END);
            sn->add_css_class("dim-label");
            sn->add_css_class("mark-snippet");
            text->append(*t);
            if (!m.snippet.empty()) text->append(*sn);
            auto del = Gtk::make_managed<Gtk::Button>();
            del->set_icon_name("edit-delete-symbolic");
            del->set_has_frame(false);
            del->set_valign(Gtk::Align::CENTER);
            del->set_tooltip_text("Remove this bookmark (Ctrl+Z puts it back)");
            const int id = m.id;
            del->signal_clicked().connect([this, book_path, id, fill_w] {
                core::library::remove_bookmark(m_lib_journal, m_library, book_path, id);
                if (auto f = fill_w.lock()) Glib::signal_idle().connect_once([f] { (*f)(); });
            });
            line->set_margin_start(10);
            line->set_margin_end(4);
            line->set_margin_top(5);
            line->set_margin_bottom(5);
            line->append(*text);
            line->append(*del);
            row->set_child(*line);
            row->set_tooltip_text(m.snippet);
            mlist->append(*row);
        }
    };
    (*fill)();
    mlist->signal_row_activated().connect([this, pop, shown](Gtk::ListBoxRow* row) {
        const int i = row->get_index();
        if (i < 0 || static_cast<std::size_t>(i) >= shown->size()) return;
        const core::Place at = (*shown)[static_cast<std::size_t>(i)].at;
        pop->popdown();
        m_reader.go_to_place(at);
    });

    t_toc->signal_toggled().connect([stack, t_toc] { if (t_toc->get_active()) stack->set_visible_child("toc"); });
    t_marks->signal_toggled().connect([stack, t_marks] { if (t_marks->get_active()) stack->set_visible_child("marks"); });
    (marks_tab ? t_marks : t_toc)->set_active(true);
    stack->set_visible_child(marks_tab ? "marks" : "toc");

    pop->set_child(*box);
    pop->set_parent(*anchor);
    pop->set_position(Gtk::PositionType::TOP);
    m_toc_open = true;
    pop->signal_closed().connect([this, pop, fill] {   // `fill` lives as long as the popover
        m_toc_open = false;
        if (m_focus) poke_hud();
        m_reader.focus_page();
        Glib::signal_idle().connect_once([pop] { pop->unparent(); });
    });
    pop->popup();
    if (marks_tab) {
        if (auto* r = mlist->get_row_at_index(0)) r->grab_focus(); else mlist->grab_focus();
    } else if (here_row) {
        list->select_row(*here_row);
        // Scroll to it once the list has its size.
        Glib::signal_idle().connect_once([scroll, here_row] {
            Gtk::Allocation a = here_row->get_allocation();
            auto adj = scroll->get_vadjustment();
            adj->set_value(std::max(0.0, a.get_y() - adj->get_page_size() / 3));
            here_row->grab_focus();
        });
    } else {
        list->grab_focus();
    }
}

// s008: Ctrl+B / the bar's bookmark button. A bookmark on the page on screen
// -> remove it; none -> add one at the page's first character, named by the
// chapter's TOC title and the page's first words. Each is ONE undo step on the
// library journal (Ctrl+Z on the reader page means the library).
void Shell::on_bookmark_toggle() {  // handler
    const core::Book* b = m_reader.book();
    const auto at = m_reader.place();
    if (!b || !at || m_reader.page_start() < 0) return;
    const std::string path = b->file();
    if (!m_library.has_book(path))   // removed from the library while open
        core::library::note_opened(m_library, covers::describe(*b, path));
    const auto item = m_library.get(core::Library::book_id(path));
    if (!item) return;
    const bool last = m_reader.page() + 1 >= m_reader.page_count();
    const auto ids = core::library::marks_between(*item, at->href, m_reader.page_start(),
                                                  last ? -1 : m_reader.page_end());
    if (!ids.empty()) {
        for (int id : ids) core::library::remove_bookmark(m_lib_journal, m_library, path, id);
        return;
    }
    core::Bookmark mk;
    mk.at = core::Place{at->href, m_reader.page_start()};
    mk.spine = m_reader.chapter();
    mk.title = where_title();
    if (mk.title.empty()) mk.title = "Chapter " + std::to_string(m_reader.chapter() + 1);
    mk.snippet = m_reader.snippet();
    core::library::add_bookmark(m_lib_journal, m_library, path, mk);
}

void Shell::on_focus_mode() { set_focus_mode(!m_focus); }  // handler: F11 (s005)

// s005: tap zones mean something only in focus mode -- outside it a click in
// the page is for links and selecting text, never a surprise page turn.
void Shell::on_reader_tap(const std::string& zone) {  // handler
    if (!m_focus) return;
    if (zone == "back")         m_reader.turn_back();
    else if (zone == "forward") m_reader.turn_forward();
    else if (m_hud_revealer.get_reveal_child()) show_hud(false);
    else                        poke_hud();
}

void Shell::on_recent_open(const std::string& path) {  // handler: open a recent
    if (path.empty()) return;
    // s002: a recent that is a FILE is a book -- open it (open_book promotes it).
    if (std::error_code ec; std::filesystem::is_regular_file(path, ec)) {
        open_book(path);
        return;
    }
    // A real app would load the project here. The seed just proves the routing:
    // a vanished folder self-prunes; a live one is promoted back to the front.
    if (!std::filesystem::exists(path)) {
        core::recents_remove(m_recents, path);
        core::save_recents(m_recents_file, m_recents);
        rebuild_recents_menu();
        rebuild_recents_page();
        if (auto lg = log::get(log::Area::Recents))
            lg->warn("open '{}': gone -- pruned", path);
        return;
    }
    if (auto lg = log::get(log::Area::Recents)) lg->info("open '{}'", path);
    note_recent(path);   // move-to-front
}

void Shell::on_recent_clear() {  // handler: empty the recents list
    m_recents.clear();
    core::save_recents(m_recents_file, m_recents);
    rebuild_recents_menu();
    rebuild_recents_page();
}

void Shell::on_dump_registry() {  // handler: print the live widget tree
    // The debuggability payoff, on demand. Same call works from a gdb prompt.
    registry::dump();
}

void Shell::on_shortcuts() {  // handler: open the keyboard reference
    // Same lifetime stone as About: build once, re-present. The window's CONTENT
    // is not written here -- it renders by walking core::shortcut_registry(),
    // the same list bind_accelerators() wires from, so the two can't drift.
    if (!m_shortcuts) m_shortcuts = std::make_unique<ShortcutsDialog>();
    m_shortcuts->show(*this);
}

void Shell::on_about() {  // handler: open the About window (dialog-lifetime exemplar)
    // Lazily build once, then re-present. Hide-on-close singleton (CANON:
    // "Lifetime shape is design") -- the X hides it, so one instance lives for
    // the session and its named children never re-register.
    if (!m_about) m_about = std::make_unique<AboutWindow>(*this);
    m_about->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// Undo / Redo -- the library's journal (s010: the stones' page is gone).
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_undo() {  // handler: take the last step back
    const auto r = m_lib_journal.undo();
    if (!r.done) return;
    m_lib_said.set_text("Undid: " + r.label);
    m_lib_said.set_visible(true);
    if (auto lg = log::get(log::Area::Library)) lg->info("undo: {}", r.label);
}

void Shell::on_redo() {  // handler: put the undone step back
    const auto r = m_lib_journal.redo();
    if (!r.done) return;
    m_lib_said.set_text("Redid: " + r.label);
    m_lib_said.set_visible(true);
    if (auto lg = log::get(log::Area::Library)) lg->info("redo: {}", r.label);
}

}  // namespace kaflip
