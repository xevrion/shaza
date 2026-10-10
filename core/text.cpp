#include "text.hpp"

#include <cstring>

namespace shaza {

namespace {

struct Glyph {
    int code, advance4, x0, y0, w, h, offset;  // advance in quarter pixels; y0 is above the baseline when negative
};

struct Face {
    int px, ascent, descent, count;
    const Glyph* glyphs;
    const uint8_t* bits;
};

#include "ui_font.inc"

const Face& face(Size s) { return s == Size::Small ? k_small : k_body; }

const Glyph* find(const Face& f, int code) {
    for (int i = 0; i < f.count; ++i)
        if (f.glyphs[i].code == code) return &f.glyphs[i];
    return nullptr;
}

// Plain ASCII, plus the middle dot (U+00B7) as two bytes of UTF-8. Anything else is skipped.
int next_code(const char*& s) {
    const unsigned char c = static_cast<unsigned char>(*s++);
    if (c == 0xC2 && static_cast<unsigned char>(*s) == 0xB7) {
        ++s;
        return 0xB7;
    }
    return c < 0x80 ? c : -1;
}

}  // namespace

int text_width(const char* s, Size size) {
    const Face& f = face(size);
    int w4 = 0;
    while (*s) {
        const Glyph* g = find(f, next_code(s));
        if (g) w4 += g->advance4;
    }
    return (w4 + 2) / 4;
}

void text(rk::Surface& sf, int cx, int y, const char* s, Size size, Rgb c, float alpha) {
    const Face& f = face(size);
    int x4 = (cx - text_width(s, size) / 2) * 4;
    while (*s) {
        const Glyph* g = find(f, next_code(s));
        if (!g) continue;
        if (g->w && g->h) mask(sf, x4 / 4 + g->x0, y + g->y0, g->w, g->h, f.bits + g->offset, c, alpha);
        x4 += g->advance4;
    }
}

}  // namespace shaza
