#pragma once
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core::tabs -- the ONE list of Ka-flip's rail pages (s004, ported from Trails).
// The rail rows, the stack pages, the per-page colour classes and the window
// tint all read it, so adding a page is one row here (plus its glyph file).
// GTK-free so the selftest checks it: ids unique, every page has a glyph and a
// colour class.
// ─────────────────────────────────────────────────────────────────────────────
namespace kaflip::core {

struct TabSpec {
    std::string_view id;      // stack page name: "reader"
    std::string_view title;   // rail label: "Reader"
    std::string_view icon;    // bundled symbolic glyph: "kaflip-tab-reader-symbolic"
    std::string_view css;     // colour class: "tab-reader" (kaflip.css); the window gets "page-reader"
};

const std::vector<TabSpec>& tab_registry();
const TabSpec* find_tab(std::string_view id);

}  // namespace kaflip::core
