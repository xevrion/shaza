#include "look.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "text.hpp"

namespace shaza {

namespace {

constexpr float kTau = 6.28318530718f;
constexpr int kW = 466, kCX = 233, kCY = 233;
constexpr int kCover = 200, kCoverX = kCX - kCover / 2, kCoverY = 52;
constexpr int kTitleTop = 274;
constexpr Rgb kWhite = {246, 244, 240};
constexpr Rgb kGrey = {150, 150, 150};

rk::Rect g_zones[Look::kMaxItems * 2];
int g_zone_n = 0;

struct Key {
    uint32_t h = 2166136261u;
    Key& add(int v) {
        h = (h ^ uint32_t(v)) * 16777619u;
        return *this;
    }
    // Snaps v to the step it is keyed on, so what gets painted depends only on the key; otherwise
    // a change smaller than the step would show after a full repaint but never incrementally.
    Key& add(float& v, float step) {
        const int i = int(std::lround(v / step));
        v = float(i) * step;
        return add(i);
    }
    Key& add(const char* s) {
        while (*s) add(int(*s++));
        return *this;
    }
};

rk::Rect around(float cx, float cy, float reach) {
    return {int(std::floor(cx - reach)) - 2, int(std::floor(cy - reach)) - 2, int(std::ceil(cx + reach)) + 2, int(std::ceil(cy + reach)) + 2};
}

bool overlap(const rk::Rect& a, const rk::Rect& b) { return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1; }
rk::Rect unite(const rk::Rect& a, const rk::Rect& b) { return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)}; }
bool same(const rk::Rect& a, const rk::Rect& b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

float breathe(float now, float period) { return 0.5f + 0.5f * std::sin(now * kTau / period); }

const char* ago(float s, char* buf, size_t n) {
    if (s < 60) return "just now";
    if (s < 3600) std::snprintf(buf, n, "%dm ago", int(s / 60));
    else if (s < 86400) std::snprintf(buf, n, "%dh ago", int(s / 3600));
    else return "";
    return buf;
}

}  // namespace

int last_zones(rk::Rect* out, int max) {
    const int n = std::min(max, g_zone_n);
    std::copy(g_zones, g_zones + n, out);
    return n;
}

void Look::frame(rk::Surface& sf, const App& app, float now) {
    Item items[kMaxItems];
    const int n = layout(app, now, items);
    const rk::Rect screen = {0, 0, sf.width() - 1, sf.height() - 1};

    rk::Rect* zones = g_zones;
    int z = 0;
    auto add = [&](rk::Rect r) {
        r = {std::max(r.x0, 0), std::max(r.y0, 0), std::min(r.x1, screen.x1), std::min(r.y1, screen.y1)};
        if (r.x1 >= r.x0 && r.y1 >= r.y0 && z < kMaxItems * 2) zones[z++] = r;
    };
    if (full_) {
        add(screen);
        full_ = false;
    } else {
        for (int i = 0; i < n; ++i) {
            const Item* old = nullptr;
            for (int j = 0; j < last_n_; ++j)
                if (last_[j].id == items[i].id) old = &last_[j];
            if (!old) add(items[i].box);
            else if (old->key != items[i].key || !same(old->box, items[i].box)) add(unite(old->box, items[i].box));
        }
        for (int j = 0; j < last_n_; ++j) {
            bool kept = false;
            for (int i = 0; i < n; ++i) kept |= items[i].id == last_[j].id;
            if (!kept) add(last_[j].box);
        }
    }
    // Zones that touch become one, so no pixel is cleared and painted twice.
    for (bool merged = true; merged;) {
        merged = false;
        for (int a = 0; a < z && !merged; ++a)
            for (int b = a + 1; b < z && !merged; ++b)
                if (overlap(zones[a], zones[b])) zones[a] = unite(zones[a], zones[b]), zones[b] = zones[--z], merged = true;
    }
    for (int k = 0; k < z; ++k) {
        set_clip(&zones[k]);
        clear(sf, zones[k]);
        for (int i = 0; i < n; ++i)
            if (overlap(items[i].box, zones[k])) paint(sf, items[i].id);
    }
    set_clip(nullptr);
    g_zone_n = z;
    std::copy(items, items + n, last_);
    last_n_ = n;
}

