#include "timesync.hpp"

#include <cstring>
#include <ctime>
#include <strings.h>
#include <sys/time.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

namespace dev {

static const char* TAG = "time";

static bool plausible() { return time(nullptr) > 1'700'000'000; }

static esp_err_t on_header(esp_http_client_event_t* e) {
    if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && strcasecmp(e->header_key, "Date") == 0) {
        // "Sun, 11 Oct 2026 02:15:33 GMT"
        struct tm tm = {};
        if (strptime(e->header_value, "%a, %d %b %Y %H:%M:%S", &tm)) {
            // The board's time zone is UTC, so mktime reads this as UTC too.
            struct timeval tv = {mktime(&tm), 0};
            settimeofday(&tv, nullptr);
        }
    }
    return ESP_OK;
}

static bool from_http_date() {
    // Plain HTTP on purpose: it needs no certificate, and the response's Date header is all we want.
    esp_http_client_config_t cfg = {};
    cfg.url = "http://xevrion.dev/";
    cfg.method = HTTP_METHOD_HEAD;
    cfg.timeout_ms = 8000;
    cfg.disable_auto_redirect = true;
    cfg.event_handler = on_header;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    const esp_err_t err = esp_http_client_perform(c);
    esp_http_client_cleanup(c);
    if (err != ESP_OK) ESP_LOGW(TAG, "http date failed: %s", esp_err_to_name(err));
    return plausible();
}

bool sync_time() {
    if (plausible()) return true;

    static bool started = false;
    if (!started) {
        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if (esp_netif_sntp_init(&cfg) == ESP_OK) started = true;
    }
    if (started && esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000)) == ESP_OK && plausible()) {
        ESP_LOGI(TAG, "clock set from ntp");
        return true;
    }
    if (from_http_date()) {
        ESP_LOGI(TAG, "clock set from the http Date header");
        return true;
    }
    return false;
}

}  // namespace dev
