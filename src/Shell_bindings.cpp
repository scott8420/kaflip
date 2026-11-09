#include "Shell.hpp"
#include "Log.hpp"
#include "core/Recents.hpp"

#include <giomm/simpleactiongroup.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <glibmm/variant.h>
#include <glibmm/main.h>
#include "StyleDialog.hpp"

#include <filesystem>

// Shell_bindings.cpp -- BINDINGS. What's wired to what: the win.* actions, the
// parameterised recents action group, and (where a keystroke must survive focus)
// accelerators. No slot bodies here -- those are handlers; this file only wires.

namespace kaflip {

void Shell::bind_actions() {  // bindings: actions + recents group
    // Plain win.* actions -> handler bodies (Shell_handlers.cpp).
    add_action("pick-folder",   sigc::mem_fun(*this, &Shell::on_pick_folder));
    // s002 -- the reader's verbs. next/prev are held so update_reader_ui can
    // grey them at the ends of the spine and when no book is open.
    add_action("open-book",     sigc::mem_fun(*this, &Shell::on_open_book));
    m_act_next = add_action("chapter-next", sigc::mem_fun(*this, &Shell::on_chapter_next));
    m_act_prev = add_action("chapter-prev", sigc::mem_fun(*this, &Shell::on_chapter_prev));
    m_act_page_next = add_action("page-next", sigc::mem_fun(*this, &Shell::on_page_next));   // s003
    m_act_page_prev = add_action("page-prev", sigc::mem_fun(*this, &Shell::on_page_prev));
    m_act_focus = add_action("focus-mode", sigc::mem_fun(*this, &Shell::on_focus_mode));   // s005
    add_action("add-folder", sigc::mem_fun(*this, &Shell::on_add_folder));                 // s006
    add_action("reading-style", sigc::mem_fun(*this, &Shell::on_reading_style));           // s007

    // s007 -- the styles' door; a change restyles the book live, is saved once
    // per idle, and repaints the style window if it is open.
    m_styles.set_door([this] { return m_style_journal.open(); });
    m_styles.set_on_change([this] {
        apply_style();
        if (m_style_queued) return;
        m_style_queued = true;
        Glib::signal_idle().connect_once([this] {
            m_style_queued = false;
            if (!m_styles.save(m_style_file))
                if (auto lg = log::get(log::Area::Book)) lg->warn("could not save {}", m_style_file);
            if (m_style_dialog) m_style_dialog->refresh();
        });
    });
    m_style_journal.set_on_change([this] { if (m_style_dialog) m_style_dialog->refresh(); });

    // s006 -- the library's door, change hook and journal (the stones' shape).
    m_library.set_door([this] { return m_lib_journal.open(); });
    m_library.set_on_change([this] { queue_library_repaint(); });
    m_lib_journal.set_on_change([this] { update_undo_ui(); });
    // s005: if the desktop takes us out of fullscreen (a WM key), leave focus mode too.
    property_fullscreened().signal_changed().connect([this] {
        if (m_focus && !is_fullscreen()) set_focus_mode(false);
    });
    bind_reader_input();
    add_action("dump-registry", sigc::mem_fun(*this, &Shell::on_dump_registry));
    add_action("about",         sigc::mem_fun(*this, &Shell::on_about));
    add_action("shortcuts",     sigc::mem_fun(*this, &Shell::on_shortcuts));
    add_action("quit",          [this]() { close(); });

    // Recents: a "recents" action group with a PARAMETERISED open verb --
    // recents.open(<path>) carries each entry's path as its target, so one
    // action serves every menu row (CANON/Curvz: the proven parameterised-action
    // shape). clear is held so rebuild_recents_menu can grey it when empty.
    auto group = Gio::SimpleActionGroup::create();
    group->add_action_with_parameter(
        "open", Glib::Variant<Glib::ustring>::variant_type(),
        [this](const Glib::VariantBase& param) {
            const std::string p =
                Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(param).get();
            on_recent_open(p);
        });
    m_recents_clear = Gio::SimpleAction::create("clear");
    m_recents_clear->signal_activate().connect(
        [this](const Glib::VariantBase&) { on_recent_clear(); });
    group->add_action(m_recents_clear);
    insert_action_group("recents", group);

    // Now that the group exists, load the persisted list (prunes stale folders).
    m_recents = core::load_recents(m_recents_file);

    // ── s046: the undo stone ────────────────────────────────────────────────
    // Undo / Redo are held so update_undo_ui can grey them; the stone verbs
    // are plain actions the page's buttons name.
    m_act_undo = add_action("undo", sigc::mem_fun(*this, &Shell::on_undo));
    m_act_redo = add_action("redo", sigc::mem_fun(*this, &Shell::on_redo));
    add_action("stone-add",    sigc::mem_fun(*this, &Shell::on_stone_add));
    add_action("stone-delete", sigc::mem_fun(*this, &Shell::on_stone_delete));
    add_action("stone-up",     sigc::mem_fun(*this, &Shell::on_stone_up));
    add_action("stone-down",   sigc::mem_fun(*this, &Shell::on_stone_down));

    // The model <-> journal <-> UI wiring. The store asks the journal whether
    // the door is open (the one-door guard); the journal reads the selection
    // and tells the UI when Undo / Redo change; the store's changes repaint.
    m_stones.set_door([this] { return m_journal.open(); });
    m_stones.set_on_change([this] { queue_stones_repaint(); });
    m_journal.set_selection_source([this] {
        return m_sel.empty() ? std::vector<std::string>{} : std::vector<std::string>{m_sel};
    });
    m_journal.set_on_change([this] { update_undo_ui(); });
    {
        core::Stones::Loading load(m_stones);   // fixture data: not a user choice
        for (const char* n : {"Ruby", "Jade", "Onyx", "Opal"}) {
            const auto id = m_stones.fresh_id();
            m_stones.put(id, core::Stone{n, 3, m_stones.pos_after({})});
        }
    }

    // ── Ctrl+Z from ANYWHERE but a text box ─────────────────────────────────
    // NOT an accelerator: a set_accels_for_action chord fires before the
    // focused widget, so it would take Ctrl+Z away from every Entry and
    // TextView (jot s016c met that with Ctrl+Delete). Instead the WINDOW
    // hears it in the BUBBLE phase -- only keys nothing else took -- and lets
    // a text box keep its own even when it had nothing to undo. Binding undo
    // to one widget's focus is the other trap: after a menu closes, or the
    // focused row is rebuilt away, the keyboard is nowhere and Ctrl+Z is dead
    // (jot s046a: "undo a delete" did nothing). The registry carries these as
    // doc-only rows for that reason.
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) {
            const auto mods = state & (Gdk::ModifierType::CONTROL_MASK |
                                       Gdk::ModifierType::SHIFT_MASK |
                                       Gdk::ModifierType::ALT_MASK);
            const auto CTRL  = Gdk::ModifierType::CONTROL_MASK;
            const auto SHIFT = Gdk::ModifierType::SHIFT_MASK;
            const char* act = nullptr;
            if ((keyval == GDK_KEY_z || keyval == GDK_KEY_Z) && mods == CTRL)               act = "win.undo";
            else if ((keyval == GDK_KEY_z || keyval == GDK_KEY_Z) && mods == (CTRL | SHIFT)) act = "win.redo";
            else if ((keyval == GDK_KEY_y || keyval == GDK_KEY_Y) && mods == CTRL)          act = "win.redo";
            if (!act || focus_is_text()) return false;
            if (auto lg = log::get(log::Area::Shell)) lg->info("window key -> {}", act);
            activate_action(act);   // the FULL name: "undo" alone would do nothing
            return true;
        }, false);
    add_controller(keys);
    update_undo_ui();
}

