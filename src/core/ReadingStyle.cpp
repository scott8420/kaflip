#include "core/ReadingStyle.hpp"

#include "json.hpp"   // vendored nlohmann/json (core-private)

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kaflip::core {

namespace fs = std::filesystem;
using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Presets
// ─────────────────────────────────────────────────────────────────────────────
const std::vector<ReadingStyle>& presets() {
    static const std::vector<ReadingStyle> kPresets = [] {
        ReadingStyle day;   // the defaults are Day
        ReadingStyle sepia = day;
        sepia.preset = "Sepia";
        sepia.page = "#f4ecd8"; sepia.text = "#5b4636"; sepia.head = "#7a4a1e"; sepia.link = "#9a5b13";
        ReadingStyle night = day;
        night.preset = "Night";
        night.level = Override::Everything;   // a dark page needs its colours everywhere
        night.page = "#0f1719"; night.text = "#d7e3e5"; night.head = "#f1f7f8"; night.link = "#5eead4";
        ReadingStyle paper = day;
        paper.preset = "Paper";
        paper.page = "#fbfaf6"; paper.text = "#2b2b2b"; paper.head = "#1a1a1a"; paper.link = "#0e7490";
        paper.body_font = "serif";
        paper.justify = Justify::Full;
        paper.hyphens = true;
        paper.paras = Paras::Indent;
        return std::vector<ReadingStyle>{day, sepia, night, paper};
    }();
    return kPresets;
}

std::optional<ReadingStyle> find_preset(const std::string& name) {
    for (const auto& p : presets())
        if (p.preset == name) return p;
    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sanitising
// ─────────────────────────────────────────────────────────────────────────────
namespace {
bool is_hex_colour(const std::string& c) {
    if (c.size() != 7 || c[0] != '#') return false;
    return std::all_of(c.begin() + 1, c.end(), [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)); });
}
std::string safe_font(const std::string& f) {
    // A family name: letters, digits, spaces, '-', '_' and '.' only. Anything
    // else (quotes, ';', braces) could escape the CSS -- refuse it.
    if (f.size() > 80) return {};
    for (char c : f)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || c == '.'))
            return {};
    return f;
}
std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}
std::string family(const std::string& f) {
    if (f == "serif" || f == "sans-serif" || f == "monospace") return f;
    return "\"" + f + "\"";
}
}  // namespace

