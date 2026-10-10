#include <cstdio>
#include <vector>

#include "audio.hpp"
#include "check.hpp"

using namespace shaza;

static uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

static void header() {
    uint8_t h[kWavHeader];
    wav_header(h, 16000, 1, 32000);
    CHECK(std::memcmp(h, "RIFF", 4) == 0 && std::memcmp(h + 8, "WAVEfmt ", 8) == 0 && std::memcmp(h + 36, "data", 4) == 0);
    CHECK(u32(h + 4) == 36 + 32000);
    CHECK(u32(h + 24) == 16000);
    CHECK(u32(h + 28) == 32000);
    CHECK(h[22] == 1 && h[32] == 2 && h[34] == 16);
    CHECK(u32(h + 40) == 32000);
    wav_header(h, 16000, 2, 0);
    CHECK(u32(h + 28) == 64000 && h[32] == 4);
}

static void loudness() {
    std::vector<int16_t> s(1600, 0);
    CHECK(block_dbfs(s.data(), int(s.size())) == -96.0f);
    CHECK(block_dbfs(s.data(), 0) == -96.0f);
    for (size_t i = 0; i < s.size(); ++i) s[i] = (i & 1) ? 32767 : -32767;
    CHECK_NEAR(block_dbfs(s.data(), int(s.size())), 0.0f, 0.01f);
    for (auto& v : s) v /= 10;
    CHECK_NEAR(block_dbfs(s.data(), int(s.size())), -20.0f, 0.05f);
    // Stride reads one channel of an interleaved pair: a silent right channel does not halve the left.
    std::vector<int16_t> st(3200);
    for (size_t i = 0; i < st.size(); i += 2) st[i] = 3276, st[i + 1] = 0;
    CHECK_NEAR(block_dbfs(st.data(), int(st.size()), 2), -20.0f, 0.05f);
    CHECK(block_dbfs(st.data() + 1, int(st.size()) - 1, 2) == -96.0f);
}

static void meter() {
    CHECK(LevelMeter::normalise(-96) == 0.0f);
    CHECK(LevelMeter::normalise(LevelMeter::kFloorDb) == 0.0f);
    CHECK(LevelMeter::normalise(LevelMeter::kCeilDb) == 1.0f);
    CHECK(LevelMeter::normalise(0) == 1.0f);

    LevelMeter m;
    // Attack: most of the way up within 0.1 s.
    for (int i = 0; i < 6; ++i) m.update(-14, 1.0f / 60);
    CHECK(m.level() > 0.9f);
    // Release: still clearly up after 0.1 s of silence, near zero after a second and a half.
    for (int i = 0; i < 6; ++i) m.update(-96, 1.0f / 60);
    CHECK(m.level() > 0.6f);
    for (int i = 0; i < 84; ++i) m.update(-96, 1.0f / 60);
    CHECK(m.level() < 0.01f);
    // A long frame does not overshoot.
    m.update(-14, 5.0f);
    CHECK(m.level() <= 1.0f && m.level() > 0.999f);
}

int main() {
    header();
    loudness();
    meter();
    return check_report("audio");
}
