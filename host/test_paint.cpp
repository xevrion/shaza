#include <cstdlib>
#include <vector>

#include "check.hpp"
#include "paint.hpp"

using namespace shaza;

struct Canvas {
    std::vector<uint16_t> px;
    rk::Surface sf;
    explicit Canvas(int w = 466, int h = 466) : px(size_t(w) * h, 0), sf(reinterpret_cast<uint8_t*>(px.data()), w, h, w * 2) {}
};

static float rnd(float lo, float hi) { return lo + (hi - lo) * float(std::rand()) / float(RAND_MAX); }

static int differing(const Canvas& a, const Canvas& b) {
    int n = 0;
    for (size_t i = 0; i < a.px.size(); ++i) n += a.px[i] != b.px[i];
    return n;
}

static long lit(const Canvas& a) {
    long n = 0;
    for (uint16_t p : a.px) n += p != 0;
    return n;
}

// Largest difference in any 8 bit channel between the two canvases.
static int channel_gap(const Canvas& a, const Canvas& b) {
    int worst = 0;
    for (size_t i = 0; i < a.px.size(); ++i) {
        const Rgb p = unpack(a.px[i]), q = unpack(b.px[i]);
        worst = std::max({worst, std::abs(p.r - q.r), std::abs(p.g - q.g), std::abs(p.b - q.b)});
    }
    return worst;
}

static void shapes_match_reference() {
    std::srand(7);
    int worst = 0, worst_gap = 0;
    long total_lit = 0;
    for (int i = 0; i < 300; ++i) {
        Canvas fast, slow;
        // A background so blending is exercised, not only writes onto black.
        for (size_t k = 0; k < fast.px.size(); k += 7) fast.px[k] = slow.px[k] = uint16_t(k * 2654435761u >> 16);
        const float cx = rnd(-40, 506), cy = rnd(-40, 506), r = rnd(0.5f, 240), w = rnd(0.6f, 14), al = rnd(0.1f, 1);
        const Rgb c = {uint8_t(std::rand()), uint8_t(std::rand()), uint8_t(std::rand())};
        bool full_circle = false;
        switch (i % 3) {
        case 0:
            ring(fast.sf, cx, cy, r, w, c, al);
            reference::ring(slow.sf, cx, cy, r, w, c, al);
            break;
        case 1: {
            const float a0 = rnd(-7, 7), a1 = a0 + rnd(0.01f, 6.5f);
            arc(fast.sf, cx, cy, r, w, a0, a1, c, al);
            reference::arc(slow.sf, cx, cy, r, w, a0, a1, c, al);
            full_circle = a1 - a0 >= 6.28318530718f;  // drawn as a ring, so held to the ring's tolerance
            break;
        }
        default: {
            const float g = rnd(0, 60), s = rnd(0, 1);
            glow_disc(fast.sf, cx, cy, r / 3, g, c, al, s);
            reference::glow_disc(slow.sf, cx, cy, r / 3, g, c, al, s);
        }
        }
        // Arcs are computed exactly, so they must match exactly. Rings and glows go through a table
        // (radial_rows) and may be a step of the RGB565 grid off: 8 levels in red and blue.
        if (i % 3 == 1 && !full_circle) {
            const int d = differing(fast, slow);
            if (d > worst) worst = d, std::printf("  arc case %d differs in %d pixels\n", i, d);
        } else {
            worst_gap = std::max(worst_gap, channel_gap(fast, slow));
        }
        total_lit += lit(fast);
    }
    std::printf("  rings and glows: largest channel difference from the exact formula %d of 255\n", worst_gap);
    CHECK(worst == 0);
    CHECK(worst_gap <= 9);
    CHECK(total_lit > 100000);
}

static void ring_is_where_it_should_be() {
    Canvas c;
    ring(c.sf, 233, 233, 100, 3, {255, 255, 255});
    CHECK(c.px[233 * 466 + 333] != 0);   // on the ring, right side
    CHECK(c.px[133 * 466 + 233] != 0);   // top
    CHECK(c.px[233 * 466 + 233] == 0);   // centre stays empty
    CHECK(c.px[233 * 466 + 340] == 0);   // outside
    CHECK(c.px[233 * 466 + 333] == 0xFFFF);
}

static void arc_starts_at_twelve_and_runs_clockwise() {
    Canvas c;
    arc(c.sf, 233, 233, 100, 4, 0, 1.5708f, {255, 255, 255});
    CHECK(c.px[(233 - 100) * 466 + 250] != 0);  // just right of twelve
    CHECK(c.px[(233 - 30) * 466 + 233 + 95] != 0);  // near three
    CHECK(c.px[(233 + 100) * 466 + 233] == 0);  // six is not in it
    CHECK(c.px[233 * 466 + 133] == 0);          // nor nine
}

static void dirty_box_covers_only_the_shape() {
    Canvas c;
    rk::Rect r;
    c.sf.take_dirty(r);
    ring(c.sf, 233, 233, 50, 2, {255, 255, 255});
    CHECK(c.sf.take_dirty(r));
    // 54 is the ring plus its anti-aliasing margin, and one more for the panel's even alignment.
    CHECK(r.x0 >= 233 - 55 && r.x1 <= 233 + 55 && r.y0 >= 233 - 55 && r.y1 <= 233 + 55);
    // A fully transparent shape paints nothing.
    ring(c.sf, 233, 233, 50, 2, {255, 255, 255}, 0.0f);
    CHECK(!c.sf.take_dirty(r));
}

static void mask_and_image() {
    Canvas c;
    const uint8_t m[] = {0, 255, 0, 128};
    mask(c.sf, 10, 10, 2, 2, m, {255, 255, 255});
    CHECK(c.px[10 * 466 + 10] == 0 && c.px[10 * 466 + 11] == 0xFFFF && c.px[11 * 466 + 11] != 0 && c.px[11 * 466 + 11] != 0xFFFF);
    mask(c.sf, -1, 20, 2, 2, m, {255, 255, 255});  // clipped on the left
    CHECK(c.px[20 * 466 + 0] == 0xFFFF);

    std::vector<uint16_t> img(40 * 40, 0xF800);
    image(c.sf, 100, 100, 40, 40, img.data(), 1.0f, 10.0f);
    CHECK(c.px[120 * 466 + 120] == 0xF800);
    CHECK(c.px[100 * 466 + 100] == 0);  // rounded-off corner
    image(c.sf, 200, 100, 40, 40, img.data(), 0.5f);
    const Rgb half = unpack(c.px[120 * 466 + 220]);
    CHECK(half.r > 110 && half.r < 145 && half.g == 0);
}

static void colour_maths() {
    CHECK(blend(0, {255, 255, 255}, 255) == 0xFFFF);
    CHECK(blend(0x1234, {255, 255, 255}, 0) == 0x1234);
    const Rgb w = unpack(0xFFFF);
    CHECK(w.r == 255 && w.g == 255 && w.b == 255);
    CHECK_NEAR(ease_out(0), 0, 1e-6);
    CHECK_NEAR(ease_out(1), 1, 1e-6);
    CHECK_NEAR(ease_in_out(0.5f), 0.5, 1e-6);
}

int main() {
    shapes_match_reference();
    ring_is_where_it_should_be();
    arc_starts_at_twelve_and_runs_clockwise();
    dirty_box_covers_only_the_shape();
    mask_and_image();
    colour_maths();
    return check_report("paint");
}
