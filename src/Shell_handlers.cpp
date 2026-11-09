#include "Shell.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "Registry.hpp"
#include "core/Recents.hpp"

#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/popover.h>
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

void Shell::on_library_remove(const std::string& path) {  // handler: one undo step
    if (core::library::remove_book(m_lib_journal, m_library, path)) {
        m_lib_said.set_text("Removed from the library  (Ctrl+Z puts it back; the file is untouched)");
        m_lib_said.set_visible(true);
    }
}

// Right-click a cover: a one-item menu. Built per click and parented to the
// tile; unparented once it closes (a popover must not outlive its parent).
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
// s046 -- the undo stone's handlers. Each verb is ONE call into core::stones
// (which goes through the journal) and then says where the selection goes.
// None of them writes m_stones directly: the selftest's gate would not see a
// handler that did, so the rule is kept here by hand -- handlers call verbs.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_undo() {  // handler: take the last step back
    if (!stones_active()) {   // s006: the library's journal, everywhere but the Undo page
        const auto r = m_lib_journal.undo();
        if (!r.done) return;
        m_lib_said.set_text("Undid: " + r.label);
        m_lib_said.set_visible(true);
        if (auto lg = log::get(log::Area::Library)) lg->info("undo: {}", r.label);
        return;
    }
    const auto r = m_journal.undo();
    if (!r.done) return;
    m_sel = r.selection.empty() ? std::string{} : r.selection.front();
    m_undo_said.set_text("Undid: " + r.label);
    select_stone(m_sel, true);
    if (auto lg = log::get(log::Area::Shell)) lg->info("undo: {}", r.label);
}

void Shell::on_redo() {  // handler: put the undone step back
    if (!stones_active()) {   // s006
        const auto r = m_lib_journal.redo();
        if (!r.done) return;
        m_lib_said.set_text("Redid: " + r.label);
        m_lib_said.set_visible(true);
        if (auto lg = log::get(log::Area::Library)) lg->info("redo: {}", r.label);
        return;
    }
    const auto r = m_journal.redo();
    if (!r.done) return;
    m_sel = r.selection.empty() ? std::string{} : r.selection.front();
    m_undo_said.set_text("Redid: " + r.label);
    select_stone(m_sel, true);
    if (auto lg = log::get(log::Area::Shell)) lg->info("redo: {}", r.label);
}

void Shell::on_stone_add() {  // handler: a new stone just below the selection
    const auto id = core::stones::add(m_journal, m_stones, "Stone", m_sel);
    m_journal.reselect({id});
    m_sel = id;
    select_stone(id, false);
    m_stone_name.grab_focus();   // name it next -- the field keeps its own Ctrl+Z
}

void Shell::on_stone_delete() {  // handler: delete; the neighbour takes the selection
    if (m_sel.empty()) return;
    const auto next = core::stones::neighbour(m_stones, m_sel);
    if (!core::stones::remove(m_journal, m_stones, m_sel)) return;
    m_sel = next;
    m_journal.reselect(next.empty() ? std::vector<std::string>{} : std::vector<std::string>{next});
    select_stone(next, true);   // the KEYBOARD goes with it: the next Ctrl+Z lands
}

void Shell::on_stone_up() {  // handler
    if (!m_sel.empty() && core::stones::move_up(m_journal, m_stones, m_sel)) select_stone(m_sel, true);
}

void Shell::on_stone_down() {  // handler
    if (!m_sel.empty() && core::stones::move_down(m_journal, m_stones, m_sel)) select_stone(m_sel, true);
}

void Shell::on_stone_rename() {  // handler: Enter in the name field, or leaving it
    if (m_painting || m_sel.empty()) return;
    const auto v = m_stones.get(m_sel);
    const std::string name = m_stone_name.get_text();
    if (v && v->name != name) core::stones::rename(m_journal, m_stones, m_sel, name);
}

void Shell::on_stone_size() {  // handler: the slider moved
    if (m_painting || m_sel.empty()) return;
    const int n = static_cast<int>(m_stone_size.get_value() + 0.5);
    // Mid-drag the gesture is open and this is just a write inside it; by
    // keys or the wheel it is a step that merges with the presses before it.
    if (m_journal.in_gesture()) core::stones::drag_to(m_journal, m_stones, m_sel, n);
    else                        core::stones::resize(m_journal, m_stones, m_sel, n);
}

}  // namespace kaflip