// ─────────────────────────────────────────────────────────────────────────────
// s003 -- page keys and the wheel, on the READER PAGE only.
//
// Not accelerators: Space, the arrows and Home/End belong to every text box
// (the Undo page's name field among them), and an accelerator would take them
// first. A CAPTURE-phase controller on the WINDOW sees them before the web
// view does, and acts only while the Reader page is showing and the keyboard
// is on that page -- or nowhere. "Nowhere" matters: a nav button greys for a
// moment while a chapter loads, GTK drops the focus off it, and a controller
// on the page itself would then never hear the next key (seen in s003).
// Ctrl+Page Up / Down stay accelerators (chapters) and never reach here.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::bind_reader_input() {  // bindings: page keys + wheel
    auto keys = Gtk::EventControllerKey::create();
    keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) {
            if (auto lg = log::get(log::Area::Shell))
                lg->debug("reader key {} (state {:#x})", keyval, static_cast<unsigned>(state));
            // s005: Escape leaves focus mode, wherever the keyboard is.
            if (m_focus && keyval == GDK_KEY_Escape) { set_focus_mode(false); return true; }
            if (!m_reader.has_book() || m_stack.get_visible_child_name() != "reader") return false;
            if (auto* f = get_focus(); f && f != &m_reader_overlay && !f->is_ancestor(m_reader_overlay))
                return false;
            const auto mods = state & (Gdk::ModifierType::CONTROL_MASK |
                                       Gdk::ModifierType::ALT_MASK |
                                       Gdk::ModifierType::SHIFT_MASK);
            const bool none  = mods == Gdk::ModifierType{};
            const bool shift = mods == Gdk::ModifierType::SHIFT_MASK;
            switch (keyval) {
                case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down:
                case GDK_KEY_Right: case GDK_KEY_Down:
                    if (!none) return false;
                    m_reader.turn_forward(); return true;
                case GDK_KEY_space:
                    if (none)  { m_reader.turn_forward(); return true; }
                    if (shift) { m_reader.turn_back();    return true; }
                    return false;
                case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up:
                case GDK_KEY_Left: case GDK_KEY_Up: case GDK_KEY_BackSpace:
                    if (!none) return false;
                    m_reader.turn_back(); return true;
                case GDK_KEY_Home: if (!none) return false; m_reader.first_page(); return true;
                case GDK_KEY_End:  if (!none) return false; m_reader.last_page();  return true;
                default: return false;
            }
        }, false);
    add_controller(keys);

    // The wheel turns pages. A mouse notch is 1.0 (WHEEL units); a touchpad
    // streams pixels (SURFACE units), so it turns once per ~80 px of travel.
    auto wheel = Gtk::EventControllerScroll::create();
    wheel->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    wheel->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    wheel->signal_scroll_begin().connect([this] { m_wheel = 0.0; });
    wheel->signal_scroll().connect(
        [this, w = wheel.get()](double, double dy) {
            if (!m_reader.has_book()) return false;
            const double step = w->get_unit() == Gdk::ScrollUnit::WHEEL ? 1.0 : 80.0;
            m_wheel += dy;
            if (m_wheel >= step)       { m_wheel = 0.0; m_reader.turn_forward(); }
            else if (m_wheel <= -step) { m_wheel = 0.0; m_reader.turn_back(); }
            return true;
        }, false);
    m_reader_page.add_controller(wheel);
}

}  // namespace kaflip
