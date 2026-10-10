#pragma once

#include <cstdint>

#include "esp_codec_dev.h"

namespace dev {

// The two microphones behind the ES7210, opened through the board package so its pins and MCLK
// are used as they are. Samples come out interleaved, left then right.
class Mic {
public:
    bool begin(int rate = 16000, int channels = 2, float gain_db = 30.0f);
    // Blocks until frames frames have been read into out.
    bool read(int16_t* out, int frames);
    int rate() const { return rate_; }
    int channels() const { return channels_; }

private:
    esp_codec_dev_handle_t dev_ = nullptr;
    int rate_ = 0, channels_ = 0;
};

}  // namespace dev
