#include "listener.hpp"

#include <new>

#include "audio.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace dev {

static const char* TAG = "listen";

constexpr int kBlock = 320;

bool Listener::begin() {
    if (!mic_.begin(shaza::Fingerprint::kRate, 2)) return false;
    // The fingerprint's spectrum history is about 2 MB; its vectors go to PSRAM on their own
    // (allocations over 4 KB do), and the object itself is put there too.
    void* mem = heap_caps_malloc(sizeof(shaza::Fingerprint), MALLOC_CAP_SPIRAM);
    if (!mem) return false;
    fp_ = new (mem) shaza::Fingerprint();
    // Core 1, away from Wi-Fi; the FFTs take a good part of it while listening.
    return xTaskCreatePinnedToCoreWithCaps(entry, "listen", 8192, this, 6, nullptr, 1, MALLOC_CAP_SPIRAM) == pdPASS;
}

void Listener::start() {
    restart_ = true;
    capturing_ = true;
}

void Listener::stop() { capturing_ = false; }

void Listener::entry(void* self) { static_cast<Listener*>(self)->run(); }

void Listener::run() {
    static int16_t stereo[kBlock * 2];
    static int16_t left[kBlock];
    for (;;) {
        if (!mic_.read(stereo, kBlock)) {
            ESP_LOGW(TAG, "read failed");
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        for (int i = 0; i < kBlock; ++i) left[i] = stereo[i * 2];
        dbfs_ = shaza::block_dbfs(left, kBlock);
        if (capturing_) {
            std::lock_guard<std::mutex> lock(lock_);
            if (restart_.exchange(false)) fp_->reset();
            fp_->feed(left, kBlock);
        }
    }
}

}  // namespace dev
