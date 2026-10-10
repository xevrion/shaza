#include <cmath>
#include <fstream>
#include <iterator>
#include <vector>

#include "check.hpp"
#include "look.hpp"

using namespace shaza;

struct Screen {
    std::vector<uint16_t> px = std::vector<uint16_t>(466 * 466, 0);
    rk::Surface sf{reinterpret_cast<uint8_t*>(px.data()), 466, 466, 466 * 2};
    long sent() {
        rk::Rect r[rk::Surface::kMaxDirty];
        const int n = sf.take_dirty_rects(r, rk::Surface::kMaxDirty);
        long s = 0;
        for (int i = 0; i < n; ++i) s += long(r[i].x1 - r[i].x0 + 1) * long(r[i].y1 - r[i].y0 + 1);
        return s;
    }
};

static Reply fixture() {
    std::ifstream f("host/fixtures/kesariya.reply", std::ios::binary);
    std::vector<uint8_t> d{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    Reply r;
    parse_reply(d.data(), d.size(), r);
    return r;
}

// Drives an app through every phase: listen, found, listen, no match, offline, ready.
struct Story {
    App app;
    float t = 0;
    int step = 0;
    void advance(float dt) {
        t += dt;
        const float beat = std::fmod(t * 2.0f, 1.0f);
        app.level(app.recording() ? -40.0f + 24.0f * std::exp(-beat * 5.0f) : -80.0f, dt);
        app.update(dt);
        if (step == 0 && t > 0.5f) app.tap(), step = 1;
        if (step == 1 && app.take_clip()) step = 2;
        if (step == 2 && app.phase_time() > 1.0f) app.reply(fixture()), step = 3;
        if (step == 3 && app.phase_time() > 2.0f) app.tap(), step = 4;
        if (step == 4 && app.take_clip()) step = 5;
        if (step == 5 && app.phase_time() > 0.8f) {
            Reply none;
            none.status = Reply::Status::None;
            app.reply(none);
            step = 6;
        }
        if (step == 6 && app.phase() == Phase::Ready) app.set_online(false), app.tap(), step = 7;
        if (step == 7 && app.phase() == Phase::Ready && app.phase_time() > 1.5f) step = 8;
    }
};

static void incremental_matches_full_repaint() {
    for (int which = 0; which < kLooks; ++which) {
        auto fast = make_look(which), full = make_look(which);
        Screen a, b;
        Story s;
        int frames = 0, bad = 0;
        while (s.step < 8 && frames < 60 * 60) {
            s.advance(1.0f / 60);
            fast->frame(a.sf, s.app, s.t);
            full->invalidate();
            full->frame(b.sf, s.app, s.t);
            a.sent(), b.sent();
            if (a.px != b.px && bad++ < 3) {
                int diff = 0;
                for (size_t i = 0; i < a.px.size(); ++i) diff += a.px[i] != b.px[i];
                std::printf("  %s: frame %d (%s, %.2f s in) differs in %d pixels\n", fast->name(), frames, phase_name(s.app.phase()), double(s.app.phase_time()), diff);
            }
            ++frames;
        }
        CHECK(s.step == 8);
        CHECK(bad == 0);
    }
}

static void still_screens_send_nothing() {
    for (int which = 0; which < kLooks; ++which) {
        auto look = make_look(which);
        Screen sc;
        App app;
        app.tap();
        for (int i = 0; i < 60 * 6; ++i) app.update(1.0f / 60);
        app.reply(fixture());
        for (int i = 0; i < 60 * 2; ++i) {
            app.update(1.0f / 60);
            look->frame(sc.sf, app, i / 60.0f);
            sc.sent();
        }
        // Two seconds into a result nothing moves, so nothing is sent.
        long total = 0;
        for (int i = 0; i < 60; ++i) {
            app.update(1.0f / 60);
            look->frame(sc.sf, app, 2 + i / 60.0f);
            total += sc.sent();
        }
        CHECK(total == 0);
    }
}

int main() {
    incremental_matches_full_repaint();
    still_screens_send_nothing();
    return check_report("look");
}
