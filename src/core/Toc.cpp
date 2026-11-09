#include "core/Toc.hpp"
#include "core/Epub.hpp"   // resolve_href, percent_decode

#include <pugixml.hpp>

#include <cstring>

namespace kaflip::core {

namespace {
const char* local(const char* name) {
    const char* colon = std::strrchr(name, ':');
    return colon ? colon + 1 : name;
}
bool is(const pugi::xml_node& n, const char* want) {
    return n.type() == pugi::node_element && std::strcmp(local(n.name()), want) == 0;
}
pugi::xml_node child(const pugi::xml_node& parent, const char* want) {
    for (auto c : parent.children())
        if (is(c, want)) return c;
    return {};
}
std::string all_text(const pugi::xml_node& n) {
    std::string out;
    for (auto t : n.select_nodes(".//text()")) out += t.node().value();
    return collapse_ws(out);
}
bool has_token(const std::string& list, const char* tok) {
    return (" " + collapse_ws(list) + " ").find(std::string(" ") + tok + " ") != std::string::npos;
}
// The fragment of an href, decoded ("" when there is none).
std::string fragment_of(const std::string& href) {
    const auto hash = href.find('#');
    return hash == std::string::npos ? std::string{} : percent_decode(href.substr(hash + 1));
}
TocEntry entry(const std::string& title, const std::string& href, const std::string& base, int depth) {
    TocEntry e;
    e.title = title;
    e.depth = depth;
    if (!href.empty()) {
        e.path = resolve_href(base, href);
        if (!e.path.empty()) e.fragment = fragment_of(href);
    }
    return e;
}

// ── EPUB 3 nav ─────────────────────────────────────────────────────────────
bool is_toc_nav(const pugi::xml_node& nav) {
    for (auto a : nav.attributes()) {
        const char* name = a.name();
        if ((std::strcmp(local(name), "type") == 0 && std::strchr(name, ':')) ||
            std::strcmp(name, "role") == 0) {
            if (has_token(a.value(), "toc") || has_token(a.value(), "doc-toc")) return true;
        }
    }
    return false;
}
void find_navs(const pugi::xml_node& n, std::vector<pugi::xml_node>& out) {
    for (auto c : n.children()) {
        if (is(c, "nav")) out.push_back(c);
        else find_navs(c, out);
    }
}
pugi::xml_node first_ol(const pugi::xml_node& n) {
    for (auto c : n.children()) {
        if (is(c, "ol") || is(c, "ul")) return c;
        if (auto d = first_ol(c)) return d;
    }
    return {};
}
void walk_ol(const pugi::xml_node& ol, const std::string& base, int depth, std::vector<TocEntry>& out) {
    for (auto li : ol.children()) {
        if (!is(li, "li")) continue;
        pugi::xml_node label;
        for (auto c : li.children())
            if (is(c, "a") || is(c, "span")) { label = c; break; }
        if (label) {
            const std::string href = is(label, "a") ? label.attribute("href").value() : "";
            TocEntry e = entry(all_text(label), href, base, depth);
            if (!e.title.empty() || !e.path.empty()) out.push_back(std::move(e));
        }
        for (auto c : li.children())
            if (is(c, "ol") || is(c, "ul")) walk_ol(c, base, depth + 1, out);
    }
}

// ── EPUB 2 NCX ─────────────────────────────────────────────────────────────
void walk_points(const pugi::xml_node& parent, const std::string& base, int depth, std::vector<TocEntry>& out) {
    for (auto np : parent.children()) {
        if (!is(np, "navPoint")) continue;
        const std::string title = all_text(child(child(np, "navLabel"), "text"));
        const std::string src   = child(np, "content").attribute("src").value();
        TocEntry e = entry(title, src, base, depth);
        if (!e.title.empty() || !e.path.empty()) out.push_back(std::move(e));
        walk_points(np, base, depth + 1, out);
    }
}
}  // namespace

std::string collapse_ws(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { space = !out.empty(); continue; }
        if (space) { out += ' '; space = false; }
        out += c;
    }
    return out;
}

std::vector<TocEntry> parse_nav(const std::string& xhtml, const std::string& base) {
    std::vector<TocEntry> out;
    pugi::xml_document doc;
    if (!doc.load_buffer(xhtml.data(), xhtml.size())) return out;
    std::vector<pugi::xml_node> navs;
    find_navs(doc, navs);
    if (navs.empty()) return out;
    pugi::xml_node nav = navs.front();   // no epub:type at all: the first nav
    for (const auto& n : navs)
        if (is_toc_nav(n)) { nav = n; break; }
    if (const auto ol = first_ol(nav)) walk_ol(ol, base, 0, out);
    return out;
}

std::vector<TocEntry> parse_ncx(const std::string& xml, const std::string& base) {
    std::vector<TocEntry> out;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size())) return out;
    if (const auto map = child(child(doc, "ncx"), "navMap")) walk_points(map, base, 0, out);
    return out;
}

int toc_index_for(const std::vector<TocEntry>& toc, int chapter) {
    if (chapter < 0) return -1;
    for (std::size_t i = 0; i < toc.size(); ++i)
        if (toc[i].spine == chapter) return static_cast<int>(i);
    // Not named: the entry for the nearest chapter before it (the first entry
    // that points there -- the chapter, not its last subsection).
    int best = -1, best_spine = -1;
    for (std::size_t i = 0; i < toc.size(); ++i)
        if (toc[i].spine >= 0 && toc[i].spine < chapter && toc[i].spine > best_spine) {
            best = static_cast<int>(i);
            best_spine = toc[i].spine;
        }
    return best;
}

}  // namespace kaflip::core
