#include "Shell.hpp"
#include "Log.hpp"

#include <gtkmm/headerbar.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/gesturedrag.h>
#include <gtkmm/eventcontrollerfocus.h>
#include <gtkmm/image.h>
#include <gtkmm/separator.h>
#include <cmath>
#include <gtkmm/eventcontrollermotion.h>
#include "core/Tabs.hpp"

// Shell_zones.cpp -- ZONES. Construction of the named regions: the window frame
// and header, the sidebar+stack body, the pattern pages, and the menu model.

namespace kaflip {

void Shell::build_shell() {  // zone: window + sidebar + stack
    set_title("Ka-flip");
    set_default_size(1000, 760);   // s002: a reading window, not a demo panel
    add_css_class("kaflip");       // s004: the skin's root (resources/kaflip.css)

    auto header = Gtk::make_managed<Gtk::HeaderBar>();

    // Brand logo at the start: a frameless button whose icon is a symbolic
    // resource resolved from the compiled-in bundle (App registers it), so this
    // one button exercises the resource pipeline. Click opens the About window
    // (the dialog-lifetime exemplar).
    m_logo_button.set_icon_name("kaflip-logo-symbolic");
    m_logo_button.set_has_frame(false);
    m_logo_button.set_tooltip_text("About Ka-flip");
    m_logo_button.set_action_name("win.about");
    header->pack_start(m_logo_button);

    // s002: Open an EPUB, beside the logo -- the app's first verb.
    m_open_button.set_icon_name("document-open-symbolic");
    m_open_button.set_tooltip_text("Open a book (Ctrl+O)");
    m_open_button.set_action_name("win.open-book");
    header->pack_start(m_open_button);

    m_menu_button.set_icon_name("open-menu-symbolic");
    m_menu_button.set_menu_model(build_menu());
    header->pack_end(m_menu_button);
    // s005: focus mode, beside the menu.
    m_focus_button.set_icon_name("view-fullscreen-symbolic");
    m_focus_button.set_tooltip_text("Focus mode (F11)");
    m_focus_button.set_action_name("win.focus-mode");
    header->pack_end(m_focus_button);
    // s007: Reading style, beside focus mode.
    m_style_button.set_icon_name("kaflip-style-symbolic");
    m_style_button.set_tooltip_text("Reading style (Ctrl+,)");
    m_style_button.set_action_name("win.reading-style");
    header->pack_end(m_style_button);
    set_titlebar(*header);
    m_header = header;

    // s004: Trails' rail instead of a StackSidebar -- each row carries a big
    // coloured glyph (a StackSidebar shows text only). Rows come from
    // core::tab_registry(), the same list the pages and the window tint read.
    build_rail();
    m_stack.set_hexpand(true);
    m_stack.set_vexpand(true);
    m_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_stack.set_transition_duration(120);

    m_body.append(m_rail_scroll);
    m_body.append(m_stack);
    set_child(m_body);
}

void Shell::build_pages() {  // zone: the pattern pages
    build_library_page();  // s006: the home page
    build_reader_page();   // s002

    // Page 1 -- Recents: a live list + explanation. The menu's Open-recent
    // submenu and this list are the same data, two surfaces (CANON: two
    // surfaces, one mechanism).
    // s004: Recent books -- the same list as the menu's "Open recent" (two
    // surfaces, one mechanism), now as rows you click to open.
    m_recents_page.add_css_class("page");
    m_recents_page.set_spacing(14);
    auto intro = Gtk::make_managed<Gtk::Label>("Recent books");
    intro->set_xalign(0.0f);
    intro->add_css_class("panel-title");
    m_recents_page.append(*intro);

    m_recents_list.set_selection_mode(Gtk::SelectionMode::NONE);
    m_recents_list.add_css_class("boxed-list");
    m_recents_list.set_valign(Gtk::Align::START);
    m_recents_list.set_activate_on_single_click(true);
    m_recents_list.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const int i = row ? row->get_index() : -1;
        if (i >= 0 && i < static_cast<int>(m_recents.size())) on_recent_open(m_recents[static_cast<std::size_t>(i)]);
    });
    m_recents_scroll.set_child(m_recents_list);
    m_recents_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_recents_scroll.set_vexpand(true);
    m_recents_page.append(m_recents_scroll);
    m_stack.add(m_recents_page, "recents", "Recent");

    // Page 2 -- Registry: the debuggability payoff. The button dumps the live
    // name->widget tree to the log (also callable from gdb as registry::dump()).
    auto reg = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    reg->add_css_class("page");
    auto reg_intro = Gtk::make_managed<Gtk::Label>();
    reg_intro->set_wrap(true);
    reg_intro->set_xalign(0.0f);
    reg_intro->set_markup(
        "<b>Registry</b> -- the live address book Named&lt;W&gt; fills.\n\n"
        "Every widget here was named at construction and registered by "
        "Named&lt;W&gt;. \"Dump\" prints the live tree -- the same call is your "
        "friend in gdb when a backtrace frame is otherwise a bare pointer. See "
        "docs/widget-naming.md for why this is the LIGHT half of naming.");
    auto dump_btn = Gtk::make_managed<Gtk::Button>("Dump registry to the log");
    dump_btn->set_halign(Gtk::Align::START);
    dump_btn->set_action_name("win.dump-registry");
    reg->append(*reg_intro);
    reg->append(*dump_btn);
    m_stack.add(*reg, "registry", "Registry");

    build_undo_page();   // s046
}

