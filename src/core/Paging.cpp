#include "core/Paging.hpp"

#include <charconv>

namespace kaflip::core {

Turn turn_forward(int page, int count, int next_chapter) {
    if (count <= 0) return {};
    if (page + 1 < count) return {Turn::Kind::Page, page + 1, Land::First};
    if (next_chapter >= 0) return {Turn::Kind::Chapter, next_chapter, Land::First};
    return {};
}

Turn turn_back(int page, int count, int prev_chapter) {
    if (count <= 0) return {};
    if (page > 0) return {Turn::Kind::Page, page - 1, Land::First};
    if (prev_chapter >= 0) return {Turn::Kind::Chapter, prev_chapter, Land::Last};
    return {};
}

std::string pagination_css(const PageMargins& m) {
    // The side margin: at least m.x, more when the window is wider than a
    // line should be. Column width + gap still add up to exactly 100vw.
    // A spread (s007) is two columns per viewport: each page gets half the
    // width, the line cap applies per page, and the gap between the two
    // pages is the same 2x -- so one turn is still exactly 100vw.
    const std::string half = m.spread ? "50vw" : "100vw";
    const std::string x  = "max(" + std::to_string(m.x) + "px, calc((" + half + " - " +
                           std::to_string(m.max_line) + "px) / 2))";
    const std::string x2 = "calc(2 * " + x + ")";
    const std::string y  = std::to_string(m.y) + "px";
    const std::string y2 = std::to_string(m.y * 2) + "px";
    // USER-level sheet: its !important beats the publisher's !important.
    return
        "html {"
        " height: 100vh !important; width: 100vw !important;"
        " margin: 0 !important; padding: 0 !important;"
        " overflow: hidden !important; }\n"
        "body {"
        " box-sizing: border-box !important;"
        " height: 100vh !important; max-height: none !important;"
        " width: auto !important; max-width: none !important;"
        " margin: 0 !important;"
        " padding: " + y + " " + x + " !important;"
        + (m.spread ? std::string(" column-count: 2 !important; column-width: auto !important;")
                    : " column-width: calc(100vw - " + x2 + ") !important;") +
        " column-gap: " + x2 + " !important;"
        " column-fill: auto !important;"
        " overflow: visible !important; }\n"
        "img, svg, video {"
        " max-width: 100% !important;"
        " max-height: calc(100vh - " + y2 + ") !important;"
        " object-fit: contain;"
        " break-inside: avoid; }\n";
}

std::optional<PageReport> parse_page_report(const std::string& s) {
    const auto slash = s.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= s.size()) return std::nullopt;
    PageReport r;
    const char* b = s.data();
    auto [p1, e1] = std::from_chars(b, b + slash, r.page);
    if (e1 != std::errc{} || p1 != b + slash) return std::nullopt;
    auto [p2, e2] = std::from_chars(b + slash + 1, b + s.size(), r.count);
    if (e2 != std::errc{} || p2 != b + s.size()) return std::nullopt;
    if (r.page < 0 || r.count <= 0 || r.page >= r.count) return std::nullopt;
    return r;
}

}  // namespace kaflip::core
