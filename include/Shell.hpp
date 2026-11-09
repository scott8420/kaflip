#pragma once
#include "widgets/Widgets.hpp"
#include "core/Stones.hpp"   // s046: the undo stone's demo model + journal
#include "Reader.hpp"         // s002: the reading surface
#include "Scanner.hpp"        // s006: folder scans off the main thread
#include "core/Library.hpp"   // s006
#include "core/ReadingStyle.hpp"   // s007

#include <gtkmm/applicationwindow.h>
#include <giomm/menu.h>
#include <giomm/simpleaction.h>

#include <memory>
#include <sigc++/connection.h>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Shell -- the app window, AND the split-class exemplar.
//
// This class is split across multiple TUs by CATEGORY OF CODE, not by feature
// (CANON: "vocabulary first, file layout second" + "the header is the index").
// The split costs ~nothing while the parts are small; retrofitting it after a
// class grows to thousands of lines is a multi-session migration, so the
// structure is installed on day one. The header below IS the index -- every
// method carries a trailing `// category: ...` tag naming the file its body
// lives in. Read the tag, open that file.
//
// Vocabulary
// ----------
//   Zones    -- construction of named regions: the window frame, header,
//               sidebar+stack, the pattern pages, the menu model.
//               File: src/Shell_zones.cpp
//   Bindings -- what's wired to what: Gio actions, the recents action group,
//               accelerators.  File: src/Shell_bindings.cpp
//   Handlers -- slot bodies (on_*): what a user action actually does.
//               File: src/Shell_handlers.cpp
//   Helpers  -- reusable sync/orchestration called from zones/bindings/handlers.
//               File: src/Shell_helpers.cpp
//   Glue     -- what stays in Shell.cpp: the ctor + build_ui() (the ordered
//               construct -> name/register -> bind pass).
//
// Finding things
// --------------
//   "Where is the window built?"        -> Shell_zones.cpp    (build_shell)
//   "Where are actions registered?"     -> Shell_bindings.cpp (bind_actions)
//   "What does 'Dump registry' do?"     -> Shell_handlers.cpp (on_dump_registry)
//   "Where does the recents list sync?" -> Shell_helpers.cpp  (note_recent / rebuild_*)
//   "Where does Ctrl+Z go?"             -> Shell_bindings.cpp (the window's undo keys)
//                                          + Shell_handlers.cpp (on_undo / on_redo)
// ─────────────────────────────────────────────────────────────────────────────
namespace kaflip {

class AboutWindow;
class ShortcutsDialog;
class StyleDialog;

class Shell : public Gtk::ApplicationWindow {
public:
    Shell();
    ~Shell() override;

    void build_ui();                             // category: glue

    // s002 -- open an EPUB into the reader (menu, Ctrl+O, a recent, or the
    // command line / file manager via App::on_open). Errors go to a dialog.
    bool open_book(const std::string& path);     // category: helper: open + remember + repaint

private:
    // ── zones ──────────────────────────────────────────────────────────────
    void build_shell();                          // category: zone: window + sidebar + stack
    void build_pages();                          // category: zone: the pattern pages
    Glib::RefPtr<Gio::Menu> build_menu();        // category: zone: hamburger model
    void build_reader_page();                    // category: zone: the Reader page (s002)
    void build_rail();                           // category: zone: the page rail (s004, Trails' look)
    void build_hero();                           // category: zone: the Reader's empty state (s004)
    void build_library_page();                   // category: zone: the Library -- a grid of covers (s006)

    // ── bindings ───────────────────────────────────────────────────────────
    void bind_actions();                         // category: bindings: actions + recents group
    void bind_reader_input();                    // category: bindings: page keys + wheel on the Reader page (s003)

