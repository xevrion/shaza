#pragma once

#include <cstdint>

#include "rk/surface.hpp"

namespace shaza {

struct Rgb {
    uint8_t r, g, b;
};

constexpr uint16_t pack(Rgb c) { return rk::rgb565(c.r, c.g, c.b); }
Rgb unpack(uint16_t p);
// a is 0..255; the result is dst moved toward src by a.
uint16_t blend(uint16_t dst, Rgb src, int a);
Rgb scale(Rgb c, float k);
Rgb mix(Rgb a, Rgb b, float t);

// Everything below draws only inside this rectangle while it is set. Null clears it.
void set_clip(const rk::Rect* r);

// Anti-aliased shapes, blended over whatever is already on the surface. Every shape visits only
// the pixels it can touch: on each row it works out where its band starts and ends instead of
// testing every pixel of its bounding box. alpha is 0..1 on top of the shape's own coverage.
void ring(rk::Surface& sf, float cx, float cy, float r, float width, Rgb c, float alpha = 1.0f);
// Angles in radians, clockwise from twelve o'clock, a1 > a0. Ends are round.
void arc(rk::Surface& sf, float cx, float cy, float r, float width, float a0, float a1, Rgb c, float alpha = 1.0f);
// A filled disc of radius r with a soft glow that fades to nothing at r + glow.
void glow_disc(rk::Surface& sf, float cx, float cy, float r, float glow, Rgb c, float alpha = 1.0f, float glow_strength = 0.5f);
// A line from (x0, y0) to (x1, y1) with round caps.
void line(rk::Surface& sf, float x0, float y0, float x1, float y1, float width, Rgb c, float alpha = 1.0f);
// One byte of coverage per pixel, top-left at (x, y).
void mask(rk::Surface& sf, int x, int y, int w, int h, const uint8_t* cov, Rgb c, float alpha = 1.0f);
// An RGB565 picture faded in by alpha over black, with corners rounded to radius corner. stride is
// the source's row length in pixels when only part of a bigger picture is drawn; 0 means w.
void image(rk::Surface& sf, int x, int y, int w, int h, const uint16_t* px, float alpha = 1.0f, float corner = 0.0f, int stride = 0);
// Fills with black, within the clip.
void clear(rk::Surface& sf, const rk::Rect& r);

// The same shapes the slow way, every pixel of the bounding box, for the tests.
namespace reference {
void ring(rk::Surface& sf, float cx, float cy, float r, float width, Rgb c, float alpha = 1.0f);
void arc(rk::Surface& sf, float cx, float cy, float r, float width, float a0, float a1, Rgb c, float alpha = 1.0f);
void glow_disc(rk::Surface& sf, float cx, float cy, float r, float glow, Rgb c, float alpha = 1.0f, float glow_strength = 0.5f);
}  // namespace reference

// Easing, t in 0..1.
float ease_out(float t);
float ease_in_out(float t);
float clamp01(float t);

}  // namespace shaza
