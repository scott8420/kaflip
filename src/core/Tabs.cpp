#include "core/Tabs.hpp"

namespace kaflip::core {

const std::vector<TabSpec>& tab_registry() {
    // Order = rail order. (s010: the Cairn demo pages, Registry and Undo, are
    // gone; the registry dump stays under Diagnostics, Ctrl+D.)
    static const std::vector<TabSpec> kTabs = {
        {"library",  "Library",  "kaflip-tab-library-symbolic",  "tab-library"},   // s006
        {"reader",   "Reader",   "kaflip-tab-reader-symbolic",   "tab-reader"},
        {"recents",  "Recent",   "kaflip-tab-recent-symbolic",   "tab-recent"},
    };
    return kTabs;
}

const TabSpec* find_tab(std::string_view id) {
    for (const auto& t : tab_registry())
        if (t.id == id) return &t;
    return nullptr;
}

}  // namespace kaflip::core
