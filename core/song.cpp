#include "song.hpp"

#include <algorithm>
#include <cmath>

#include "text.hpp"

namespace shaza {

namespace {

void to_hls(float r, float g, float b, float& h, float& l, float& s) {
    const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
    l = (mx + mn) / 2;
    if (mx == mn) {
        h = s = 0;
        return;
    }
    const float d = mx - mn;
    s = l <= 0.5f ? d / (mx + mn) : d / (2 - mx - mn);
    if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h /= 6;
}

float hue_part(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}

Rgb from_hls(float h, float l, float s) {
    if (s <= 0) return {uint8_t(l * 255 + 0.5f), uint8_t(l * 255 + 0.5f), uint8_t(l * 255 + 0.5f)};
    const float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    auto c = [](float v) { return uint8_t(std::lround(std::min(1.0f, std::max(0.0f, v)) * 255)); };
    return {c(hue_part(p, q, h + 1.0f / 3)), c(hue_part(p, q, h)), c(hue_part(p, q, h - 1.0f / 3))};
}

}  // namespace

Rgb accent_of(const Picture& cover) {
    constexpr Rgb kSoftWhite = {235, 235, 235};
    constexpr int kBins = 18;
    if (cover.px.empty()) return kSoftWhite;
    float weight[kBins] = {}, r_sum[kBins] = {}, g_sum[kBins] = {}, b_sum[kBins] = {};
    const int step = std::max(1, cover.w / 48);
    int samples = 0;
    for (int y = 0; y < cover.h; y += step) {
        for (int x = 0; x < cover.w; x += step, ++samples) {
            const Rgb c = unpack(cover.px[size_t(y) * size_t(cover.w) + size_t(x)]);
            float h, l, s;
            to_hls(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, h, l, s);
            if (s <= 0.25f || l < 0.06f || l > 0.96f) continue;
            const int bin = std::min(kBins - 1, int(h * kBins));
            const float w = s * s * (0.2f + std::min(l, 1 - l));
            weight[bin] += w, r_sum[bin] += w * c.r, g_sum[bin] += w * c.g, b_sum[bin] += w * c.b;
        }
    }
    int best = 0;
    for (int i = 1; i < kBins; ++i)
        if (weight[i] > weight[best]) best = i;
    // A cover with only a sprinkle of colour stays white rather than taking a speck's hue.
    if (weight[best] < 0.004f * float(samples)) return kSoftWhite;
    float h, l, s;
    to_hls(r_sum[best] / weight[best] / 255.0f, g_sum[best] / weight[best] / 255.0f, b_sum[best] / weight[best] / 255.0f, h, l, s);
    return from_hls(h, std::max(0.62f, std::min(l, 0.78f)), std::min(1.0f, s * 1.1f));
}

Reply make_reply(const shazam::Match& m, Picture cover) {
    Reply r;
    r.status = Reply::Status::Found;
    r.title = m.title, r.artist = m.artist, r.via = "shazam";
    r.accent = accent_of(cover);
    r.cover = std::move(cover);
    if (drawable(m.title, Size::Title) > 0) {
        r.title_mask = layout(m.title, Size::Title, kTitleW, kTitleLines);
        r.artist_mask = layout(m.artist, Size::Body, kArtistW, kArtistLines);
    } else {
        r.title_mask = layout(m.artist, Size::Title, kTitleW, kTitleLines);
    }
    return r;
}

}  // namespace shaza