namespace {

// What all three directions share: the result, the notices and the "4m ago" line.
class Common : public Look {
protected:
    enum { kCoverItem = 100, kTitleItem, kArtistItem, kNotice1, kNotice2, kAgo };

    // How far the result is faded in, 0..1, with a delay and duration in seconds.
    static float found_in(const App& app, float delay, float dur) {
        if (!app.song()) return 0;
        if (app.phase() == Phase::Found) return ease_out((app.phase_time() - delay) / dur);
        if (app.previous() == Phase::Found && app.phase_time() < 0.3f) return 1.0f - app.phase_time() / 0.3f;
        return 0;
    }

    static bool notice_phase(Phase p) { return p == Phase::NotFound || p == Phase::Offline || p == Phase::Failed; }

    static float notice_alpha(const App& app) {
        if (notice_phase(app.phase())) {
            const float t = app.phase_time();
            return clamp01(t / 0.35f) * clamp01((App::kNoticeS - t) / 0.5f);
        }
        if (notice_phase(app.previous()) && app.phase_time() < 0.25f) return 1.0f - app.phase_time() / 0.25f;
        return 0;
    }

    int add_found(const App& app, Item* items, int n, float cover_alpha, float corner) {
        if (cover_alpha > 0 && !app.song()->cover.px.empty()) {
            cover_a_ = cover_alpha, corner_ = corner;
            items[n++] = {kCoverItem, Key().add(cover_a_, 1 / 128.0f).add(corner_, 0.5f).add(int(app.song()->cover.px[0])).h, {kCoverX, kCoverY, kCoverX + kCover - 1, kCoverY + kCover - 1}};
        }
        const Mask& tm = app.song()->title_mask;
        const Mask& am = app.song()->artist_mask;
        title_a_ = found_in(app, 0.18f, 0.5f), artist_a_ = found_in(app, 0.3f, 0.5f);
        song_ = app.song();
        if (title_a_ > 0 && tm.w) {
            title_y_ = kTitleTop;
            items[n++] = {kTitleItem, Key().add(title_a_, 1 / 128.0f).add(tm.w).add(tm.h).h, {kCX - tm.w / 2, title_y_, kCX - tm.w / 2 + tm.w - 1, title_y_ + tm.h - 1}};
        }
        if (artist_a_ > 0 && am.w) {
            artist_y_ = kTitleTop + (tm.w ? tm.h : 0) + 2;
            items[n++] = {kArtistItem, Key().add(artist_a_, 1 / 128.0f).add(am.w).add(int(app.song()->accent.r)).h, {kCX - am.w / 2, artist_y_, kCX - am.w / 2 + am.w - 1, artist_y_ + am.h - 1}};
        }
        return n;
    }

    int add_notice(const App& app, Item* items, int n, int y) {
        notice_a_ = notice_alpha(app);
        if (notice_a_ <= 0) return n;
        const Phase p = notice_phase(app.phase()) ? app.phase() : app.previous();
        line1_ = p == Phase::NotFound ? "No match" : p == Phase::Offline ? "No Wi-Fi" : app.error().c_str();
        line2_ = "Tap to try again";
        notice_y_ = y;
        const int w1 = text_width(line1_, Size::Body), w2 = text_width(line2_, Size::Small);
        items[n++] = {kNotice1, Key().add(notice_a_, 1 / 128.0f).add(line1_).h, {kCX - w1 / 2 - 3, y - 26, kCX + w1 / 2 + 3, y + 8}};
        items[n++] = {kNotice2, Key().add(notice_a_, 1 / 128.0f).h, {kCX - w2 / 2 - 3, y + 30 - 19, kCX + w2 / 2 + 3, y + 30 + 6}};
        return n;
    }

    int add_ago(const App& app, Item* items, int n, int y) {
        if (app.phase() != Phase::Ready || app.since_song() < 0) return n;
        ago_a_ = 0.5f * clamp01((app.phase_time() - 0.4f) / 0.6f);
        if (ago_a_ <= 0) return n;
        std::snprintf(ago_, sizeof ago_, "%s", ago(app.since_song(), ago_buf_, sizeof ago_buf_));
        ago_y_ = y;
        const int w = text_width(ago_, Size::Small);
        items[n++] = {kAgo, Key().add(ago_a_, 1 / 64.0f).add(ago_).h, {kCX - w / 2 - 3, y - 19, kCX + w / 2 + 3, y + 6}};
        return n;
    }

