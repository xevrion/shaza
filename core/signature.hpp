#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace shaza {

// Shazam's audio fingerprint: the loudest spectral peaks of 16 kHz mono audio, packed the way
// the recognition endpoint expects. Ported to C++ from shazamio-core (MIT, Copyright (c) 2024
// dotX12, https://github.com/shazamio/shazamio-core, fingerprinting/algorithm.rs and
// signature_format.rs at 8e898c8), which in turn follows SongRec's reverse engineering.
//
// It is streaming: feed audio as it arrives and the signature is ready when the last block is in.
// The two spectrum histories take about 2 MB, so on the board they live in PSRAM.
class Fingerprint {
public:
    static constexpr int kRate = 16000;

    struct Peak {
        uint32_t pass;
        uint16_t magnitude, bin;
    };

    Fingerprint();
    void reset();
    void feed(const int16_t* samples, size_t n);
    size_t samples() const { return samples_; }
    // Peaks found so far in each of the four bands: 250-520, 520-1450, 1450-3500, 3500-5500 Hz.
    const std::vector<Peak>& band(int b) const { return bands_[b]; }

    std::vector<uint8_t> encode() const;
    // "data:audio/vnd.shazam.sig;base64,...", what the request carries.
    std::string uri() const;
    // The clip length the request reports, in whole milliseconds.
    uint32_t sample_ms() const { return uint32_t(float(samples_) / float(kRate) * 1000.0f); }

private:
    void block(const int16_t* s);
    void fft();
    void spread();
    void recognise();

    std::vector<int16_t> ring_;
    std::vector<float> window_, frame_, re_, im_, cos_, sin_;
    std::vector<uint16_t> bitrev_;
    std::vector<float> fft_out_, spread_out_;  // 256 rows of 1025 bins each
    std::vector<int16_t> pending_;
    std::vector<Peak> bands_[4];
    size_t ring_index_ = 0, fft_index_ = 0, spread_index_ = 0, samples_ = 0;
    uint32_t spread_done_ = 0;
};

std::string base64(const uint8_t* data, size_t n);
uint32_t crc32(const uint8_t* data, size_t n);

}  // namespace shaza
