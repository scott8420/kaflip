#include "StyleDialog.hpp"
#include "Log.hpp"

#include <gtkmm/colordialog.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/fontdialog.h>
#include <gtkmm/grid.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stringlist.h>
#include <webkit/webkit.h>

#include <cmath>
#include <cstdio>

namespace kaflip {

namespace {
std::string hex_of(const Gdk::RGBA& c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x",
                  int(std::lround(c.get_red() * 255)), int(std::lround(c.get_green() * 255)),
                  int(std::lround(c.get_blue() * 255)));
    return buf;
}
Gdk::RGBA rgba_of(const std::string& hex) {
    Gdk::RGBA c;
    c.set(hex);
    return c;
}
// A family name from the font button; "Sans 11" -> "Sans".
std::string family_of(const Gtk::FontDialogButton& b) {
    const auto d = b.get_font_desc();
    return d.get_family().raw();
}

const char* kSample =
    "<h1>Chapter One</h1>"
    "<p>The river ran slow past the mill, and the miller's cat watched it as if it owed her "
    "money. Nobody in Linden could say when the wheel had last turned, only that it had, once, "
    "and that everyone had been surprised.</p>"
    "<p>She kept a ledger of such things. <i>Rain on a Tuesday.</i> A heron, unexpected. "
    "The <a href=\"#\">postman's bicycle</a>, which squeaked in a minor key and was therefore "
    "a poet.</p>"
    "<h2>A smaller heading</h2>"
    "<p>Hyphenation shows itself in long words: incomprehensibilities, counterrevolutionaries, "
    "and the characteristically unremarkable afternoon light.</p>";
}  // namespace

StyleDialog::StyleDialog(core::Styles& styles, core::StyleJournal& journal)
    : m_styles(styles), m_journal(journal),
      m_body_font(Gtk::FontDialog::create()), m_head_font(Gtk::FontDialog::create()),
      m_page(Gtk::ColorDialog::create()), m_text(Gtk::ColorDialog::create()),
      m_head(Gtk::ColorDialog::create()), m_link(Gtk::ColorDialog::create()) {
    set_name("style.dialog");
    set_title("Reading style");
    set_default_size(980, 660);
    set_hide_on_close(true);
    add_css_class("kaflip");        // the skin
    add_css_class("page-reader");   // the teal ground
    build();
    refresh();
}

StyleDialog::~StyleDialog() = default;

void StyleDialog::edit(const core::Styles::Id& id) {
    m_id = id;
    refresh();
}

template <class F>
void StyleDialog::change(const std::string& label, const std::string& merge, F&& f) {
    if (m_painting) return;
    core::ReadingStyle n = m_styles.at(m_id);
    f(n);
    n.preset = "Custom";
    core::styles::set(m_journal, m_styles, m_id, n, label, merge);
}