    // Paints a shared item; false when id is not one of them.
    bool paint_common(rk::Surface& sf, int id) {
        switch (id) {
        case kCoverItem:
            image(sf, kCoverX, kCoverY, kCover, kCover, song_->cover.px.data(), cover_a_, corner_);
            return true;
        case kTitleItem: {
            const Mask& m = song_->title_mask;
            mask(sf, kCX - m.w / 2, title_y_, m.w, m.h, m.cov.data(), kWhite, title_a_);
            return true;
        }
        case kArtistItem: {
            const Mask& m = song_->artist_mask;
            mask(sf, kCX - m.w / 2, artist_y_, m.w, m.h, m.cov.data(), song_->accent, artist_a_);
            return true;
        }
        case kNotice1:
            text(sf, kCX, notice_y_, line1_, Size::Body, kWhite, notice_a_ * 0.9f);
            return true;
        case kNotice2:
            text(sf, kCX, notice_y_ + 30, line2_, Size::Small, kGrey, notice_a_ * 0.8f);
            return true;
        case kAgo:
            text(sf, kCX, ago_y_, ago_, Size::Small, kGrey, ago_a_);
            return true;
        }
        return false;
    }

    const Reply* song_ = nullptr;
    float cover_a_ = 0, corner_ = 0, title_a_ = 0, artist_a_ = 0, notice_a_ = 0, ago_a_ = 0;
    int title_y_ = 0, artist_y_ = 0, notice_y_ = 0, ago_y_ = 0;
    const char* line1_ = "";
    const char* line2_ = "";
    char ago_[24] = {}, ago_buf_[24] = {};
};

// Halo: one hairline ring that breathes; listening sends ripples out of it as loud as the room.
class Halo : public Common {
public:
    const char* name() const override { return "halo"; }

protected:
    enum { kRipple = 0, kRing = 3, kProgress, kComet };
    static constexpr float kR = 92;

    int layout(const App& app, float now, Item* items) override {
        int n = 0;
        const Phase p = app.phase();
        const float t = app.phase_time(), lv = app.level();
        const float b = breathe(now, 5.0f);

        // Base ring.
        ring_r_ = kR, ring_w_ = 2.0f, ring_a_ = 0;
        switch (p) {
        case Phase::Ready:
            ring_r_ = kR + 2.0f * b;
            ring_a_ = (0.3f + 0.22f * b) * (app.previous() == Phase::Found || app.previous() == Phase::Listening ? clamp01(t / 0.5f) : 1.0f);
            break;
        case Phase::Listening:
            ring_r_ = kR + 9.0f * lv;
            ring_a_ = 0.28f;
            break;
        case Phase::Thinking:
            ring_r_ = kR - 8.0f * ease_out(t / 0.5f);
            ring_a_ = 0.24f;
            break;
        case Phase::Found:
            ring_r_ = kR + 28.0f * ease_out(t / 0.35f);
            ring_a_ = 0.5f * (1.0f - clamp01(t / 0.35f));
            break;
        default:
            ring_a_ = 0.18f;
        }
        if (ring_a_ > 0.004f) items[n++] = {kRing, Key().add(ring_r_, 0.1f).add(ring_a_, 1 / 255.0f).h, around(kCX, kCY, ring_r_ + 3)};

        // Ripples, while listening and for a moment after.
        const bool rippling = p == Phase::Listening || (p == Phase::Thinking && t < 0.3f);
        for (int i = 0; i < 3 && rippling; ++i) {
            const float clock = p == Phase::Listening ? t : App::kListenS + t;
            const float ph = std::fmod(clock / 1.5f + i / 3.0f, 1.0f);
            const float fade = p == Phase::Listening ? clamp01(t / 0.3f) : 1.0f - t / 0.3f;
            ripple_r_[i] = kR + 9.0f * lv + ph * (26.0f + 32.0f * lv);
            ripple_a_[i] = (1 - ph) * (1 - ph) * (0.12f + 0.65f * lv) * fade;
            if (ripple_a_[i] > 0.004f) items[n++] = {kRipple + i, Key().add(ripple_r_[i], 0.1f).add(ripple_a_[i], 1 / 255.0f).h, around(kCX, kCY, ripple_r_[i] + 3)};
        }

        if (p == Phase::Listening) {
            prog_ = app.listen_progress(), prog_r_ = ring_r_;
            items[n++] = {kProgress, Key().add(prog_, 1 / 720.0f).add(prog_r_, 0.1f).h, around(kCX, kCY, prog_r_ + 3)};
        }
        if (p == Phase::Thinking) {
            comet_head_ = kTau * (t / 1.3f);
            comet_a_ = clamp01(t / 0.25f) * 0.95f;
            items[n++] = {kComet, Key().add(comet_head_, 0.004f).add(comet_a_, 1 / 255.0f).add(ring_r_, 0.1f).h, around(kCX, kCY, ring_r_ + 3)};
        }

        n = add_found(app, items, n, found_in(app, 0.05f, 0.45f), 16.0f);
        n = add_notice(app, items, n, 352);
        n = add_ago(app, items, n, 408);
        return n;
    }

