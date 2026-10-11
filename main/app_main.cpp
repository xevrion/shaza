#include <algorithm>

#include "app.hpp"
#include "bsp/esp32_s3_touch_amoled_1_75c.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "listener.hpp"
#include "look.hpp"
#include "mictest.hpp"
#include "nvs_flash.h"
#include "recognizer.hpp"
#include "rk/direct_panel.hpp"
#include "rk/display_lock.hpp"
#include "rk/gesture.hpp"
#include "wifi.hpp"

static const char* TAG = "shaza";

constexpr int kBrightness = 85;
constexpr int kLogEvery = 300;  // frames, about five seconds

extern "C" void app_main() {
#if SHAZA_MICTEST
    dev::run_mictest();
    return;
#endif
    if (!bsp_display_start()) {
        ESP_LOGE(TAG, "display failed to start");
        return;
    }
    bsp_display_brightness_set(kBrightness);
    // LVGL only brings the touch screen. Pixels go to the panel directly.
    rk::DirectPanel panel;
    if (!panel.begin()) {
        ESP_LOGE(TAG, "direct panel failed to start");
        return;
    }

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    static dev::Wifi wifi;
    wifi.begin();

    static dev::Listener listener;
    if (!listener.begin()) ESP_LOGE(TAG, "microphones failed to start");
    static dev::Recognizer recognizer(listener);
    if (!recognizer.begin()) ESP_LOGE(TAG, "recognizer task failed to start");

    static shaza::App app;
    auto look = shaza::make_look(0);
    lv_indev_t* touch = bsp_display_get_input_dev();
    rk::Gesture gesture;

    int64_t last = esp_timer_get_time();
    const int64_t start = last;
    shaza::Phase phase = app.phase();
    int frame = 0, sent = 0;
    int64_t render_sum = 0, render_max = 0, send_sum = 0, send_max = 0, frame_max = 0, px_sum = 0, px_max = 0;

    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        const int64_t now = esp_timer_get_time();
        const float dt = std::min(0.1f, float(now - last) / 1e6f);
        frame_max = std::max(frame_max, now - last);
        last = now;

        bool pressed = false;
        lv_point_t point = {0, 0};
        if (touch) {
            rk::DisplayLock lock;
            pressed = lv_indev_get_state(touch) == LV_INDEV_STATE_PRESSED;
            lv_indev_get_point(touch, &point);
        }
        if (gesture.update(pressed, point.x, point.y, dt) == rk::Tap::Short) app.tap();
#if SHAZA_AUTOTAP
        const bool resting = app.phase() == shaza::Phase::Ready || app.phase() == shaza::Phase::Found || app.phase() == shaza::Phase::NotFound || app.phase() == shaza::Phase::Failed;
        if (resting && app.phase_time() > 20.0f) app.tap();
#endif

        app.set_online(wifi.connected());
        app.level(listener.latest_dbfs(), dt);
        app.update(dt);

        // Frame times are logged per phase, every kLogEvery frames and whenever the phase changes,
        // so a slow listening animation is not averaged away by a still result screen.
        const bool changed = app.phase() != phase;
        if (changed || (frame > 0 && frame % kLogEvery == 0)) {
            const double n = frame ? frame : 1;
            ESP_LOGI(TAG, "%s, %d frames | render avg %.1f worst %.1f ms, send avg %.1f worst %.1f ms, pixels avg %lld worst %lld | %d drew something | longest frame %.1f ms | heap %u",
                     shaza::phase_name(phase), frame, double(render_sum) / n / 1000.0, double(render_max) / 1000.0, double(send_sum) / n / 1000.0, double(send_max) / 1000.0,
                     int64_t(double(px_sum) / n), px_max, sent, double(frame_max) / 1000.0, unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
            render_sum = render_max = send_sum = send_max = frame_max = px_sum = px_max = 0, sent = frame = 0;
        }
        if (changed) {
            if (app.phase() == shaza::Phase::Listening) listener.start();
            else if (phase == shaza::Phase::Listening) listener.stop();
            phase = app.phase();
        }
        if (app.take_clip()) recognizer.ask();
        shaza::Reply reply;
        if (recognizer.take(reply)) app.reply(std::move(reply));

        const int64_t t0 = esp_timer_get_time();
        look->frame(panel.surface(), app, float(now - start) / 1e6f);
        rk::Rect zones[shaza::Look::kMaxItems * 2];
        const int nz = shaza::last_zones(zones, shaza::Look::kMaxItems * 2);
        int64_t px = 0;
        for (int i = 0; i < nz; ++i) px += int64_t(zones[i].x1 - zones[i].x0 + 1) * (zones[i].y1 - zones[i].y0 + 1);
        const int64_t t1 = esp_timer_get_time();
        if (panel.present()) ++sent;
        const int64_t t2 = esp_timer_get_time();
        render_sum += t1 - t0, send_sum += t2 - t1, px_sum += px;
        render_max = std::max(render_max, t1 - t0), send_max = std::max(send_max, t2 - t1), px_max = std::max(px_max, px);
        ++frame;
        // After a slow frame, carry on from now rather than racing through the frames that were missed.
        if (xTaskGetTickCount() - wake > pdMS_TO_TICKS(32)) wake = xTaskGetTickCount();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(16));
    }
}