    // ── handlers ───────────────────────────────────────────────────────────
    void on_pick_folder();                       // category: handler: folder chooser -> remember it
    void on_open_book();                         // category: handler: EPUB file chooser -> open_book (s002)
    void on_chapter_next();                      // category: handler: next linear spine item (s002)
    void on_chapter_prev();                      // category: handler: previous linear spine item (s002)
    void on_page_next();                         // category: handler: next page, across chapters (s003)
    void on_page_prev();                         // category: handler: previous page, across chapters (s003)
    void on_focus_mode();                        // category: handler: F11 -- enter / leave focus mode (s005)
    void on_reader_tap(const std::string& zone); // category: handler: a tap zone in the page (s005)
    void on_add_folder();                        // category: handler: folder chooser -> background scan (s006)
    void on_reading_style();                     // category: handler: open the Reading style window (s007)
    void on_library_remove(const std::string& path);   // category: handler: one undo step (s006)
    void on_library_menu(Gtk::Widget& tile, const std::string& path, double x, double y);   // category: handler: right-click a cover (s006)
    void on_recent_open(const std::string& p);   // category: handler: open a recent (parameterised)
    void on_recent_clear();                      // category: handler: empty the recents list
    void on_dump_registry();                     // category: handler: print the live widget tree
    void on_about();                             // category: handler: open the About window (dialog-lifetime exemplar)
    void on_shortcuts();   // handler: open the keyboard reference (renders from core::shortcut_registry)
    // s046 -- the undo stone. Every verb on the Undo page goes through the
    // journal (core/Undo.hpp); nothing on it writes the model any other way.
    void on_undo();                              // category: handler: Ctrl+Z / menu / button
    void on_redo();                              // category: handler: Ctrl+Shift+Z, Ctrl+Y / menu / button
    void on_stone_add();                         // category: handler: a new stone below the selection
    void on_stone_delete();                      // category: handler: delete; the neighbour takes the selection
    void on_stone_up();                          // category: handler: swap with the one above
    void on_stone_down();                        // category: handler: swap with the one below
    void on_stone_rename();                      // category: handler: the name field, on Enter / leaving it
    void on_stone_size();                        // category: handler: the slider (keys merge; a drag is one step)

    // ── helpers ────────────────────────────────────────────────────────────
    void note_recent(const std::string& path);   // category: helper: push onto the list + persist
    void rebuild_recents_menu();                  // category: helper: repaint the submenu
    void rebuild_recents_page();                  // category: helper: repaint the live list page
    std::string recents_file() const;             // category: helper: the XDG path (UI-side resolution)
    void build_undo_page();                       // category: zone: the Undo page (s046)
    void queue_stones_repaint();                  // category: helper: one repaint per idle, after the writes
    void repaint_stones();                        // category: helper: the list + the detail fields from the model
    void select_stone(const std::string& id, bool keys);   // category: helper: select (+ keyboard) after a repaint
    void update_undo_ui();                        // category: helper: Undo / Redo say what they would do, grey when nothing
    bool focus_is_text() const;                   // category: helper: the keyboard is in a text box (its own Ctrl+Z)
    void update_reader_ui();                      // category: helper: title, "3 of 12", grey prev/next (s002)
    void select_tab(const std::string& id);       // category: helper: stack + rail + window tint (s004)
    void set_focus_mode(bool on);                 // category: helper: fullscreen + chrome away / back (s005)
    void show_hud(bool on);                       // category: helper: fade the focus HUD in / out (s005)
    void poke_hud();                              // category: helper: show it, and hide again after a pause (s005)
    std::string library_file() const;             // category: helper: the XDG path (s006)
    void queue_library_repaint();                 // category: helper: save + repaint once per idle (s006)
    void rebuild_library();                       // category: helper: the grid from the store (s006)
    bool stones_active() const;                   // category: helper: Ctrl+Z goes to the stones (Undo page) or the library (s006)
    void apply_style();                           // category: helper: the book sheet from the active style (s007)
    std::string style_file() const;               // category: helper: the XDG config path (s007)

    // Titlebar: brand logo + Open (start), hamburger (end).
    widgets::Button     m_logo_button;   // frameless brand mark; opens About
    widgets::Button     m_open_button;   // s002: Open an EPUB
    widgets::Button     m_focus_button;  // s005: enter focus mode
    widgets::Button     m_style_button;  // s007: Reading style
    Gtk::Widget*        m_header = nullptr;   // s005: hidden in focus mode (owned by the window)
    widgets::MenuButton m_menu_button;

    // Body: sidebar | stack (each pattern is a stack page).
    widgets::Box          m_body;      // horizontal
    widgets::ScrolledWindow m_rail_scroll;   // s004: the rail replaces the StackSidebar
    widgets::ListBox        m_rail;
    bool                    m_syncing = false;   // select_tab is moving the rail: not a click
    widgets::Stack        m_stack;

