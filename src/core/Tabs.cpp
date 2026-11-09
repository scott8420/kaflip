#include "core/Tabs.hpp"

namespace kaflip::core {

const std::vector<TabSpec>& tab_registry() {
    // Order = rail order. Registry and Undo are the Cairn demo pages; they go
    // when Ka-flip's own pages (Library, Contents) arrive.
    static const std::vector<TabSpec> kTabs = {
        {"library",  "Library",  "kaflip-tab-library-symbolic",  "tab-library"},   // s006
        {"reader",   "Reader",   "kaflip-tab-reader-symbolic",   "tab-reader"},
        {"recents",  "Recent",   "kaflip-tab-recent-symbolic",   "tab-recent"},
        {"registry", "Registry", "kaflip-tab-registry-symbolic", "tab-registry"},
        {"undo",     "Undo",     "kaflip-tab-undo-symbolic",     "tab-undo"},
    };
    return kTabs;
}

const TabSpec* find_tab(std::string_view id) {
    for (const auto& t : tab_registry())
        if (t.id == id) return &t;
    return nullptr;
}

}  // namespace kaflip::core
