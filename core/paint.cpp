#include "paint.hpp"

#include <algorithm>
#include <cmath>

namespace shaza {

constexpr float kTau = 6.28318530718f;

Rgb unpack(uint16_t p) {
    const int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)), uint8_t((b << 3) | (b >> 2))};
}

uint16_t blend(uint16_t dst, Rgb s, int a) {
    if (a <= 0) return dst;
    if (a >= 255) return pack(s);
    const Rgb d = unpack(dst);
    auto mixc = [a](int x, int y) { return uint8_t(x + ((y - x) * a + 127) / 255); };
    return pack({mixc(d.r, s.r), mixc(d.g, s.g), mixc(d.b, s.b)});
}

Rgb scale(Rgb c, float k) {
    k = std::max(0.0f, k);
    auto f = [k](int v) { return uint8_t(std::min(255.0f, v * k + 0.5f)); };
    return {f(c.r), f(c.g), f(c.b)};
}

Rgb mix(Rgb a, Rgb b, float t) {
    auto f = [t](int x, int y) { return uint8_t(std::lround(x + (y - x) * t)); };
    return {f(a.r, b.r), f(a.g, b.g), f(a.b, b.b)};
}

float clamp01(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
float ease_out(float t) {
    t = clamp01(t);
    const float u = 1 - t;
    return 1 - u * u * u;
}
float ease_in_out(float t) {
    t = clamp01(t);
    return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
}

// Row scratch: shapes build a span here, blended against the surface, then write it back once.
static uint16_t g_row[1024];
static bool g_clipping = false;
static rk::Rect g_clip;

void set_clip(const rk::Rect* r) {
    g_clipping = r != nullptr;
    if (r) g_clip = *r;
}

// Narrows [x0, x1] on row y to the surface and the clip; false when nothing is left.
static bool clip_span(const rk::Surface& sf, int y, int& x0, int& x1) {
    x0 = std::max(x0, 0), x1 = std::min(x1, sf.width() - 1);
    if (g_clipping) {
        if (y < g_clip.y0 || y > g_clip.y1) return false;
        x0 = std::max(x0, g_clip.x0), x1 = std::min(x1, g_clip.x1);
    }
    return y >= 0 && y < sf.height() && x0 <= x1;
}

// cov[0] belongs to pixel start, whatever clipping later trims off either end.
static void blend_span(rk::Surface& sf, int y, int start, int end, Rgb c, const float* cov) {
    int x0 = start, x1 = end;
    if (!clip_span(sf, y, x0, x1)) return;
    const uint16_t* src = sf.row_at(y);
    bool any = false;
    for (int x = x0; x <= x1; ++x) {
        const int a = int(cov[x - start] * 255.0f + 0.5f);
        g_row[x - x0] = blend(src[x], c, a);
        any |= a > 0;
    }
    if (any) sf.write_row(x0, y, g_row, x1 - x0 + 1);
}

void clear(rk::Surface& sf, const rk::Rect& r) {
    static const uint16_t black[1024] = {};
    for (int y = r.y0; y <= r.y1; ++y) {
        int x0 = r.x0, x1 = r.x1;
        if (clip_span(sf, y, x0, x1)) sf.write_row(x0, y, black, x1 - x0 + 1);
    }
}

static float ring_cov(float d, float r, float half) {
    return clamp01(r + half - d + 0.5f) - clamp01(r - half - d + 0.5f);
}

// Distance from the pixel to an arc's angular range, in pixels along the circle, 0 inside it.
static float arc_gap(float px, float py, float r, float a0, float a1) {
    float a = std::fmod(std::atan2(px, -py) - a0, kTau);
    if (a < 0) a += kTau;
    a += a0;
    if (a > a1) {
        const float past = a - a1, before = a0 + kTau - a;
        return std::min(past, before) * r;
    }
    return 0;
}

template <typename Cov>
static void band_rows(rk::Surface& sf, float cx, float cy, float r_in, float r_out, Rgb c, Cov cov_at) {
    static float cov[1024];
    const int y0 = int(std::floor(cy - r_out)), y1 = int(std::ceil(cy + r_out));
    for (int y = std::max(y0, 0); y <= std::min(y1, sf.height() - 1); ++y) {
        const float dy = y + 0.5f - cy;
        if (std::fabs(dy) > r_out) continue;
        const float xo = std::sqrt(r_out * r_out - dy * dy);
        const float xi = std::fabs(dy) < r_in ? std::sqrt(r_in * r_in - dy * dy) : 0.0f;
        // Left piece, then right; one span through the middle when the hole is closed on this row.
        int spans[2][2];
        int n = 0;
        const int l0 = int(std::floor(cx - xo)) - 1, r1 = int(std::ceil(cx + xo)) + 1;
        const int l1 = int(std::ceil(cx - xi)) + 1, r0 = int(std::floor(cx + xi)) - 1;
        // Overlapping pieces would blend the shared pixels twice.
        if (r0 <= l1) {
            spans[n][0] = l0, spans[n][1] = r1, ++n;
        } else {
            spans[n][0] = l0, spans[n][1] = l1, ++n;
            spans[n][0] = r0, spans[n][1] = r1, ++n;
        }
        for (int s = 0; s < n; ++s) {
            const int a = std::max(spans[s][0], 0), b = std::min(spans[s][1], sf.width() - 1);
            for (int x = a; x <= b; ++x) cov[x - a] = cov_at(x + 0.5f - cx, dy);
            blend_span(sf, y, a, b, c, cov);
        }
    }
}

void ring(rk::Surface& sf, float cx, float cy, float r, float width, Rgb c, float alpha) {
    const float half = width / 2;
    band_rows(sf, cx, cy, std::max(0.0f, r - half - 1.5f), r + half + 1.5f, c,
              [&](float dx, float dy) { return alpha * ring_cov(std::sqrt(dx * dx + dy * dy), r, half); });
}

void arc(rk::Surface& sf, float cx, float cy, float r, float width, float a0, float a1, Rgb c, float alpha) {
    const float half = width / 2;
    if (a1 - a0 >= kTau) return ring(sf, cx, cy, r, width, c, alpha);
    band_rows(sf, cx, cy, std::max(0.0f, r - half - 1.5f), r + half + 1.5f, c, [&](float dx, float dy) {
        const float d = std::sqrt(dx * dx + dy * dy);
        const float radial = std::fabs(d - r);
        if (radial > half + 1.0f) return 0.0f;
        const float gap = arc_gap(dx, dy, r, a0, a1);
        // Round caps: past either end, the distance to the end point decides.
        const float dist = std::sqrt(radial * radial + gap * gap);
        return alpha * clamp01(half - dist + 0.5f);
    });
}

static float glow_cov(float d, float r, float glow, float strength) {
    const float core = clamp01(r - d + 0.5f);
    if (glow <= 0) return core;
    const float t = clamp01(1.0f - (d - r) / glow);
    return std::min(1.0f, core + strength * t * t * t);
}

void glow_disc(rk::Surface& sf, float cx, float cy, float r, float glow, Rgb c, float alpha, float strength) {
    band_rows(sf, cx, cy, 0.0f, r + glow + 1.0f, c,
              [&](float dx, float dy) { return alpha * glow_cov(std::sqrt(dx * dx + dy * dy), r, glow, strength); });
}

void line(rk::Surface& sf, float x0, float y0, float x1, float y1, float width, Rgb c, float alpha) {
    static float cov[1024];
    const float half = width / 2, pad = half + 1.0f;
    const float vx = x1 - x0, vy = y1 - y0, len2 = std::max(1e-6f, vx * vx + vy * vy);
    const int ya = int(std::floor(std::min(y0, y1) - pad)), yb = int(std::ceil(std::max(y0, y1) + pad));
    const int xa = int(std::floor(std::min(x0, x1) - pad)), xb = int(std::ceil(std::max(x0, x1) + pad));
    for (int y = std::max(ya, 0); y <= std::min(yb, sf.height() - 1); ++y) {
        const int a = std::max(xa, 0), b = std::min(xb, sf.width() - 1);
        for (int x = a; x <= b; ++x) {
            const float px = x + 0.5f - x0, py = y + 0.5f - y0;
            const float t = clamp01((px * vx + py * vy) / len2);
            const float dx = px - t * vx, dy = py - t * vy;
            cov[x - a] = alpha * clamp01(half - std::sqrt(dx * dx + dy * dy) + 0.5f);
        }
        blend_span(sf, y, a, b, c, cov);
    }
}

void mask(rk::Surface& sf, int x, int y, int w, int h, const uint8_t* m, Rgb c, float alpha) {
    static float cov[1024];
    for (int j = 0; j < h; ++j) {
        const uint8_t* row = m + size_t(j) * size_t(w);
        int a = 0, b = w - 1;
        while (a <= b && !row[a]) ++a;
        while (b >= a && !row[b]) --b;
        if (a > b) continue;
        for (int i = a; i <= b; ++i) cov[i - a] = alpha * row[i] / 255.0f;
        blend_span(sf, y + j, x + a, x + b, c, cov);
    }
}

void image(rk::Surface& sf, int x, int y, int w, int h, const uint16_t* px, float alpha, float corner, int stride) {
    const int a8 = int(clamp01(alpha) * 255.0f + 0.5f);
    if (stride <= 0) stride = w;
    for (int j = 0; j < h; ++j) {
        const int sy = y + j;
        int x0 = x, x1 = x + w - 1;
        if (!clip_span(sf, sy, x0, x1)) continue;
        const uint16_t* bg = sf.row_at(sy);
        int n = 0;
        for (int sx = x0; sx <= x1; ++sx) {
            const int i = sx - x;
            int a = a8;
            if (corner > 0) {
                // Distance outside the rounded rectangle, only near the corners.
                const float cxr = i + 0.5f < corner ? corner : (i + 0.5f > w - corner ? w - corner : i + 0.5f);
                const float cyr = j + 0.5f < corner ? corner : (j + 0.5f > h - corner ? h - corner : j + 0.5f);
                const float dx = i + 0.5f - cxr, dy = j + 0.5f - cyr;
                const float cov = clamp01(corner - std::sqrt(dx * dx + dy * dy) + 0.5f);
                a = int(a * cov + 0.5f);
            }
            const uint16_t p = px[size_t(j) * size_t(stride) + size_t(i)];
            g_row[n++] = a >= 255 ? p : blend(bg[sx], unpack(p), a);
        }
        sf.write_row(x0, sy, g_row, n);
    }
}

namespace reference {

template <typename Cov>
static void every_pixel(rk::Surface& sf, float cx, float cy, float reach, Rgb c, Cov cov_at) {
    static float cov[1024];
    for (int y = std::max(0, int(cy - reach) - 2); y <= std::min(sf.height() - 1, int(cy + reach) + 2); ++y) {
        const int a = std::max(0, int(cx - reach) - 2), b = std::min(sf.width() - 1, int(cx + reach) + 2);
        for (int x = a; x <= b; ++x) cov[x - a] = cov_at(x + 0.5f - cx, y + 0.5f - cy);
        blend_span(sf, y, a, b, c, cov);
    }
}

void ring(rk::Surface& sf, float cx, float cy, float r, float width, Rgb c, float alpha) {
    every_pixel(sf, cx, cy, r + width, c, [&](float dx, float dy) { return alpha * ring_cov(std::sqrt(dx * dx + dy * dy), r, width / 2); });
}

void arc(rk::Surface& sf, float cx, float cy, float r, float width, float a0, float a1, Rgb c, float alpha) {
    const float half = width / 2;
    every_pixel(sf, cx, cy, r + width, c, [&](float dx, float dy) {
        const float radial = std::fabs(std::sqrt(dx * dx + dy * dy) - r);
        const float gap = arc_gap(dx, dy, r, a0, a1);
        return alpha * clamp01(half - std::sqrt(radial * radial + gap * gap) + 0.5f);
    });
}

void glow_disc(rk::Surface& sf, float cx, float cy, float r, float glow, Rgb c, float alpha, float strength) {
    every_pixel(sf, cx, cy, r + glow + 1, c, [&](float dx, float dy) { return alpha * glow_cov(std::sqrt(dx * dx + dy * dy), r, glow, strength); });
}

}  // namespace reference

}  // namespace shaza
