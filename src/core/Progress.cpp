#include "core/Progress.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace kaflip::core {

namespace {
const char* local(const char* name) {
    const char* colon = std::strrchr(name, ':');
    return colon ? colon + 1 : name;
}
bool skipped(const pugi::xml_node& el) {
    const char* n = local(el.name());
    return !strcasecmp(n, "script") || !strcasecmp(n, "style") || !strcasecmp(n, "head") ||
           !strcasecmp(n, "title");
}
pugi::xml_node find_body(const pugi::xml_node& n) {
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        if (!strcasecmp(local(c.name()), "body")) return c;
        if (auto b = find_body(c)) return b;
    }
    return {};
}
// UTF-8 bytes -> UTF-16 units: every non-continuation byte is one unit, a
// 4-byte sequence (outside the BMP) is two.
int utf16_units(const char* s) {
    int n = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
        if ((*p & 0xC0) == 0x80) continue;
        n += (*p >= 0xF0) ? 2 : 1;
    }
    return n;
}
bool blank(const char* s) {
    for (; *s; ++s)
        if (!(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f')) return false;
    return true;
}
int count(const pugi::xml_node& n) {
    int total = 0;
    for (auto c : n.children()) {
        if (c.type() == pugi::node_pcdata || c.type() == pugi::node_cdata) {
            if (!blank(c.value())) total += utf16_units(c.value());
        } else if (c.type() == pugi::node_element && !skipped(c)) {
            total += count(c);
        }
    }
    return total;
}
}  // namespace

int text_length(const std::string& xhtml) {
    pugi::xml_document doc;
    if (!doc.load_buffer(xhtml.data(), xhtml.size(), pugi::parse_default | pugi::parse_ws_pcdata))
        return 0;
    const auto body = find_body(doc);
    return count(body ? body : pugi::xml_node(doc));
}

double book_progress(const std::vector<int>& lengths, const std::vector<bool>& linear,
                     int chapter, int through) {
    long long total = 0, before = 0;
    const int n = static_cast<int>(std::min(lengths.size(), linear.size()));
    for (int i = 0; i < n; ++i) {
        if (!linear[static_cast<std::size_t>(i)]) continue;
        const int len = std::max(0, lengths[static_cast<std::size_t>(i)]);
        total += len;
        if (i < chapter) before += len;
        else if (i == chapter) before += std::clamp(through, 0, len);
    }
    if (total <= 0) return 0.0;
    return std::clamp(double(before) / double(total), 0.0, 1.0);
}

std::string percent_text(double progress) {
    const int pct = static_cast<int>(std::floor(std::clamp(progress, 0.0, 1.0) * 100.0 + 1e-9));
    return std::to_string(pct) + "%";
}

}  // namespace kaflip::core
