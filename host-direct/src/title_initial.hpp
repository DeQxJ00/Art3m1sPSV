#pragma once
#include <cstdint>
#include <string>

namespace direct {
// Common and Extension-A Han characters, including traditional forms.
// Han-only or mixed Han titles prefer Chinese readings. Kana uses romaji.
// Polyphonic characters use their dictionary's default reading.
inline char title_initial(const std::string& title) {
    static constexpr char han[] =
#include "title_initial_data.inc"
    ;
    size_t pos = 0;
    while (pos < title.size()) {
        const auto first = static_cast<unsigned char>(title[pos++]);
        uint32_t cp = first;
        unsigned extra = 0;
        if (first >= 0xc2 && first <= 0xdf) { cp &= 0x1f; extra = 1; }
        else if (first >= 0xe0 && first <= 0xef) { cp &= 0x0f; extra = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { cp &= 7; extra = 3; }
        else if (first >= 0x80) return '#';
        if (pos + extra > title.size()) return '#';
        for (unsigned n = 0; n < extra; ++n) {
            const auto byte = static_cast<unsigned char>(title[pos++]);
            if ((byte & 0xc0) != 0x80) return '#';
            cp = (cp << 6) | (byte & 0x3f);
        }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800)
            || (extra == 3 && cp < 0x10000) || cp > 0x10ffff
            || (cp >= 0xd800 && cp <= 0xdfff)) return '#';
        // Ignore leading spacing, BOM, quotes and title brackets.
        if (cp <= 0x20 || cp == 0xfeff || cp == 0x3000 || cp == '"' || cp == 39
            || cp == '(' || cp == '[' || cp == '{' || cp == 0x300a || cp == 0x3008
            || cp == 0x300c || cp == 0x300e || cp == 0x3010 || cp == 0xff08) continue;
        if (cp >= 'a' && cp <= 'z') return char(cp - 'a' + 'A');
        if (cp >= 'A' && cp <= 'Z') return char(cp);
        // Katakana and hiragana share the same base syllable ordering.
        if (cp >= 0x30a1 && cp <= 0x30f6) cp -= 0x60;
        if (cp >= 0x3041 && cp <= 0x3096) {
            static constexpr char kana[] =
                "AAIIUUEEOO" "KGKGKGKGKG" "SZSZSZSZSZ" "TDCJTTZTDTD"
                "NNNNN" "HBPHBPFBPHBPHBP" "MMMMM" "YYYYYY" "RRRRR" "WWIEONVKK";
            static_assert(sizeof(kana) == 87);
            return kana[cp - 0x3041];
        }
        if (cp >= 0x3400 && cp < 0xa000) return han[cp - 0x3400];
        return '#';
    }
    return '#';
}
inline std::string title_with_initial(const std::string& title) {
    return std::string(1, title_initial(title)) + u8" · " + title;
}
} // namespace direct