// ─────────────────────────────────────────────────────────────────────────────
// The Undo page (s046) -- the undo stone, exercised. A list of stones; every
// button, the name field and the size slider are user choices, and every one
// is a step (core/Undo.hpp, docs/undo.md). Try: delete a stone and press
// Ctrl+Z without clicking anything; drag the slider and undo once; tap the
// arrow keys on the slider and undo once.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_undo_page() {  // zone: the Undo page
    m_undo_page.add_css_class("page");

    auto intro = Gtk::make_managed<Gtk::Label>();
    intro->set_wrap(true);
    intro->set_xalign(0.0f);
    intro->set_markup(
        "<b>Undo</b> -- every user choice is a step, through one door.\n\n"
        "Add, delete, move, rename, resize: <b>Ctrl+Z</b> takes any of them back, "
        "from anywhere but a text box (the name field keeps its own). A slider "
        "<i>drag</i> is one step; arrow presses within 1.5 s merge into one. See "
        "core/Undo.hpp and docs/undo.md.");
    m_undo_page.append(*intro);

    // Verbs.
    auto verbs = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    const auto verb = [&](const char* label, const char* action, const char* name) {
        auto b = Gtk::make_managed<Gtk::Button>(label);
        b->set_name(name);
        b->set_action_name(action);
        verbs->append(*b);
    };
    verb("Add", "win.stone-add", "shell.undo.add");
    verb("Delete", "win.stone-delete", "shell.undo.delete");
    verb("Up", "win.stone-up", "shell.undo.up");
    verb("Down", "win.stone-down", "shell.undo.down");
    auto spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    verbs->append(*spacer);
    m_undo_button.set_label("Undo");
    m_undo_button.set_action_name("win.undo");
    m_redo_button.set_label("Redo");
    m_redo_button.set_action_name("win.redo");
    verbs->append(m_undo_button);
    verbs->append(m_redo_button);
    m_undo_page.append(*verbs);

    // The list.
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_vexpand(true);
    scroll->set_child(m_stone_list);
    m_stone_list.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_stone_list.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (m_painting) return;
        m_sel = row ? std::string(row->get_name()) : std::string{};
        repaint_stones();   // the detail fields follow the selection
    });
    m_undo_page.append(*scroll);

    // Detail: name + size.
    auto detail = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto nl = Gtk::make_managed<Gtk::Label>("Name");
    m_stone_name.set_hexpand(true);
    m_stone_name.set_valign(Gtk::Align::CENTER);
    m_stone_name.signal_activate().connect(sigc::mem_fun(*this, &Shell::on_stone_rename));
    auto leave = Gtk::EventControllerFocus::create();
    leave->signal_leave().connect(sigc::mem_fun(*this, &Shell::on_stone_rename));
    m_stone_name.add_controller(leave);
    auto sl = Gtk::make_managed<Gtk::Label>("Size");
    m_stone_size.set_range(1, 10);
    m_stone_size.set_increments(1, 1);
    m_stone_size.set_digits(0);
    m_stone_size.set_round_digits(0);
    m_stone_size.set_draw_value(true);
    m_stone_size.set_size_request(180, -1);
    m_stone_size.set_valign(Gtk::Align::CENTER);
    m_stone_size.signal_value_changed().connect(sigc::mem_fun(*this, &Shell::on_stone_size));
    // A drag is ONE step: open the gesture at press, close it at release.
    // CAPTURE, so it sees the press before the slider's own gesture claims
    // it; a claimed sequence arrives here as cancel -- also the end.
    auto drag = Gtk::GestureDrag::create();
    drag->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    drag->signal_drag_begin().connect([this](double, double) {
        if (!m_sel.empty()) core::stones::drag_begin(m_journal, m_stones, m_sel);
    });
    const auto end = [this]() { core::stones::drag_end(m_journal); };
    drag->signal_drag_end().connect([end](double, double) { end(); });
    drag->signal_cancel().connect([end](const Gdk::EventSequence*) { end(); });
    m_stone_size.add_controller(drag);
    detail->append(*nl);
    detail->append(m_stone_name);
    detail->append(*sl);
    detail->append(m_stone_size);
    m_undo_page.append(*detail);

    m_undo_said.set_xalign(0.0f);
    m_undo_said.add_css_class("dim-label");
    m_undo_page.append(m_undo_said);

    m_stack.add(m_undo_page, "undo", "Undo");
}