    void paint(rk::Surface& sf, int id) override {
        if (paint_common(sf, id)) return;
        if (id == kRing) ring(sf, kCX, kCY, ring_r_, ring_w_, kWhite, ring_a_);
        else if (id >= kRipple && id < kRipple + 3) ring(sf, kCX, kCY, ripple_r_[id], 1.4f, kWhite, ripple_a_[id]);
        else if (id == kProgress && prog_ > 0.001f) arc(sf, kCX, kCY, prog_r_, 2.4f, 0, prog_ * kTau, kWhite, 0.95f);
        else if (id == kComet) arc(sf, kCX, kCY, ring_r_, 2.6f, comet_head_ - 0.8f, comet_head_, kWhite, comet_a_);
    }

    float ring_r_ = kR, ring_w_ = 2, ring_a_ = 0, prog_ = 0, prog_r_ = kR, comet_head_ = 0, comet_a_ = 0;
    float ripple_r_[3] = {}, ripple_a_[3] = {};
};

// Orb: a small glowing pearl that swells with the room and opens into the cover.
class Orb : public Common {
public:
    const char* name() const override { return "orb"; }

protected:
    enum { kGlow = 0, kTrack, kProgress, kPearl, kReveal };
    static constexpr Rgb kPearlColour = {250, 245, 236};
    static constexpr float kTrackR = 130;