void StyleDialog::build() {
    // ── header: what is being edited, and its undo ──────────────────────────
    auto header = Gtk::make_managed<Gtk::HeaderBar>();
    auto which = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    which->add_css_class("linked");
    m_edit_reading.set_label("Reading");
    m_edit_focus.set_label("Focus mode");
    m_edit_focus.set_group(m_edit_reading);
    m_edit_reading.set_active(true);
    m_edit_reading.signal_toggled().connect([this] { if (m_edit_reading.get_active()) edit(core::Styles::kReading); });
    m_edit_focus.signal_toggled().connect([this] { if (m_edit_focus.get_active()) edit(core::Styles::kFocus); });
    which->append(m_edit_reading);
    which->append(m_edit_focus);
    header->set_title_widget(*which);
    m_undo.set_icon_name("edit-undo-symbolic");
    m_redo.set_icon_name("edit-redo-symbolic");
    m_undo.signal_clicked().connect(sigc::mem_fun(*this, &StyleDialog::on_undo));
    m_redo.signal_clicked().connect(sigc::mem_fun(*this, &StyleDialog::on_redo));
    header->pack_start(m_undo);
    header->pack_start(m_redo);
    set_titlebar(*header);

    // Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y in this window mean the style journal.
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType st) {
        const auto m = st & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK);
        const auto C = Gdk::ModifierType::CONTROL_MASK, S = Gdk::ModifierType::SHIFT_MASK;
        if ((kv == GDK_KEY_z || kv == GDK_KEY_Z) && m == C) { on_undo(); return true; }
        if (((kv == GDK_KEY_z || kv == GDK_KEY_Z) && m == (C | S)) || ((kv == GDK_KEY_y || kv == GDK_KEY_Y) && m == C)) { on_redo(); return true; }
        if (kv == GDK_KEY_Escape) { close(); return true; }
        return false;
    }, false);
    add_controller(keys);

    // ── body: controls | preview ────────────────────────────────────────────
    auto body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 18);
    body->add_css_class("page");

    auto left = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    auto presets = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    for (const auto& p : core::presets()) {
        auto b = Gtk::make_managed<Gtk::Button>(p.preset);
        b->add_css_class("preset");
        b->add_css_class("preset-" + p.preset);
        const std::string name = p.preset;
        b->signal_clicked().connect([this, name] {
            core::styles::apply_preset(m_journal, m_styles, m_id, name);
        });
        presets->append(*b);
    }
    left->append(*presets);

    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(10);
    grid->set_column_spacing(14);
    int row = 0;
    const auto add_row = [&](const char* label, Gtk::Widget& w) {
        auto l = Gtk::make_managed<Gtk::Label>(label);
        l->set_xalign(1.0f);
        l->add_css_class("dim-label");
        grid->attach(*l, 0, row);
        w.set_hexpand(true);
        grid->attach(w, 1, row);
        ++row;
    };
    const auto section = [&](const char* title) {
        auto l = Gtk::make_managed<Gtk::Label>(title);
        l->set_xalign(0.0f);
        l->add_css_class("section-title");
        l->set_margin_top(row ? 8 : 0);
        grid->attach(*l, 0, row, 2, 1);
        ++row;
    };

    // Override
    m_level.set_model(Gtk::StringList::create({"The book's", "My fonts", "Everything mine"}));
    m_level.property_selected().signal_changed().connect([this] {
        change("Override", "", [this](core::ReadingStyle& s) {
            s.level = static_cast<core::Override>(m_level.get_selected());
        });
    });

    // Fonts
    const auto font_row = [&](Gtk::FontDialogButton& fb, Gtk::CheckButton& book, bool head) {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        fb.set_level(Gtk::FontLevel::FAMILY);
        fb.set_use_font(true);
        fb.set_hexpand(true);
        book.set_label("The book's");
        box->append(fb);
        box->append(book);
        fb.property_font_desc().signal_changed().connect([this, &fb, &book, head] {
            if (m_painting || book.get_active()) return;
            change(head ? "Heading font" : "Body font", "", [&fb, head](core::ReadingStyle& s) {
                (head ? s.head_font : s.body_font) = family_of(fb);
            });
        });
        book.signal_toggled().connect([this, &fb, &book, head] {
            fb.set_sensitive(!book.get_active());
            change(head ? "Heading font" : "Body font", "", [&fb, &book, head](core::ReadingStyle& s) {
                (head ? s.head_font : s.body_font) = book.get_active() ? std::string{} : family_of(fb);
            });
        });
        return box;
    };

    const auto scale = [&](Gtk::Scale& sc, double lo, double hi, double step, int digits) {
        sc.set_range(lo, hi);
        sc.set_increments(step, step * 4);
        sc.set_digits(digits);
        sc.set_round_digits(digits);
        sc.set_draw_value(true);
        sc.set_value_pos(Gtk::PositionType::RIGHT);
    };
    scale(m_size, 60, 220, 5, 0);
    scale(m_line, 1.0, 2.4, 0.05, 2);
    scale(m_margin_x, 8, 200, 4, 0);
    scale(m_margin_y, 8, 160, 4, 0);
    scale(m_max_line, 320, 1600, 20, 0);
    m_size.signal_value_changed().connect([this] {
        change("Text size", "size", [this](core::ReadingStyle& s) { s.size_pct = int(std::lround(m_size.get_value())); });
    });
    m_line.signal_value_changed().connect([this] {
        change("Line height", "line", [this](core::ReadingStyle& s) { s.line_height = m_line.get_value(); });
    });
    m_margin_x.signal_value_changed().connect([this] {
        change("Side margin", "mx", [this](core::ReadingStyle& s) { s.margin_x = int(std::lround(m_margin_x.get_value())); });
    });
    m_margin_y.signal_value_changed().connect([this] {
        change("Top margin", "my", [this](core::ReadingStyle& s) { s.margin_y = int(std::lround(m_margin_y.get_value())); });
    });
    m_max_line.signal_value_changed().connect([this] {
        change("Line width", "ml", [this](core::ReadingStyle& s) { s.max_line = int(std::lround(m_max_line.get_value())); });
    });

    m_paras.set_model(Gtk::StringList::create({"The book's", "Indented", "Spaced"}));
    m_paras.property_selected().signal_changed().connect([this] {
        change("Paragraphs", "", [this](core::ReadingStyle& s) { s.paras = static_cast<core::Paras>(m_paras.get_selected()); });
    });
    m_justify.set_model(Gtk::StringList::create({"The book's", "Left", "Justified"}));
    m_justify.property_selected().signal_changed().connect([this] {
        change("Alignment", "", [this](core::ReadingStyle& s) { s.justify = static_cast<core::Justify>(m_justify.get_selected()); });
    });
    m_hyphens.set_halign(Gtk::Align::START);
    m_hyphens.property_active().signal_changed().connect([this] {
        change("Hyphenation", "", [this](core::ReadingStyle& s) { s.hyphens = m_hyphens.get_active(); });
    });
    m_spread.set_halign(Gtk::Align::START);
    m_spread.property_active().signal_changed().connect([this] {
        change("Two pages", "", [this](core::ReadingStyle& s) { s.spread = m_spread.get_active(); });
    });

    const auto colour = [&](Gtk::ColorDialogButton& cb, const char* label, std::string core::ReadingStyle::*field) {
        cb.set_halign(Gtk::Align::START);
        if (auto d = cb.get_dialog()) d->set_with_alpha(false);
        cb.property_rgba().signal_changed().connect([this, &cb, label, field] {
            change(label, "", [&cb, field](core::ReadingStyle& s) { s.*field = hex_of(cb.get_rgba()); });
        });
    };
    colour(m_page, "Page colour", &core::ReadingStyle::page);
    colour(m_text, "Text colour", &core::ReadingStyle::text);
    colour(m_head, "Heading colour", &core::ReadingStyle::head);
    colour(m_link, "Link colour", &core::ReadingStyle::link);
    auto colours = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    for (auto [b, l] : {std::pair{&m_page, "Page"}, std::pair{&m_text, "Text"},
                        std::pair{&m_head, "Headings"}, std::pair{&m_link, "Links"}}) {
        auto v = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        auto lab = Gtk::make_managed<Gtk::Label>(l);
        lab->add_css_class("dim-label");
        v->append(*b);
        v->append(*lab);
        colours->append(*v);
    }

    section("Text");
    add_row("Whose style", m_level);
    add_row("Body font", *font_row(m_body_font, m_body_book, false));
    add_row("Heading font", *font_row(m_head_font, m_head_book, true));
    add_row("Size %", m_size);
    add_row("Line height", m_line);
    add_row("Paragraphs", m_paras);
    add_row("Alignment", m_justify);
    add_row("Hyphenation", m_hyphens);
    section("Colours");
    add_row("", *colours);
    section("Page");
    add_row("Side margin", m_margin_x);
    add_row("Top margin", m_margin_y);
    add_row("Line width", m_max_line);
    add_row("Two pages", m_spread);

    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_child(*grid);
    scroll->set_vexpand(true);
    left->append(*scroll);
    m_said.set_xalign(0.0f);
    m_said.add_css_class("dim-label");
    left->append(m_said);
    left->set_size_request(470, -1);
    left->set_hexpand(false);   // the controls keep their width; the preview takes the rest
    body->append(*left);

    // The preview: a small WebKit view of sample text in the style.
    auto card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    card->add_css_class("book-card");
    card->set_overflow(Gtk::Overflow::HIDDEN);
    card->set_hexpand(true);
    card->set_vexpand(true);
    GtkWidget* w = webkit_web_view_new();
    m_preview = WEBKIT_WEB_VIEW(w);
    webkit_settings_set_enable_javascript(webkit_web_view_get_settings(m_preview), FALSE);
    Gtk::Widget* pv = Gtk::manage(Glib::wrap(w));
    pv->set_hexpand(true);
    pv->set_vexpand(true);
    pv->set_can_target(false);   // a picture of the style, not a page to click
    card->append(*pv);
    body->append(*card);

    set_child(*body);
}

