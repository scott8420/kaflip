#pragma once
// core::Paging -- the page-turn rules (s003). GTK-free and WebKit-free.
//
// A chapter is laid out as CSS columns one viewport wide (pagination_css), and
// a small script in the page (resources/js/paginate.js) scrolls whole columns
// and reports "page/count" back. Everything that DECIDES lives here, where the
// selftest can reach it:
//
//   * what a turn does -- another page in this chapter, or a hand-off to the
//     next / previous linear chapter, landing on its first / last page;
//   * the stylesheet that makes the columns;
//   * parsing the page's report.
#include <optional>
#include <string>

namespace kaflip::core {

enum class Land { First, Last };

struct Turn {
    enum class Kind { None, Page, Chapter };
    Kind kind   = Kind::None;
    int  target = -1;            // page index (Page) or spine index (Chapter)
    Land land   = Land::First;   // where a Chapter turn lands
    bool operator==(const Turn&) const = default;
};

// page/count: where we are in the chapter. count <= 0 means the page has not
// reported yet (still loading) -- a turn then does nothing, so a double press
// during a load cannot skip a chapter.
// next_chapter/prev_chapter: the neighbouring linear spine index, or -1.
Turn turn_forward(int page, int count, int next_chapter);
Turn turn_back(int page, int count, int prev_chapter);

// x/y: the least side / top margin. max_line (s005): the widest a line of
// text may run; on a wide window (focus mode, fullscreen) the side margins
// grow to keep it, so a page never becomes one 1800 px line.
// spread (s007): two pages side by side -- still one viewport per turn.
struct PageMargins { int x = 48; int y = 40; int max_line = 760; bool spread = false; };   // CSS px
// The user stylesheet: the root fills the viewport and does not scroll; the
// body is cut into columns exactly one viewport apart, so page n is at
// scrollLeft = n * innerWidth. Images are held inside one page.
std::string pagination_css(const PageMargins& m);

// s008: paginate.js also says where the page is in the chapter's text --
// "3/11@1200,1585,1230\tIt was a dark..." : the first character on the
// page, the first on the next (the chapter's length on the last page), the
// MARK (the character the reader is held to through relayouts: where they
// turned to, or where a bookmark / the saved position sent them), and the
// page's first words. Old "3/11" still parses (the place fields stay -1).
struct PageReport {
    int page = 0; int count = 0;
    int start = -1, end = -1, mark = -1;
    std::string snippet;
};
// "3/11" -> {3, 11}. Rejects junk, negatives, a zero count and page >= count.
std::optional<PageReport> parse_page_report(const std::string& s);
// Just the "3/11" part (s008: parse_page_report's first half).
std::optional<PageReport> parse_page_counts(const std::string& s);

}  // namespace kaflip::core