    int layout(const App& app, float now, Item* items) override {
        int n = 0;
        const Phase p = app.phase();
        const float t = app.phase_time(), lv = app.level();
        const float b = breathe(now, 4.5f);

        pearl_a_ = 0;
        switch (p) {
        case Phase::Ready:
            r_ = 22 + 2 * b, glow_ = 56 + 6 * b, strength_ = 0.3f;
            pearl_a_ = (0.55f + 0.25f * b) * (app.previous() == Phase::Found ? clamp01((t - 0.15f) / 0.5f) : 1.0f);
            break;
        case Phase::Listening:
            r_ = 22 + 30 * lv, glow_ = 56 + 40 * lv, strength_ = 0.3f + 0.25f * lv, pearl_a_ = 0.95f;
            break;
        case Phase::Thinking: {
            const float beat = std::sin(kTau * t / 1.1f);
            const float settle = ease_out(t / 0.45f);
            r_ = 22 + (12 + 3 * beat * beat - 22) * settle, glow_ = 56 - 20 * settle, strength_ = 0.3f, pearl_a_ = 0.85f;
            break;
        }
        case Phase::Found:
            break;
        default:
            r_ = 14, glow_ = 34, strength_ = 0.25f, pearl_a_ = 0.35f;
        }
        if (pearl_a_ > 0.004f)
            items[n++] = {kPearl, Key().add(r_, 0.1f).add(glow_, 0.25f).add(strength_, 0.01f).add(pearl_a_, 1 / 255.0f).h, around(kCX, kCY, r_ + glow_ + 2)};

        track_a_ = 0;
        if (p == Phase::Listening) track_a_ = clamp01(t / 0.3f), prog_ = app.listen_progress();
        else if (p == Phase::Thinking && t < 0.35f) track_a_ = 1 - t / 0.35f, prog_ = 1;
        if (track_a_ > 0.004f) {
            items[n++] = {kTrack, Key().add(track_a_, 1 / 255.0f).h, around(kCX, kCY, kTrackR + 3)};
            items[n++] = {kProgress, Key().add(prog_, 1 / 720.0f).add(track_a_, 1 / 255.0f).h, around(kCX, kCY, kTrackR + 3)};
        }

        // The pearl opens into the cover: a circle that grows from the pearl's size and rises.
        reveal_a_ = 0;
        if (app.song() && (p == Phase::Found || (app.previous() == Phase::Found && t < 0.3f))) {
            const float k = p == Phase::Found ? ease_out(t / 0.55f) : 1.0f;
            reveal_a_ = p == Phase::Found ? clamp01(t / 0.12f) : 1 - t / 0.3f;
            reveal_r_ = 22 + (kCover / 2 - 22) * k;
            reveal_cy_ = kCY + (kCoverY + kCover / 2 - kCY) * k;
            glow_in_ = p == Phase::Found ? ease_out((t - 0.3f) / 0.6f) : reveal_a_;
            accent_ = app.song()->accent;
            if (glow_in_ > 0)
                items[n++] = {kGlow, Key().add(glow_in_, 1 / 128.0f).add(int(accent_.r) << 16 | accent_.g << 8 | accent_.b).h, around(kCX, kCoverY + kCover / 2, kCover / 2 + 36)};
            items[n++] = {kReveal, Key().add(reveal_r_, 0.1f).add(reveal_cy_, 0.1f).add(reveal_a_, 1 / 128.0f).add(int(app.song()->cover.px[0])).h,
                          around(kCX, reveal_cy_, reveal_r_ + 1)};
        }

        n = add_found(app, items, n, 0, 0);
        n = add_notice(app, items, n, 352);
        n = add_ago(app, items, n, 408);
        return n;
    }

    void paint(rk::Surface& sf, int id) override {
        if (paint_common(sf, id)) return;
        switch (id) {
        case kPearl: glow_disc(sf, kCX, kCY, r_, glow_, kPearlColour, pearl_a_, strength_); break;
        case kTrack: ring(sf, kCX, kCY, kTrackR, 1.5f, kWhite, 0.12f * track_a_); break;
        case kProgress:
            if (prog_ > 0.001f) arc(sf, kCX, kCY, kTrackR, 2.0f, 0, prog_ * kTau, kWhite, 0.55f * track_a_);
            break;
        case kGlow: glow_disc(sf, kCX, kCoverY + kCover / 2, kCover / 2 - 6, 40, accent_, glow_in_ * 0.9f, 0.45f); break;
        case kReveal: {
            if (!song_ || song_->cover.px.empty()) break;
            const int r = int(reveal_r_ + 0.5f);
            const int off = kCover / 2 - r;
            image(sf, kCX - r, int(reveal_cy_ + 0.5f) - r, 2 * r, 2 * r, song_->cover.px.data() + off * kCover + off, reveal_a_, float(r), kCover);
            break;
        }
        }
    }

    float r_ = 22, glow_ = 56, strength_ = 0.3f, pearl_a_ = 0, track_a_ = 0, prog_ = 0;
    float reveal_a_ = 0, reveal_r_ = 0, reveal_cy_ = 0, glow_in_ = 0;
    Rgb accent_ = kWhite;
};

// Dial: 64 fine ticks round the edge. Listening sweeps once round them and each tick keeps the
// loudness it heard, so the clip is left drawn as a groove.
class Dial : public Common {
public:
    const char* name() const override { return "dial"; }

protected:
    static constexpr int kTicks = 64, kPerSector = 4, kSectors = kTicks / kPerSector;
    static constexpr float kR0 = 150, kMaxLen = 34, kIdle = 0.14f;

