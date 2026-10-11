#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.hpp"
#include "song.hpp"
#include "text.hpp"

using namespace shaza;

static Reply fixture(const char* name) {
    std::ifstream f(std::string("host/fixtures/") + name + ".reply", std::ios::binary);
    std::vector<uint8_t> d{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    Reply r;
    parse_reply(d.data(), d.size(), r);
    return r;
}

static float hue(Rgb c) {
    const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
    if (mx - mn < 0.08f) return -1;
    float h = mx == r ? (g - b) / (mx - mn) : mx == g ? (b - r) / (mx - mn) + 2 : (r - g) / (mx - mn) + 4;
    return std::fmod(h * 60 + 360, 360);
}

// The board lays titles out itself now. The relay rendered the same real titles with Pillow and
// the same font, so line breaks must agree and widths nearly so.
static void layout_matches_the_relay() {
    const char* names[] = {"kesariya", "get_lucky", "blinding_lights", "vichudo", "golden_hour", "bad_guy", "loser", "laufey", "gurenge"};
    for (const char* name : names) {
        const Reply relay = fixture(name);
        const Mask title = layout(relay.title, Size::Title, 360, 2);
        const Mask artist = layout(relay.artist, Size::Body, 320, 1);
        const bool ok = title.h == relay.title_mask.h && std::abs(title.w - relay.title_mask.w) <= 4 && artist.h == relay.artist_mask.h && std::abs(artist.w - relay.artist_mask.w) <= 4;
        if (!ok)
            std::printf("  %s: title %dx%d vs relay %dx%d, artist %dx%d vs relay %dx%d\n", name, title.w, title.h, relay.title_mask.w, relay.title_mask.h, artist.w, artist.h, relay.artist_mask.w,
                        relay.artist_mask.h);
        CHECK(ok);
    }
}

static void wrapping_rules() {
    const Mask one = layout("Get Lucky", Size::Title, 360, 2);
    const Mask two = layout("Let You Break My Heart Again", Size::Title, 360, 2);
    CHECK(one.h > 0 && two.h > one.h);  // two lines are taller than one
    CHECK(two.w <= 362);
    const Mask clipped = layout("Supercalifragilisticexpialidocious Is A Very Long Title That Will Not Fit On Two Lines At All", Size::Title, 360, 2);
    CHECK(clipped.h == two.h && clipped.w <= 362);
    const Mask artist = layout("Pritam, Arijit Singh & Amitabh Bhattacharya", Size::Body, 320, 1);
    CHECK(artist.w <= 322 && artist.w > 250);
    CHECK(layout("", Size::Title, 360, 2).w == 0);
    CHECK(layout("   ", Size::Title, 360, 2).w == 0);
    CHECK(layout("x", Size::Title, 0, 2).w == 0);
    // A single glyph wider than the box still yields something rather than looping forever.
    CHECK(layout("WWWWWWWWWW", Size::Title, 20, 3).w > 0);
}

static void scripts_and_kerning() {
    CHECK(drawable("Ren\xc3\xa9" "e", Size::Title) == 5);            // é
    CHECK(drawable("\xd0\x9a\xd0\xb8\xd0\xbd\xd0\xbe", Size::Title) == 4);  // Кино
    CHECK(drawable("\xce\xb1\xce\xb2", Size::Title) == 2);            // αβ
    CHECK(drawable("\xe7\xb4\x85\xe8\x93\xae\xe8\x8f\xaf", Size::Title) == 0);  // 紅蓮華
    CHECK(drawable("\xe0\xa4\x95", Size::Title) == 0);                // क
    CHECK(text_width("AV", Size::Title) < text_width("A", Size::Title) + text_width("V", Size::Title));
    CHECK(text_width("No match", Size::Body) > 60);
    // Unknown characters disappear without leaving double spaces.
    CHECK(layout("Hi \xe7\xb4\x85 there", Size::Body, 320, 1).w == layout("Hi there", Size::Body, 320, 1).w);
}

static void reply_building() {
    shazam::Match m;
    m.result = shazam::Match::Result::Found;
    m.title = "WILDFLOWER", m.artist = "Billie Eilish";
    const Reply r = make_reply(m, Picture());
    CHECK(r.status == Reply::Status::Found && r.title_mask.w > 0 && r.artist_mask.w > 0);
    CHECK(r.accent.r == 235 && r.accent.g == 235);  // no cover, soft white

    m.title = "\xe7\xb4\x85\xe8\x93\xae\xe8\x8f\xaf", m.artist = "LiSA";  // nothing drawable: the artist takes its place
    const Reply cjk = make_reply(m, Picture());
    CHECK(cjk.title_mask.w > 0 && cjk.artist_mask.w == 0);
}

static void accent_from_covers() {
    Picture red;
    red.w = red.h = 60;
    red.px.assign(3600, rk::rgb565(150, 20, 20));
    const Rgb a = accent_of(red);
    CHECK(a.r > 180 && a.r > a.g + 60);
    Picture grey = red;
    grey.px.assign(3600, rk::rgb565(90, 90, 90));
    CHECK(accent_of(grey).r == 235);

    // Where the two disagree the board's pick was judged better by eye: the gold helmet on Get
    // Lucky (the relay took a teal reflection), the warm photo on Laufey (the relay's bright green
    // is barely on the cover), and white for the near-monochrome bad guy.
    CHECK(hue(accent_of(fixture("get_lucky").cover)) >= 35 && hue(accent_of(fixture("get_lucky").cover)) <= 60);
    CHECK(hue(accent_of(fixture("laufey").cover)) >= 15 && hue(accent_of(fixture("laufey").cover)) <= 45);
    CHECK(hue(accent_of(fixture("bad_guy").cover)) < 0);

    // Everywhere else the hue should land near the one the relay picked.
    const char* names[] = {"kesariya", "blinding_lights", "vichudo", "golden_hour", "gurenge"};
    for (const char* name : names) {
        const Reply relay = fixture(name);
        const Rgb ours = accent_of(relay.cover);
        const float h1 = hue(ours), h2 = hue(relay.accent);
        float d = std::fabs(h1 - h2);
        d = std::min(d, 360 - d);
        const bool ok = (h1 < 0 && h2 < 0) || (h1 >= 0 && h2 >= 0 && d <= 40);
        if (!ok) std::printf("  %s: ours %02x%02x%02x (hue %.0f), relay %02x%02x%02x (hue %.0f)\n", name, ours.r, ours.g, ours.b, double(h1), relay.accent.r, relay.accent.g, relay.accent.b, double(h2));
        CHECK(ok);
    }
}

int main() {
    layout_matches_the_relay();
    wrapping_rules();
    scripts_and_kerning();
    reply_building();
    accent_from_covers();
    return check_report("text");
}