// ─────────────────────────────────────────────────────────────────────────────
// The Reader page (s002): the web surface, and under it a nav bar --
// previous | "3 of 12" | next. The buttons name actions; bind_actions greys
// them through update_reader_ui().
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_reader_page() {  // zone: the Reader page
    m_reader_page.add_css_class("reader-page");
    build_hero();
    m_reader_page.append(m_hero);
    m_reader.set_vexpand(true);
    m_reader.set_visible(false);   // the hero shows until a book opens
    m_reader_page.append(m_reader);

    m_reader_nav.add_css_class("reader-nav");
    m_reader_nav.set_halign(Gtk::Align::CENTER);
    m_reader_nav.set_visible(false);
    // s003: the arrows turn PAGES (crossing chapters at the ends), the way a
    // reader expects; whole chapters are Ctrl+Page Up / Down.
    m_prev_button.set_icon_name("go-previous-symbolic");
    m_prev_button.set_tooltip_text("Previous page (Page Up, \u2190)");
    m_prev_button.set_action_name("win.page-prev");
    m_next_button.set_icon_name("go-next-symbolic");
    m_next_button.set_tooltip_text("Next page (Page Down, \u2192, Space)");
    m_next_button.set_action_name("win.page-next");
    // Clicking an arrow leaves the keyboard in the book, so Page Down still works.
    m_prev_button.set_focus_on_click(false);
    m_next_button.set_focus_on_click(false);
    m_chapter_label.add_css_class("dim-label");
    m_chapter_label.set_margin_start(10);
    m_chapter_label.set_margin_end(10);
    // s008: Contents, at the left of the bar.
    m_toc_button.set_icon_name("view-list-symbolic");
    m_toc_button.set_tooltip_text("Contents (F9)");
    m_toc_button.set_action_name("win.contents");
    m_toc_button.set_focus_on_click(false);
    m_reader_nav.append(m_toc_button);
    m_reader_nav.append(m_prev_button);
    m_reader_nav.append(m_chapter_label);
    m_reader_nav.append(m_next_button);
    // s008a: text size, right in the bar -- "A" small, a slider, "A" big.
    const auto size_slider = [this](widgets::Scale& sc, widgets::Box& into) {
        auto small = Gtk::make_managed<Gtk::Label>("A");
        small->add_css_class("size-small");
        auto big = Gtk::make_managed<Gtk::Label>("A");
        big->add_css_class("size-big");
        sc.set_range(60, 220);
        sc.set_increments(5, 10);
        sc.set_digits(0);
        sc.set_round_digits(0);
        sc.set_draw_value(false);
        sc.set_size_request(140, -1);
        sc.set_focus_on_click(false);
        sc.set_tooltip_text("Text size (Ctrl+plus / Ctrl+minus / Ctrl+0)");
        sc.add_mark(100, Gtk::PositionType::BOTTOM, "");
        sc.signal_value_changed().connect([this, &sc] {
            if (m_size_painting) return;
            set_text_size(int(std::lround(sc.get_value())), true);
        });
        auto sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
        sep->set_margin_start(6);
        sep->set_margin_end(6);
        into.append(*sep);
        into.append(*small);
        into.append(sc);
        into.append(*big);
    };
    size_slider(m_size_scale, m_reader_nav);
    m_reader_page.append(m_reader_nav);

    m_reader.signal_changed().connect(sigc::mem_fun(*this, &Shell::update_reader_ui));
    m_reader.signal_tap().connect(sigc::mem_fun(*this, &Shell::on_reader_tap));   // s005

    // s005 -- the focus HUD: a pill that fades in over the bottom of the book.
    // Its buttons never take the keyboard, so Page Down keeps working.
    m_hud.add_css_class("focus-hud");
    const auto hud_button = [&](const char* icon, const char* action, const char* tip) {
        auto b = Gtk::make_managed<Gtk::Button>();
        b->set_icon_name(icon);
        b->set_action_name(action);
        b->set_tooltip_text(tip);
        b->set_focus_on_click(false);
        return b;
    };
    m_hud_toc = hud_button("view-list-symbolic", "win.contents", "Contents (F9)");   // s008
    m_hud.append(*m_hud_toc);
    m_hud.append(*hud_button("go-previous-symbolic", "win.page-prev", "Previous page"));
    m_hud_label.set_margin_start(10);
    m_hud_label.set_margin_end(10);
    m_hud.append(m_hud_label);
    m_hud.append(*hud_button("go-next-symbolic", "win.page-next", "Next page"));
    size_slider(m_hud_size, m_hud);   // s008a
    m_hud.append(*hud_button("view-restore-symbolic", "win.focus-mode", "Leave focus mode (Esc)"));
    m_hud_revealer.set_child(m_hud);
    m_hud_revealer.set_transition_type(Gtk::RevealerTransitionType::CROSSFADE);
    m_hud_revealer.set_transition_duration(250);
    m_hud_revealer.set_reveal_child(false);
    m_hud_revealer.set_halign(Gtk::Align::CENTER);
    m_hud_revealer.set_valign(Gtk::Align::END);

    m_reader_overlay.set_child(m_reader_page);
    m_reader_overlay.add_overlay(m_hud_revealer);

    // Moving the mouse brings the HUD back (it fades again after a pause).
    // Motion that does not move (a synthetic re-send) is ignored, or the bar
    // would never stay hidden.
    auto motion = Gtk::EventControllerMotion::create();
    motion->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    motion->signal_motion().connect([this](double x, double y) {
        if (!m_focus) return;
        if (std::abs(x - m_last_x) + std::abs(y - m_last_y) < 3) return;
        m_last_x = x;
        m_last_y = y;
        poke_hud();
    });
    m_reader_overlay.add_controller(motion);

    m_stack.add(m_reader_overlay, "reader", "Reader");
}

