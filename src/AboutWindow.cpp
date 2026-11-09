#include "AboutWindow.hpp"
#include "widgets/Widgets.hpp"
#include "Registry.hpp"

#include <giomm/resource.h>
#include <glibmm/markup.h>
#include <gtkmm/box.h>
#include <gtkmm/enums.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stack.h>
#include <gtkmm/stackswitcher.h>
#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <string>

#ifndef KAFLIP_VERSION
#define KAFLIP_VERSION "dev"
#endif

namespace kaflip {

namespace {

std::string esc(const std::string& s) { return Glib::Markup::escape_text(s); }

Gtk::Label* text(const std::string& markup, bool dim = false, bool centre = false) {
    auto* l = Gtk::make_managed<Gtk::Label>();
    l->set_markup(markup);
    l->set_wrap(true);
    l->set_xalign(centre ? 0.5f : 0.0f);
    if (centre) l->set_justify(Gtk::Justification::CENTER);
    l->set_max_width_chars(46);
    if (dim) l->add_css_class("dim-label");
    return l;
}

Gtk::Label* heading(const char* t) {
    auto* l = Gtk::make_managed<Gtk::Label>(t);
    l->set_xalign(0.0f);
    l->add_css_class("section-title");
    l->set_margin_top(10);
    return l;
}

// One credit: the name in bold, then who and the licence, dimmed.
Gtk::Box* credit(const std::string& name, const std::string& detail) {
    auto* b = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 1);
    b->append(*text("<b>" + esc(name) + "</b>"));
    b->append(*text(esc(detail), true));
    return b;
}

std::string license_text() {
    try {
        auto bytes = Gio::Resource::lookup_data_global("/io/github/scott8420/Kaflip/LICENSE");
        gsize n = 0;
        const char* p = static_cast<const char*>(bytes->get_data(n));
        // Reflow: a single newline joins lines, a blank line keeps the paragraph.
        std::string in(p, n), out;
        for (std::size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '\n' && i + 1 < in.size() && in[i + 1] != '\n' && !out.empty() && out.back() != '\n')
                out += ' ';
            else
                out += in[i];
        }
        return out;
    } catch (const Glib::Error&) {
        return "MIT License — see the LICENSE file beside the source.";
    }
}

Gtk::ScrolledWindow* scrolled(Gtk::Widget& child) {
    auto* sw = Gtk::make_managed<Gtk::ScrolledWindow>();
    sw->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    sw->set_child(child);
    sw->set_vexpand(true);
    return sw;
}

}  // namespace

AboutWindow::AboutWindow(Gtk::Window& parent) {
    set_name("shell.about");
    registry::add("shell.about", this);

    set_title("About Ka-flip");
    set_modal(true);
    set_resizable(false);
    set_default_size(440, 500);
    set_transient_for(parent);
    set_hide_on_close(true);   // singleton: X hides, never destructs mid-session

    auto* stack = Gtk::make_managed<Gtk::Stack>();
    stack->set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    stack->set_vexpand(true);

    // Header: the tabs, as the window's title (s010).
    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(true);
    auto* tabs = Gtk::make_managed<Gtk::StackSwitcher>();
    tabs->set_stack(*stack);
    hb->set_title_widget(*tabs);
    set_titlebar(*hb);

    // ── About ──────────────────────────────────────────────────────────────
    auto* about = Gtk::make_managed<widgets::Box>(
        widgets::unregistered, "shell.about.page", Gtk::Orientation::VERTICAL);
    about->set_spacing(10);
    about->set_margin(24);
    about->set_halign(Gtk::Align::CENTER);
    // Hero logo: symbolic icon from the gresource bundle, recoloured by GTK.
    auto* logo = Gtk::make_managed<widgets::Image>(widgets::unregistered, "shell.about.logo");
    logo->set_from_icon_name("kaflip-hero-logo");
    logo->set_pixel_size(88);
    about->append(*logo);
    auto* name = Gtk::make_managed<widgets::Label>(widgets::unregistered, "shell.about.name");
    name->set_markup("<span size='xx-large' weight='bold'>Ka-flip</span>");
    about->append(*name);
    about->append(*text("Version " KAFLIP_VERSION, true, true));
    about->append(*text("An EPUB reader for GNOME.", false, true));
    auto* note = text(
        "Reads EPUB 2 and EPUB 3 books: a library of covers, pages that "
        "turn, a reading style of your own (or one per book), focus mode, "
        "contents and bookmarks — and it keeps your place.", true, true);
    note->set_margin_top(6);
    about->append(*note);
    about->append(*text("<small>© 2026 Scott · MIT License</small>", true, true));
    stack->add(*about, "about", "About");

    // ── Credits ────────────────────────────────────────────────────────────
    auto* cr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    cr->set_margin(24);
    cr->append(*heading("Made by"));
    cr->append(*credit("Scott", "Design, direction and code"));
    cr->append(*credit("Claude (Anthropic)", "Pair programming, session by session"));
    cr->append(*heading("Built on"));
    cr->append(*credit("GTK " + std::to_string(gtk_get_major_version()) + "." +
                       std::to_string(gtk_get_minor_version()) + " and gtkmm",
                       "The GTK and gtkmm teams · LGPL 2.1 or later"));
    cr->append(*credit("WebKitGTK " + std::to_string(webkit_get_major_version()) + "." +
                       std::to_string(webkit_get_minor_version()),
                       "The WebKit project · LGPL 2.1 and BSD — draws every page"));
    cr->append(*credit("pugixml", "Arseny Kapoulkine · MIT — reads the OPF, the nav and the NCX"));
    cr->append(*credit("JSON for Modern C++", "Niels Lohmann · MIT — the library, places and styles on disk"));
    cr->append(*credit("spdlog", "Gabi Melman and contributors · MIT — the diagnostic log"));
    cr->append(*credit("zlib", "Jean-loup Gailly and Mark Adler · zlib licence — opens the EPUB"));
    cr->append(*heading("With thanks"));
    cr->append(*text("To the IDPF and W3C for EPUB, and to every author and "
                     "publisher whose books end up in here.", true));
    stack->add(*scrolled(*cr), "credits", "Credits");

    // ── License ────────────────────────────────────────────────────────────
    auto* lic = Gtk::make_managed<Gtk::Label>(license_text());
    lic->set_wrap(true);
    lic->set_xalign(0.0f);
    lic->set_yalign(0.0f);
    lic->set_selectable(true);
    lic->set_margin(24);
    lic->set_max_width_chars(52);
    lic->add_css_class("dim-label");
    stack->add(*scrolled(*lic), "license", "License");

    // ── the window ─────────────────────────────────────────────────────────
    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    page->append(*stack);
    auto* close_btn = Gtk::make_managed<widgets::Button>(
        widgets::unregistered, "shell.about.close", "Close");
    close_btn->add_css_class("suggested-action");
    close_btn->set_halign(Gtk::Align::CENTER);
    close_btn->set_margin(12);
    close_btn->signal_clicked().connect([this]() { close(); });
    page->append(*close_btn);
    set_child(*page);
}

AboutWindow::~AboutWindow() {
    registry::remove(this);
}

}  // namespace kaflip