    int layout(const App& app, float now, Item* items) override {
        int n = 0;
        const Phase p = app.phase();
        const float t = app.phase_time(), lv = app.level();
        if (p == Phase::Listening && (seen_ != Phase::Listening || t < last_t_)) std::fill(hist_, hist_ + kTicks, 0.0f);
        seen_ = p, last_t_ = t;

        const int cur = p == Phase::Listening ? std::min(kTicks - 1, int(app.listen_progress() * kTicks)) : -1;
        if (cur >= 0) hist_[cur] = std::max(hist_[cur], lv);
        const float b = breathe(now, 4.0f);

        // Only the ticks that move are repainted, so whole-dial changes (every tick at once) are kept
        // to single frames: the dial's box is about 150k pixels, past the panel's budget.
        for (int k = 0; k < kTicks; ++k) {
            float len = 3, a = kIdle;
            switch (p) {
            case Phase::Ready:
                if (k == 0) a = 0.3f + 0.5f * b;
                if (app.previous() == Phase::Found && t < 0.5f) a = 0;
                break;
            case Phase::Listening:
                if (k < cur) len = 3 + kMaxLen * hist_[k], a = 0.8f;
                else if (k == cur) len = 3 + kMaxLen * lv, a = 1.0f;
                break;
            case Phase::Thinking: {
                // The groove stays as heard; one bright tick goes round it.
                len = 3 + kMaxLen * hist_[k];
                a = 0.55f;
                const float head = std::fmod(t / 1.2f, 1.0f) * kTicks;
                const float behind = std::fmod(head - k + kTicks, float(kTicks));
                if (behind < 4) a = std::max(a, 1.0f - behind / 8);
                break;
            }
            case Phase::Found:
                // Four steps rather than a smooth fade: each step repaints the whole dial.
                len = 3 + kMaxLen * hist_[k];
                a = 0.55f * (1.0f - std::ceil(clamp01(t / 0.24f) * 4) / 4);
                break;
            default:
                a = 0.1f;
            }
            len_[k] = len, alpha_[k] = a;
        }
        for (int s = 0; s < kSectors; ++s) {
            Key key;
            float reach = 0;
            for (int k = s * kPerSector; k < (s + 1) * kPerSector; ++k) key.add(len_[k], 0.25f).add(alpha_[k], 1 / 255.0f), reach = std::max(reach, alpha_[k]);
            if (reach < 0.004f) continue;
            items[n++] = {s, key.h, sector_box(s)};
        }

        n = add_found(app, items, n, found_in(app, 0.2f, 0.45f), 16.0f);
        n = add_notice(app, items, n, 236);
        n = add_ago(app, items, n, 236);
        return n;
    }

    rk::Rect sector_box(int s) const {
        rk::Rect box = {kW, kW, 0, 0};
        for (int k = s * kPerSector; k < (s + 1) * kPerSector; ++k) {
            const float a = kTau * k / kTicks, sx = std::sin(a), cy = -std::cos(a);
            box = unite(box, around(kCX + sx * kR0, kCY + cy * kR0, 3));
            box = unite(box, around(kCX + sx * (kR0 + 3 + kMaxLen), kCY + cy * (kR0 + 3 + kMaxLen), 3));
        }
        return box;
    }

    void paint(rk::Surface& sf, int id) override {
        if (paint_common(sf, id)) return;
        for (int k = id * kPerSector; k < (id + 1) * kPerSector; ++k) {
            const float a = kTau * k / kTicks, sx = std::sin(a), cy = -std::cos(a);
            line(sf, kCX + sx * kR0, kCY + cy * kR0, kCX + sx * (kR0 + len_[k]), kCY + cy * (kR0 + len_[k]), 2.6f, kWhite, alpha_[k]);
        }
    }

    float hist_[kTicks] = {}, len_[kTicks] = {}, alpha_[kTicks] = {};
    Phase seen_ = Phase::Ready;
    float last_t_ = 0;
};

}  // namespace

std::unique_ptr<Look> make_look(int which) {
    switch (which) {
    case 1: return std::make_unique<Orb>();
    case 2: return std::make_unique<Dial>();
    default: return std::make_unique<Halo>();
    }
}

}  // namespace shaza
