#pragma once

#include <string>

#include "paint.hpp"
#include "reply.hpp"

namespace shaza {

// Inter, baked by tools/make_font.py. Small is ASCII for the screen's own words; Body and Title
// cover every Latin, Greek and Cyrillic letter Inter has, for song titles. Characters outside
// that (Chinese, Devanagari...) are skipped.
enum class Size { Small, Body, Title };

int text_width(const char* utf8, Size size);
// One line centred on cx, with the baseline at y.
void text(rk::Surface& sf, int cx, int y, const char* utf8, Size size, Rgb c, float alpha = 1.0f);

// The text wrapped to at most max_lines lines of max_w pixels, each line centred, ending in an
// ellipsis when it does not fit, as a coverage mask. Straight quotes become curly ones.
Mask layout(const std::string& utf8, Size size, int max_w, int max_lines);
// How many characters of utf8 the font can draw.
int drawable(const std::string& utf8, Size size);

}  // namespace shaza
