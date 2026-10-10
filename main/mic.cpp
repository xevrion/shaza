#include "mic.hpp"

#include "bsp/esp32_s3_touch_amoled_1_75c.h"
#include "esp_log.h"

namespace dev {

static const char* TAG = "mic";

bool Mic::begin(int rate, int channels, float gain_db) {
    dev_ = bsp_audio_codec_microphone_init();
    if (!dev_) {
        ESP_LOGE(TAG, "codec init failed");
        return false;
    }
    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16;
    fs.channel = uint8_t(channels);
    fs.sample_rate = uint32_t(rate);
    if (esp_codec_dev_open(dev_, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "open failed");
        return false;
    }
    esp_codec_dev_set_in_gain(dev_, gain_db);
    rate_ = rate, channels_ = channels;
    ESP_LOGI(TAG, "%d Hz, %d channels, gain %.0f dB", rate, channels, double(gain_db));
    return true;
}

bool Mic::read(int16_t* out, int frames) {
    return esp_codec_dev_read(dev_, out, frames * channels_ * int(sizeof(int16_t))) == ESP_CODEC_DEV_OK;
}

}  // namespace dev