ReadingStyle sanitized(ReadingStyle s) {
    const ReadingStyle d;
    s.size_pct    = std::clamp(s.size_pct, 60, 220);
    s.line_height = std::clamp(s.line_height, 1.0, 2.4);
    s.margin_x    = std::clamp(s.margin_x, 8, 200);
    s.margin_y    = std::clamp(s.margin_y, 8, 160);
    s.max_line    = std::clamp(s.max_line, 320, 1600);
    s.body_font   = safe_font(s.body_font);
    s.head_font   = safe_font(s.head_font);
    if (!is_hex_colour(s.page)) s.page = d.page;
    if (!is_hex_colour(s.text)) s.text = d.text;
    if (!is_hex_colour(s.head)) s.head = d.head;
    if (!is_hex_colour(s.link)) s.link = d.link;
    if (s.preset.size() > 40) s.preset.resize(40);
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// CSS
// ─────────────────────────────────────────────────────────────────────────────
std::string style_css(const ReadingStyle& in) {
    const ReadingStyle s = sanitized(in);
    const std::string F = s.level == Override::Publisher ? "" : " !important";    // fonts strength
    const std::string A = s.level == Override::Everything ? " !important" : "";   // colour strength
    std::string css;

    // The page is ours: ground and base text, always. Size is the reader's.
    css += "html { background: " + s.page + " !important; font-size: " +
           std::to_string(s.size_pct) + "% !important; }\n";
    css += "body { background: " + s.page + " !important; color: " + s.text + " !important;"
           " line-height: " + num(s.line_height) + F + ";";
    if (!s.body_font.empty()) css += " font-family: " + family(s.body_font) + F + ";";
    css += " }\n";

    // Fonts level and up: the book's own element rules give way.
    if (s.level != Override::Publisher) {
        css += "body p, body li, body div, body span, body blockquote, body td, body dd {"
               " line-height: " + num(s.line_height) + " !important;";
        if (!s.body_font.empty()) css += " font-family: " + family(s.body_font) + " !important;";
        css += " }\n";
    }
    if (!s.head_font.empty())
        css += "h1, h2, h3, h4, h5, h6 { font-family: " + family(s.head_font) + F + "; }\n";

    switch (s.paras) {
        case Paras::Book: break;
        case Paras::Indent:
            css += "body p { margin-top: 0" + F + "; margin-bottom: 0" + F + "; text-indent: 1.5em" + F + "; }\n"
                   "body h1 + p, body h2 + p, body h3 + p, body h4 + p, body hr + p,"
                   " body p:first-child { text-indent: 0" + F + "; }\n";
            break;
        case Paras::Spaced:
            css += "body p { margin-top: 0" + F + "; margin-bottom: 0.9em" + F + "; text-indent: 0" + F + "; }\n";
            break;
    }
    switch (s.justify) {
        case Justify::Book: break;
        case Justify::Left: css += "body, body p { text-align: left" + F + "; }\n"; break;
        case Justify::Full: css += "body, body p { text-align: justify" + F + "; }\n"; break;
    }
    if (s.hyphens)
        css += "body, body p { -webkit-hyphens: auto" + F + "; hyphens: auto" + F + "; }\n";

    // Colours: headings and links always get ours as a default; Everything
    // clears the book's grounds and text colours on every element too.
    if (s.level == Override::Everything)
        css += "body * { background-color: transparent !important; color: inherit !important; }\n";
    css += "h1, h2, h3, h4, h5, h6 { color: " + s.head + A + "; }\n";
    css += "a, a * { color: " + s.link + A + "; }\n";
    css += "::selection { background: " + s.link + "55 !important; }\n";
    return css;
}

std::string book_css(const ReadingStyle& in) {
    const ReadingStyle s = sanitized(in);
    return pagination_css(PageMargins{s.margin_x, s.margin_y, s.max_line, s.spread}) + style_css(s);
}

ReadingStyle focus_effective(const ReadingStyle& reading, const ReadingStyle& focus) {
    ReadingStyle e = reading;
    e.page = focus.page;
    e.text = focus.text;
    e.head = focus.head;
    e.link = focus.link;
    e.level = std::max(reading.level, focus.level);
    e.preset = focus.preset;
    return sanitized(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON
// ─────────────────────────────────────────────────────────────────────────────
namespace {
json to_j(const ReadingStyle& s) {
    return json{{"preset", s.preset},
                {"level", s.level == Override::Publisher ? "publisher" : s.level == Override::Fonts ? "fonts" : "everything"},
                {"body_font", s.body_font}, {"head_font", s.head_font},
                {"size_pct", s.size_pct}, {"line_height", s.line_height},
                {"paras", s.paras == Paras::Book ? "book" : s.paras == Paras::Indent ? "indent" : "spaced"},
                {"justify", s.justify == Justify::Book ? "book" : s.justify == Justify::Left ? "left" : "full"},
                {"hyphens", s.hyphens},
                {"page", s.page}, {"text", s.text}, {"head", s.head}, {"link", s.link},
                {"margin_x", s.margin_x}, {"margin_y", s.margin_y}, {"max_line", s.max_line},
                {"spread", s.spread}};
}
ReadingStyle from_j(const json& j) {
    ReadingStyle s;
    if (!j.is_object()) return s;
    s.preset    = j.value("preset", s.preset);
    const std::string lv = j.value("level", std::string("fonts"));
    s.level     = lv == "publisher" ? Override::Publisher : lv == "everything" ? Override::Everything : Override::Fonts;
    s.body_font = j.value("body_font", s.body_font);
    s.head_font = j.value("head_font", s.head_font);
    s.size_pct  = j.value("size_pct", s.size_pct);
    s.line_height = j.value("line_height", s.line_height);
    const std::string pa = j.value("paras", std::string("book"));
    s.paras     = pa == "indent" ? Paras::Indent : pa == "spaced" ? Paras::Spaced : Paras::Book;
    const std::string ju = j.value("justify", std::string("book"));
    s.justify   = ju == "left" ? Justify::Left : ju == "full" ? Justify::Full : Justify::Book;
    s.hyphens   = j.value("hyphens", s.hyphens);
    s.page = j.value("page", s.page); s.text = j.value("text", s.text);
    s.head = j.value("head", s.head); s.link = j.value("link", s.link);
    s.margin_x = j.value("margin_x", s.margin_x);
    s.margin_y = j.value("margin_y", s.margin_y);
    s.max_line = j.value("max_line", s.max_line);
    s.spread   = j.value("spread", s.spread);
    return sanitized(s);
}
}  // namespace

std::string style_to_json(const ReadingStyle& s) { return to_j(s).dump(1); }

std::optional<ReadingStyle> style_from_json(const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    return from_j(j);
}

// ─────────────────────────────────────────────────────────────────────────────
// The store
// ─────────────────────────────────────────────────────────────────────────────
Styles::Styles() {
    m_items[kReading] = *find_preset("Day");
    m_items[kFocus]   = *find_preset("Night");
}

std::optional<ReadingStyle> Styles::get(const Id& id) const {
    auto it = m_items.find(id);
    if (it == m_items.end()) return std::nullopt;
    return it->second;
}

void Styles::put(const Id& id, const std::optional<ReadingStyle>& v) {
    if (m_loading == 0 && m_door && !m_door()) ++m_strays;   // the gate
    if (v) m_items[id] = *v;
    else   m_items.erase(id);
    if (m_changed) m_changed();
}

const ReadingStyle& Styles::at(const Id& id) const {
    static const ReadingStyle kDay;
    auto it = m_items.find(id);
    return it == m_items.end() ? kDay : it->second;
}

std::string Styles::to_json() const {
    json doc = json::object();
    doc["version"] = 1;
    for (const auto& [id, s] : m_items) doc[id] = to_j(s);
    return doc.dump(1);
}

bool Styles::from_json(const std::string& text) {
    json doc = json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) return false;
    Loading load(*this);
    for (const Id& id : {kReading, kFocus})
        if (doc.contains(id)) put(id, from_j(doc[id]));
    for (auto it = doc.begin(); it != doc.end(); ++it)   // s010: the books' own
        if (it.key().rfind("book:", 0) == 0 && it.key().size() > 5 && it.value().is_object())
            put(it.key(), from_j(it.value()));
    return true;
}

bool Styles::save(const std::string& file) const {
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    const std::string tmp = file + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << to_json();
        if (!out) return false;
    }
    fs::rename(tmp, file, ec);
    return !ec;
}

bool Styles::load(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return true;   // no file yet: the defaults
    std::ostringstream ss;
    ss << in.rdbuf();
    return from_json(ss.str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Verbs
// ─────────────────────────────────────────────────────────────────────────────
namespace styles {

bool set(StyleJournal& j, Styles& s, const Styles::Id& id, const ReadingStyle& next,
         const std::string& label, const std::string& merge_key) {
    const ReadingStyle clean = sanitized(next);
    if (s.get(id) && *s.get(id) == clean) return false;
    return j.run(label, {id}, [&]() { s.put(id, clean); }, merge_key.empty() ? std::string{} : id + ":" + merge_key);
}

bool apply_preset(StyleJournal& j, Styles& s, const Styles::Id& id, const std::string& name) {
    auto p = find_preset(name);
    if (!p) return false;
    return set(j, s, id, *p, "Style “" + name + "”");
}

bool own_for_book(StyleJournal& j, Styles& s, const std::string& path) {
    if (path.empty() || s.has_own(path)) return false;
    const Styles::Id id = Styles::book_id(path);
    const ReadingStyle copy = s.at(Styles::kReading);
    return j.run("This book keeps its own style", {id}, [&]() { s.put(id, copy); });
}

bool follow_mine(StyleJournal& j, Styles& s, const std::string& path) {
    if (!s.has_own(path)) return false;
    const Styles::Id id = Styles::book_id(path);
    return j.run("This book follows your style", {id}, [&]() { s.put(id, std::nullopt); });
}

const std::vector<Verb>& verbs() {
    static const std::vector<Verb> kVerbs = {
        {"apply preset", [](StyleJournal& j, Styles& s) { apply_preset(j, s, Styles::kReading, "Sepia"); }},
        {"set a field", [](StyleJournal& j, Styles& s) {
             ReadingStyle n = s.at(Styles::kFocus);
             n.size_pct = 130;
             set(j, s, Styles::kFocus, n, "Text size");
         }},
        {"own style for a book", [](StyleJournal& j, Styles& s) { own_for_book(j, s, "/gate/book.epub"); }},
        {"follow my style", [](StyleJournal& j, Styles& s) { follow_mine(j, s, "/gate/book2.epub"); },
         [](Styles& s) {
             Styles::Loading l(s);
             s.put(Styles::book_id("/gate/book2.epub"), s.at(Styles::kReading));
         }},
    };
    return kVerbs;
}

}  // namespace styles

}  // namespace kaflip::core
