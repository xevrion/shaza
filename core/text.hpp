#pragma once

#include "paint.hpp"

namespace shaza {

// The screen's own words ("No match", "2m ago") in Inter, baked by tools/make_font.py.
enum class Size { Small, Body };

int text_width(const char* s, Size size);
// Centred on cx, with the baseline at y.
void text(rk::Surface& sf, int cx, int y, const char* s, Size size, Rgb c, float alpha = 1.0f);

}  // namespace shaza
