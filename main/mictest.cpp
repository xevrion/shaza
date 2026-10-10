// Microphone bring-up: prints the level of both channels a few times a second, draws them as two
// bars, and records four seconds at boot and on every tap, dumped over the console as base64
// between SHAZA-WAV and SHAZA-END lines. tools/grab_wav.py turns that back into a WAV.

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "audio.hpp"
#include "bsp/esp32_s3_touch_amoled_1_75c.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mic.hpp"
#include "mictest.hpp"
#include "rk/direct_panel.hpp"
#include "rk/display_lock.hpp"
#include "rk/gesture.hpp"

namespace dev {

static const char* TAG = "mictest";

constexpr int kRate = 16000, kChannels = 2;
constexpr int kBlock = 320;  // 20 ms
constexpr int kRecordFrames = kRate * 4;
using shaza::kWavHeader;

static void dump_base64(const uint8_t* p, size_t n) {
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char line[100];
    int len = 0;
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = uint32_t(p[i]) << 16 | (i + 1 < n ? uint32_t(p[i + 1]) << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        line[len++] = abc[(v >> 18) & 63];
        line[len++] = abc[(v >> 12) & 63];
        line[len++] = i + 1 < n ? abc[(v >> 6) & 63] : '=';
        line[len++] = i + 2 < n ? abc[v & 63] : '=';
        if (len >= 96) {
            line[len++] = '\n';
            fwrite(line, 1, len, stdout);
            len = 0;
        }
    }
    if (len) line[len++] = '\n', fwrite(line, 1, len, stdout);
}

static void draw_bar(rk::Surface& sf, int y, float level, uint16_t colour) {
    constexpr int kMaxW = 300, kH = 28;
    const int w = std::max(2, int(level * kMaxW)) & ~1;
    const int x = (sf.width() - kMaxW) / 2;
    sf.fill_rect(x, y, w, kH, colour);
    sf.fill_rect(x + w, y, kMaxW - w, kH, rk::rgb565(24, 24, 24));
}

void run_mictest() {
    if (!bsp_display_start()) return;
    bsp_display_brightness_set(60);
    rk::DirectPanel panel;
    if (!panel.begin()) return;

    Mic mic;
    if (!mic.begin(kRate, kChannels)) return;

    const size_t wav_bytes = kWavHeader + size_t(kRecordFrames) * kChannels * 2;
    uint8_t* wav = static_cast<uint8_t*>(heap_caps_malloc(wav_bytes, MALLOC_CAP_SPIRAM));
    int16_t* rec = reinterpret_cast<int16_t*>(wav + kWavHeader);
    int16_t block[kBlock * kChannels];
    shaza::LevelMeter left, right;
    rk::Gesture gesture;
    lv_indev_t* touch = bsp_display_get_input_dev();

    int recorded = -1;  // frames so far, or -1 when not recording
    int64_t boot_record_at = esp_timer_get_time() + 1'500'000, next_log = 0;
    float worst_l = -96, worst_r = -96;
    int zero_blocks = 0, blocks = 0, peak = 0;

    for (;;) {
        if (!mic.read(block, kBlock)) {
            ESP_LOGE(TAG, "read failed");
            continue;
        }
        const int64_t now = esp_timer_get_time();
        bool all_zero = true;
        for (int i = 0; i < kBlock * kChannels; ++i) {
            if (block[i]) all_zero = false;
            peak = std::max(peak, std::abs(int(block[i])));
        }
        zero_blocks += all_zero, ++blocks;
        const float db_l = shaza::block_dbfs(block, kBlock * kChannels, 2);
        const float db_r = shaza::block_dbfs(block + 1, kBlock * kChannels - 1, 2);
        worst_l = std::max(worst_l, db_l), worst_r = std::max(worst_r, db_r);
        left.update(db_l, 0.02f), right.update(db_r, 0.02f);

        bool pressed = false;
        lv_point_t pt = {0, 0};
        if (touch) {
            rk::DisplayLock lock;
            pressed = lv_indev_get_state(touch) == LV_INDEV_STATE_PRESSED;
            lv_indev_get_point(touch, &pt);
        }
        const bool tap = gesture.update(pressed, pt.x, pt.y, 0.02f) == rk::Tap::Short;
        if (recorded < 0 && (tap || (boot_record_at && now >= boot_record_at))) {
            boot_record_at = 0, recorded = 0;
            ESP_LOGI(TAG, "recording 4 s");
        }
        if (recorded >= 0) {
            const int n = std::min(kBlock, kRecordFrames - recorded);
            std::copy(block, block + n * kChannels, rec + size_t(recorded) * kChannels);
            recorded += n;
            if (recorded == kRecordFrames) {
                shaza::wav_header(wav, kRate, kChannels, uint32_t(wav_bytes - kWavHeader));
                fflush(stdout);
                printf("\nSHAZA-WAV %d %d %u\n", kRate, kChannels, unsigned(wav_bytes));
                dump_base64(wav, wav_bytes);
                printf("SHAZA-END\n");
                fflush(stdout);
                recorded = -1;
            }
        }

        const uint16_t rec_colour = recorded >= 0 ? rk::rgb565(255, 80, 70) : rk::rgb565(230, 230, 230);
        draw_bar(panel.surface(), 196, left.level(), all_zero ? rk::rgb565(255, 0, 0) : rec_colour);
        draw_bar(panel.surface(), 242, right.level(), all_zero ? rk::rgb565(255, 0, 0) : rec_colour);
        panel.present();

        if (now >= next_log) {
            next_log = now + 250'000;
            ESP_LOGI(TAG, "level L %6.1f R %6.1f dBFS | peak %5d | all-zero blocks %d of %d", double(worst_l), double(worst_r), peak, zero_blocks, blocks);
            worst_l = worst_r = -96, zero_blocks = blocks = peak = 0;
        }
    }
}

}  // namespace dev