    // The recents "live page": a label + a scrolling list rebuilt on change.
    widgets::Box            m_recents_page;   // vertical
    widgets::ScrolledWindow m_recents_scroll;
    widgets::ListBox        m_recents_list;

    // Recents state (core data; UI holds it + resolves the path -- the seam).
    std::vector<std::string>        m_recents;
    std::string                     m_recents_file;
    Glib::RefPtr<Gio::Menu>         m_recents_menu;   // live submenu, rebuilt in place
    Glib::RefPtr<Gio::SimpleAction> m_recents_clear;  // held to toggle enabled state

    // s046 -- the undo stone. Declaration order matters: the store before the
    // journal that holds a reference to it.
    core::Stones       m_stones;
    core::StoneJournal m_journal{m_stones};
    std::string        m_sel;                  // the selected stone's id ("" = none)
    bool               m_painting = false;     // a repaint sets fields: not a user choice
    bool               m_repaint_queued = false;
    bool               m_keys_to_list = false; // after the repaint, give the list the keyboard
    widgets::Box       m_undo_page;
    widgets::ListBox   m_stone_list;
    widgets::Entry     m_stone_name;
    widgets::Scale     m_stone_size;
    widgets::Button    m_undo_button;
    widgets::Button    m_redo_button;
    widgets::Label     m_undo_said;            // "Undid: Delete “Jade”"
    Glib::RefPtr<Gio::Menu>         m_edit_menu;   // Undo / Redo items, re-labelled live
    Glib::RefPtr<Gio::SimpleAction> m_act_undo, m_act_redo;

    // s002 -- the Reader page: the web surface + a nav bar under it.
    widgets::Overlay m_reader_overlay;       // s005: the stack page -- the reader page + the focus HUD
    widgets::Box     m_reader_page;          // vertical
    widgets::Box     m_hero;                 // s004: shown instead of the book until one opens
    Reader           m_reader;
    widgets::Box     m_reader_nav;           // horizontal: prev | "3 of 12" | next
    widgets::Button  m_prev_button;
    widgets::Label   m_chapter_label;
    widgets::Button  m_next_button;
    Glib::RefPtr<Gio::SimpleAction> m_act_next, m_act_prev;
    Glib::RefPtr<Gio::SimpleAction> m_act_page_next, m_act_page_prev;   // s003
    Glib::RefPtr<Gio::SimpleAction> m_act_focus;                        // s005

    // s006 -- the library. Declaration order: the store before its journal.
    core::Library           m_library;
    core::LibraryJournal    m_lib_journal{m_library};
    std::string             m_library_file;
    bool                    m_lib_repaint_queued = false;
    std::vector<std::string> m_lib_paths;          // grid child index -> book path
    widgets::Box            m_library_page;        // vertical
    widgets::Label          m_lib_count;
    widgets::Label          m_lib_said;            // "Undid: Remove “Tapper”"
    widgets::Box            m_lib_busy;            // the "working on ..." row
    widgets::ProgressBar    m_lib_progress;
    widgets::Label          m_lib_status;
    widgets::ScrolledWindow m_lib_scroll;
    widgets::FlowBox        m_lib_grid;
    widgets::Box            m_lib_empty;
    Scanner                 m_scanner;

    // s007 -- reading style: two styles (reading, focus), their own journal,
    // the window that edits them. Store before journal (declaration order).
    core::Styles                 m_styles;
    core::StyleJournal           m_style_journal{m_styles};
    std::string                  m_style_file;
    bool                         m_style_queued = false;
    std::unique_ptr<StyleDialog> m_style_dialog;

    // s005 -- focus mode. A view state, like the page: not an undo step.
    bool              m_focus = false;
    widgets::Revealer m_hud_revealer;
    widgets::Box      m_hud;                 // prev | label | next | leave
    widgets::Label    m_hud_label;
    sigc::connection  m_hud_timer;
    double            m_last_x = -1, m_last_y = -1;   // motion that does not move is not motion
    double                          m_wheel = 0.0;                      // s003: smooth-scroll accumulator

    // The About window: a hide-on-close singleton, built lazily on first open.
    std::unique_ptr<AboutWindow>    m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;   // shortcut registry's GTK consumer (same lifetime stone)
};

}  // namespace kaflip
