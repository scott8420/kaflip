#include "core/Proof.hpp"

#include <cstdint>

namespace kaflip::core {

namespace {
// Next code point from UTF-8 (a bad byte reads as U+FFFD and advances one).
char32_t next_cp(std::string_view s, size_t& i) {
    const auto b = static_cast<unsigned char>(s[i]);
    int n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
    if (n == 0 || i + n > s.size()) { ++i; return 0xFFFD; }
    char32_t c = n == 1 ? b : n == 2 ? (b & 0x1F) : n == 3 ? (b & 0x0F) : (b & 0x07);
    for (int k = 1; k < n; ++k) {
        const auto cb = static_cast<unsigned char>(s[i + k]);
        if ((cb >> 6) != 0x2) { ++i; return 0xFFFD; }
        c = (c << 6) | (cb & 0x3F);
    }
    i += n;
    return c;
}
bool is_space(char32_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xA0 || c == 0x2009 || c == 0x200A || c == 0x202F; }
bool is_ornament(char32_t c) {
    switch (c) {
        case '*': case '#': case '~': case '+': case 0x2042 /* ⁂ */: case 0x2217 /* ∗ */:
        case 0x2731 /* ✱ */: case 0x2767 /* ❧ */: case 0x2766 /* ❦ */: case 0x2022 /* • */:
        case 0x00B7 /* · */: case 0x00A7 /* § */: case 0x2734 /* ✴ */: case 0x2736 /* ✶ */:
            return true;
        default: return false;
    }
}
bool is_rule(char32_t c) { return c == '-' || c == '_' || c == '=' || c == 0x2013 || c == 0x2014; }
}  // namespace

bool is_scene_break(std::string_view s) {
    int ornaments = 0, rules = 0, length = 0;
    for (size_t i = 0; i < s.size();) {
        const char32_t c = next_cp(s, i);
        if (is_space(c)) continue;
        if (++length > 24) return false;
        if (is_ornament(c)) ++ornaments;
        else if (is_rule(c)) ++rules;
        else if (c == '.') continue;
        else return false;
    }
    return ornaments > 0 || rules >= 3;
}

}  // namespace kaflip::core
