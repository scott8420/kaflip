#include "Shell.hpp"
#include "AboutWindow.hpp"      // complete type for ~unique_ptr<AboutWindow> in ~Shell()
#include "ShortcutsDialog.hpp"  // ditto -- same singleton shape, same dtor requirement
#include "StyleDialog.hpp"      // s007: same again
#include "Log.hpp"

// Shell.cpp -- GLUE. The ctor and the ordered build_ui() pass live here; every
// other method body lives in a category file (see the index in Shell.hpp).

namespace kaflip {

Shell::Shell()
    : m_logo_button("shell.logo_button"),
      m_open_button("shell.open_button"),
      m_focus_button("shell.focus_button"),
      m_style_button("shell.style_button"),
      m_size_scale("shell.reader.size", Gtk::Orientation::HORIZONTAL),
      m_hud_size("shell.focus.size", Gtk::Orientation::HORIZONTAL),
      m_menu_button("shell.menu_button"),
      m_body("shell.body", Gtk::Orientation::HORIZONTAL),
      m_rail_scroll("shell.rail_scroll"),
      m_rail("shell.rail"),
      m_stack("shell.stack"),
      m_recents_page("shell.recents_page", Gtk::Orientation::VERTICAL),
      m_recents_scroll("shell.recents_scroll"),
      m_recents_list("shell.recents_list"),
      m_undo_page("shell.undo.page", Gtk::Orientation::VERTICAL, 8),
      m_stone_list("shell.undo.list"),
      m_stone_name("shell.undo.name"),
      m_stone_size("shell.undo.size", Gtk::Orientation::HORIZONTAL),
      m_undo_button("shell.undo.undo"),
      m_redo_button("shell.undo.redo"),
      m_undo_said("shell.undo.said"),
      m_reader_overlay("shell.reader.overlay"),
      m_reader_page("shell.reader.page", Gtk::Orientation::VERTICAL),
      m_hero("shell.reader.hero", Gtk::Orientation::VERTICAL, 14),
      m_reader_nav("shell.reader.nav", Gtk::Orientation::HORIZONTAL, 6),
      m_toc_button("shell.reader.contents"),
      m_mark_button("shell.reader.bookmark"),
      m_prev_button("shell.reader.prev"),
      m_chapter_label("shell.reader.chapter"),
      m_next_button("shell.reader.next"),
      m_hud_revealer("shell.focus.hud_revealer"),
      m_hud("shell.focus.hud", Gtk::Orientation::HORIZONTAL, 6),
      m_hud_label("shell.focus.hud_label"),
      m_library_page("shell.library.page", Gtk::Orientation::VERTICAL, 14),
      m_lib_count("shell.library.count"),
      m_lib_said("shell.library.said"),
      m_lib_busy("shell.library.busy", Gtk::Orientation::HORIZONTAL, 12),
      m_lib_progress("shell.library.progress"),
      m_lib_status("shell.library.status"),
      m_lib_scroll("shell.library.scroll"),
      m_lib_grid("shell.library.grid"),
      m_lib_empty("shell.library.empty", Gtk::Orientation::VERTICAL, 8) {
    set_name("shell.window");
}

Shell::~Shell() = default;

// The ordered pass Scott's Controller/MainWindow shape names: construct the
// shell, build the pages, bind actions + accelerators, load persisted state,
// paint. Each step is one call into a category file.
void Shell::build_ui() {
    build_shell();     // zone
    build_pages();     // zone
    bind_actions();    // bindings

    // Load persisted recents (the UI resolves the XDG path -- the seam -- and
    // hands the core a plain string), then paint both surfaces.
    m_recents_file = recents_file();
    // m_recents filled by note_recent / load; load happens in bind_actions after
    // the action group exists so a click can route. Paint whatever's there.
    rebuild_recents_menu();
    rebuild_recents_page();
    update_reader_ui();   // s002: nothing open yet -> prev/next grey
    // s006: the library, loaded (not a user choice) and painted; the app opens on it.
    // s007: the reading styles (not a user choice to load) -> the book sheet.
    m_style_file = style_file();
    {
        core::Styles::Loading load(m_styles);
        m_styles.load(m_style_file);
    }
    apply_style();

    m_library_file = library_file();
    m_library.load(m_library_file);
    // s008: the reading position is saved after a pause; closing must not lose it.
    signal_close_request().connect([this] { save_library_now(); return false; }, false);
    rebuild_library();
    select_tab("library");

    if (auto lg = log::get(log::Area::Shell))
        lg->info("ready -- {} widget(s) registered", 0);  // dump-registry shows the real count
}

}  // namespace kaflip