// s004: the Reader's empty state -- the teal orb, a line, an Open pill.
void Shell::build_hero() {  // zone: the hero
    m_hero.add_css_class("hero");
    m_hero.set_valign(Gtk::Align::CENTER);
    m_hero.set_halign(Gtk::Align::CENTER);
    m_hero.set_vexpand(true);
    auto orb = Gtk::make_managed<Gtk::Image>();
    orb->set_from_icon_name("kaflip-hero-logo");
    orb->set_pixel_size(144);
    auto title = Gtk::make_managed<Gtk::Label>("Ka-flip");
    title->add_css_class("hero-title");
    auto blurb = Gtk::make_managed<Gtk::Label>("Open an EPUB to start reading.");
    blurb->add_css_class("hero-blurb");
    auto open = Gtk::make_managed<Gtk::Button>("Open a book\u2026");
    open->add_css_class("suggested-action");
    open->set_halign(Gtk::Align::CENTER);
    open->set_margin_top(8);
    open->set_action_name("win.open-book");
    auto recent = Gtk::make_managed<Gtk::Button>("Library");   // s006
    recent->add_css_class("text-button");   // ghost pill
    recent->set_halign(Gtk::Align::CENTER);
    recent->signal_clicked().connect([this] { select_tab("library"); });
    m_hero.append(*orb);
    m_hero.append(*title);
    m_hero.append(*blurb);
    m_hero.append(*open);
    m_hero.append(*recent);
}

