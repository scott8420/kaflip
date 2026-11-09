#pragma once
// core::ReadingStyle -- how a book looks (s007). GTK-free; the selftest pins
// the CSS it makes, its JSON, and that every change is an undo step.
//
// A style is fonts + colours + page geometry + an OVERRIDE LEVEL:
//   Publisher  -- the book's fonts and colours win; ours are defaults only.
//   Fonts      -- our fonts, size, spacing, paragraphs and alignment win.
//   Everything -- that, plus our colours on every element.
// The page ground and the base text colour are always ours (it is our page),
// and so are size and geometry (the reader's controls).
//
// Two styles are kept: "reading" (the window) and "focus" (focus mode, by
// default the Night preset at Everything). book_css() turns one into the ONE
// user stylesheet the Reader injects -- columns, margins, typography, colour.
#include "core/Paging.hpp"
#include "core/Undo.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kaflip::core {

enum class Override { Publisher, Fonts, Everything };
enum class Paras    { Book, Indent, Spaced };
enum class Justify  { Book, Left, Full };

struct ReadingStyle {
    std::string preset = "Day";      // the preset it started from (a label only)
    Override    level  = Override::Fonts;
    // fonts
    std::string body_font;           // "" = the book's
    std::string head_font;           // "" = the book's
    int         size_pct    = 100;   // 60..220
    double      line_height = 1.5;   // 1.0..2.4
    Paras       paras   = Paras::Book;
    Justify     justify = Justify::Book;
    bool        hyphens = false;
    // colours (#rrggbb)
    std::string page = "#ffffff";
    std::string text = "#1f2328";
    std::string head = "#0e7490";
    std::string link = "#0e7490";
    // page
    int  margin_x = 48;              // px, the least side margin
    int  margin_y = 40;              // px
    int  max_line = 760;             // px, the widest line (per page in a spread)
    bool spread   = false;           // two pages side by side
    bool operator==(const ReadingStyle&) const = default;
};

// Day, Sepia, Night, Paper -- in that order.
const std::vector<ReadingStyle>& presets();
std::optional<ReadingStyle> find_preset(const std::string& name);

// Clamp numbers into range, drop unsafe font names and bad colours (a style
// read from disk or typed by hand can never break the stylesheet).
ReadingStyle sanitized(ReadingStyle s);

// The typography + colour part, and the whole sheet (geometry included).
std::string style_css(const ReadingStyle& s);
std::string book_css(const ReadingStyle& s);

// s008a -- focus mode follows the Reading style (Scott: "the focus mode
// doesn't use the custom settings"): everything comes from `reading` except
// the colours, which are focus's own; the override level is the stronger of
// the two, so a dark focus palette still reaches every element.
ReadingStyle focus_effective(const ReadingStyle& reading, const ReadingStyle& focus);

// JSON for one style (unknown or missing fields keep their defaults).
std::string style_to_json(const ReadingStyle& s);
std::optional<ReadingStyle> style_from_json(const std::string& text);

// ── the store: two records, "reading" and "focus" ──────────────────────────
class Styles {
public:
    using Id    = std::string;
    using Value = ReadingStyle;
    static inline const Id kReading = "reading";
    static inline const Id kFocus   = "focus";

    Styles();   // defaults: reading = Day, focus = Night

    std::optional<ReadingStyle> get(const Id& id) const;
    void put(const Id& id, const std::optional<ReadingStyle>& v);
    const ReadingStyle& at(const Id& id) const;   // a missing id reads as Day

    std::string to_json() const;
    bool        from_json(const std::string& text);
    bool        save(const std::string& file) const;
    bool        load(const std::string& file);   // missing file: defaults

    void set_door(std::function<bool()> open) { m_door = std::move(open); }
    int  strays() const { return m_strays; }
    struct Loading {
        explicit Loading(Styles& s) : m_s(s) { ++m_s.m_loading; }
        ~Loading() { --m_s.m_loading; }
        Styles& m_s;
    };
    void set_on_change(std::function<void()> f) { m_changed = std::move(f); }

private:
    std::map<Id, ReadingStyle> m_items;
    int                        m_strays = 0;
    int                        m_loading = 0;
    std::function<bool()>      m_door;
    std::function<void()>      m_changed;
};

using StyleJournal = Journal<Styles>;

namespace styles {
// One step: the style becomes `next` (sanitized). `merge_key` folds a slider's
// stream of values into one step (Journal's merge window). False: no change.
bool set(StyleJournal& j, Styles& s, const Styles::Id& id, const ReadingStyle& next,
         const std::string& label, const std::string& merge_key = {});
// One step: a named preset, whole.
bool apply_preset(StyleJournal& j, Styles& s, const Styles::Id& id, const std::string& name);

struct Verb {
    std::string name;
    std::function<void(StyleJournal&, Styles&)> run;
};
const std::vector<Verb>& verbs();   // the gate's table
}  // namespace styles

}  // namespace kaflip::core
