#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace shaza {

constexpr int kWavHeader = 44;

// A canonical PCM WAV header for 16 bit samples.
inline void wav_header(uint8_t* out, int rate, int channels, uint32_t data_bytes) {
    auto u32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) out[at + i] = uint8_t(v >> (8 * i)); };
    auto u16 = [&](int at, uint16_t v) { out[at] = uint8_t(v), out[at + 1] = uint8_t(v >> 8); };
    std::memcpy(out, "RIFF", 4);
    u32(4, 36 + data_bytes);
    std::memcpy(out + 8, "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, 1);
    u16(22, uint16_t(channels));
    u32(24, uint32_t(rate));
    u32(28, uint32_t(rate * channels * 2));
    u16(32, uint16_t(channels * 2));
    u16(34, 16);
    std::memcpy(out + 36, "data", 4);
    u32(40, data_bytes);
}

// Loudness of a block in dBFS, from the RMS of every stride-th sample. Silence is -96.
inline float block_dbfs(const int16_t* s, int n, int stride = 1) {
    if (n <= 0) return -96.0f;
    double sum = 0;
    int count = 0;
    for (int i = 0; i < n; i += stride, ++count) sum += double(s[i]) * double(s[i]);
    const double rms = std::sqrt(sum / count) / 32768.0;
    return rms < 1.6e-5 ? -96.0f : float(20.0 * std::log10(rms));
}

// Turns block loudness into a 0..1 level for drawing: quiet rooms sit near 0, a loud song near 1.
// It rises quickly and falls slowly, the way a VU needle does, so beats read as pulses and not
// as flicker.
class LevelMeter {
public:
    static constexpr float kFloorDb = -58.0f;
    static constexpr float kCeilDb = -14.0f;
    static constexpr float kAttackS = 0.03f;
    static constexpr float kReleaseS = 0.28f;

    static float normalise(float db) {
        const float t = (db - kFloorDb) / (kCeilDb - kFloorDb);
        return t < 0 ? 0 : t > 1 ? 1 : t;
    }

    float update(float db, float dt) {
        const float target = normalise(db);
        const float tau = target > level_ ? kAttackS : kReleaseS;
        level_ += (target - level_) * (1.0f - std::exp(-dt / tau));
        return level_;
    }

    float level() const { return level_; }
    void reset() { level_ = 0; }

private:
    float level_ = 0;
};

}  // namespace shaza