// ─────────────────────────────────────────────────────────────────────────────
// s006 -- the Library: a header (count, Add folder, Open), a "working on ..."
// row while a folder scans, and a grid of covers. Click a cover to read it;
// right-click to remove it from the library (Ctrl+Z puts it back).
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_library_page() {  // zone: the Library
    m_library_page.add_css_class("page");

    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    auto title = Gtk::make_managed<Gtk::Label>("Library");
    title->add_css_class("panel-title");
    m_lib_count.add_css_class("dim-label");
    m_lib_count.set_valign(Gtk::Align::BASELINE);
    auto spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    auto add = Gtk::make_managed<Gtk::Button>("Add folder\u2026");
    add->set_action_name("win.add-folder");
    add->set_tooltip_text("Add every EPUB in a folder (and its subfolders)");
    add->set_valign(Gtk::Align::CENTER);
    auto open = Gtk::make_managed<Gtk::Button>("Open a book\u2026");
    open->add_css_class("suggested-action");
    open->set_action_name("win.open-book");
    open->set_valign(Gtk::Align::CENTER);
    head->append(*title);
    head->append(m_lib_count);
    head->append(*spacer);
    head->append(*add);
    head->append(*open);
    m_library_page.append(*head);

    m_lib_said.set_xalign(0.0f);
    m_lib_said.add_css_class("dim-label");
    m_lib_said.set_visible(false);
    m_library_page.append(m_lib_said);

    // "working on ..." -- REQUIREMENTS: long jobs show what they are doing.
    m_lib_busy.add_css_class("panel");
    m_lib_progress.set_hexpand(true);
    m_lib_progress.set_valign(Gtk::Align::CENTER);
    m_lib_status.set_xalign(0.0f);
    m_lib_status.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    m_lib_status.set_size_request(320, -1);
    m_lib_busy.append(m_lib_status);
    m_lib_busy.append(m_lib_progress);
    m_lib_busy.set_visible(false);
    m_library_page.append(m_lib_busy);

    m_lib_grid.add_css_class("library-grid");
    m_lib_grid.set_selection_mode(Gtk::SelectionMode::NONE);
    m_lib_grid.set_activate_on_single_click(true);
    m_lib_grid.set_homogeneous(true);
    m_lib_grid.set_min_children_per_line(1);
    m_lib_grid.set_max_children_per_line(24);
    m_lib_grid.set_row_spacing(22);
    m_lib_grid.set_column_spacing(18);
    m_lib_grid.set_valign(Gtk::Align::START);
    m_lib_grid.signal_child_activated().connect([this](Gtk::FlowBoxChild* c) {
        const int i = c ? c->get_index() : -1;
        if (i >= 0 && i < static_cast<int>(m_lib_paths.size()))
            open_book(m_lib_paths[static_cast<std::size_t>(i)]);
    });
    m_lib_scroll.set_child(m_lib_grid);
    m_lib_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_lib_scroll.set_vexpand(true);
    m_library_page.append(m_lib_scroll);

    m_lib_empty.add_css_class("hero");
    m_lib_empty.set_vexpand(true);
    m_lib_empty.set_valign(Gtk::Align::CENTER);
    auto e1 = Gtk::make_managed<Gtk::Label>("Your library is empty");
    e1->add_css_class("hero-title");
    auto e2 = Gtk::make_managed<Gtk::Label>("Add a folder of EPUBs, or open a book -- it lands here.");
    e2->add_css_class("hero-blurb");
    m_lib_empty.append(*e1);
    m_lib_empty.append(*e2);
    m_library_page.append(m_lib_empty);

    // The scan reports back on the main loop; the folder becomes ONE step.
    m_scanner.signal_progress().connect([this](int done, int total, const std::string& now) {
        m_lib_busy.set_visible(true);
        m_lib_progress.set_fraction(total > 0 ? double(done) / total : 0.0);
        m_lib_status.set_text(total == 0 ? "Looking for books\u2026"
                              : "Reading " + std::to_string(done) + " of " + std::to_string(total) +
                                    (now.empty() ? "" : "  \u00b7  " + now));
    });
    m_scanner.signal_done().connect([this](const std::string& folder, std::vector<core::LibItem> found) {
        m_lib_busy.set_visible(false);
        const auto n = found.size();
        const bool added = core::library::add_folder(m_lib_journal, m_library, folder, std::move(found));
        m_lib_said.set_text(added ? "Added " + std::to_string(n) + " book(s) from " + folder + "  (Ctrl+Z takes them back)"
                                  : folder + " is already in the library");
        m_lib_said.set_visible(true);
    });

    m_stack.add(m_library_page, "library", "Library");
}