void StyleDialog::refresh() {
    const core::ReadingStyle& s = m_styles.at(m_id);
    m_painting = true;
    m_edit_reading.set_active(m_id == core::Styles::kReading);
    m_edit_focus.set_active(m_id == core::Styles::kFocus);
    m_level.set_selected(static_cast<guint>(s.level));
    m_body_book.set_active(s.body_font.empty());
    m_body_font.set_sensitive(!s.body_font.empty());
    if (!s.body_font.empty()) m_body_font.set_font_desc(Pango::FontDescription(s.body_font));
    m_head_book.set_active(s.head_font.empty());
    m_head_font.set_sensitive(!s.head_font.empty());
    if (!s.head_font.empty()) m_head_font.set_font_desc(Pango::FontDescription(s.head_font));
    m_size.set_value(s.size_pct);
    m_line.set_value(s.line_height);
    m_paras.set_selected(static_cast<guint>(s.paras));
    m_justify.set_selected(static_cast<guint>(s.justify));
    m_hyphens.set_active(s.hyphens);
    m_page.set_rgba(rgba_of(s.page));
    m_text.set_rgba(rgba_of(s.text));
    m_head.set_rgba(rgba_of(s.head));
    m_link.set_rgba(rgba_of(s.link));
    m_margin_x.set_value(s.margin_x);
    m_margin_y.set_value(s.margin_y);
    m_max_line.set_value(s.max_line);
    m_spread.set_active(s.spread);
    m_painting = false;

    m_undo.set_sensitive(m_journal.can_undo());
    m_redo.set_sensitive(m_journal.can_redo());
    m_undo.set_tooltip_text(m_journal.can_undo() ? "Undo " + m_journal.undo_label() + "  (Ctrl+Z)" : "Nothing to undo");
    m_redo.set_tooltip_text(m_journal.can_redo() ? "Redo " + m_journal.redo_label() + "  (Ctrl+Shift+Z)" : "Nothing to redo");
    set_title(std::string("Reading style — ") + (m_id == core::Styles::kFocus ? "focus mode" : "reading") +
              (s.preset.empty() ? "" : "  ·  " + s.preset));
    update_preview();
}

void StyleDialog::update_preview() {
    if (!m_preview) return;
    const core::ReadingStyle& s = m_styles.at(m_id);
    // Not paginated: the style alone, with the margins as padding.
    const std::string html =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>" + core::style_css(s) +
        "body { margin: 0 !important; padding: " + std::to_string(s.margin_y / 2) + "px " +
        std::to_string(s.margin_x / 2) + "px !important; max-width: " + std::to_string(s.max_line) +
        "px; } h1 { margin-top: 0; }</style></head><body>" + kSample + "</body></html>";
    GdkRGBA rgba;
    if (gdk_rgba_parse(&rgba, s.page.c_str())) webkit_web_view_set_background_color(m_preview, &rgba);
    webkit_web_view_load_html(m_preview, html.c_str(), "about:blank");
}

void StyleDialog::on_undo() {
    const auto r = m_journal.undo();
    if (r.done) m_said.set_text("Undid: " + r.label);
}

void StyleDialog::on_redo() {
    const auto r = m_journal.redo();
    if (r.done) m_said.set_text("Redid: " + r.label);
}

}  // namespace kaflip