// s004: the rail (Trails' shape). A row's NAME is its page id.
void Shell::build_rail() {  // zone: the rail
    m_rail.add_css_class("rail");
    m_rail.set_selection_mode(Gtk::SelectionMode::BROWSE);
    for (const auto& t : core::tab_registry()) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name(std::string(t.id));
        row->add_css_class(std::string(t.css));
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        auto icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(std::string(t.icon));
        icon->set_pixel_size(28);
        icon->add_css_class("rail-icon");
        auto label = Gtk::make_managed<Gtk::Label>(std::string(t.title));
        label->set_xalign(0.0f);
        label->add_css_class("rail-label");
        box->append(*icon);
        box->append(*label);
        row->set_child(*box);
        m_rail.append(*row);
    }
    m_rail.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (m_syncing || !row) return;
        select_tab(std::string(row->get_name()));
    });
    m_rail_scroll.set_child(m_rail);
    m_rail_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_rail_scroll.set_size_request(190, -1);
    m_rail_scroll.add_css_class("rail-scroll");
}

Glib::RefPtr<Gio::Menu> Shell::build_menu() {  // zone: hamburger model
    auto menu = Gio::Menu::create();

    auto file = Gio::Menu::create();
    file->append("Open book\u2026", "win.open-book");   // s002
    file->append("Add folder to library\u2026", "win.add-folder");   // s006
    file->append("Reading style\u2026", "win.reading-style");          // s007
    file->append("Remember a folder\u2026", "win.pick-folder");
    m_recents_menu = Gio::Menu::create();
    file->append_submenu("Open recent", m_recents_menu);
    menu->append_section(file);

    // s046: Undo / Redo, re-labelled live ("Undo Delete “Jade”") and greyed
    // when there is nothing to do -- update_undo_ui() owns this section.
    m_edit_menu = Gio::Menu::create();
    m_edit_menu->append("Undo", "win.undo");
    m_edit_menu->append("Redo", "win.redo");
    menu->append_section(m_edit_menu);

    auto diag = Gio::Menu::create();
    diag->append("Dump registry", "win.dump-registry");
    menu->append_section(diag);

    auto app = Gio::Menu::create();
    app->append("About Ka-flip", "win.about");
    app->append("Quit", "win.quit");
    menu->append_section(app);
    return menu;
}

}  // namespace kaflip
