#include "wifi.hpp"

#include <cstring>

#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "secrets.h"

namespace dev {

static const char* TAG = "wifi";

static void reconnect_later(void*) { esp_wifi_connect(); }

void Wifi::on_event(void* self, const char* base, int id, void* data) {
    Wifi* w = static_cast<Wifi*>(self);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto* e = static_cast<wifi_event_sta_disconnected_t*>(data);
        if (w->connected_) w->down_since_us_ = esp_timer_get_time();
        w->connected_ = false;
        ESP_LOGW(TAG, "disconnected, reason %d", e ? int(e->reason) : -1);
        // Try again in two seconds. An event handler must not block, so use a one-shot timer.
        static esp_timer_handle_t retry = nullptr;
        if (!retry) {
            const esp_timer_create_args_t args = {.callback = reconnect_later, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK, .name = "wifi_retry"};
            esp_timer_create(&args, &retry);
        }
        esp_timer_stop(retry);
        esp_timer_start_once(retry, 2'000'000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto* e = static_cast<ip_event_got_ip_t*>(data);
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&e->ip_info.ip));
        w->connected_ = true;
    }
}

int Wifi::seconds_down() const {
    if (connected_) return 0;
    return int((esp_timer_get_time() - down_since_us_) / 1'000'000);
}

void Wifi::reconnect() {
    ESP_LOGW(TAG, "network went quiet, rejoining");
    esp_wifi_disconnect();
}

int Wifi::rssi() const {
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? int(ap.rssi) : 0;
}

void Wifi::begin() {
    down_since_us_ = esp_timer_get_time();
    esp_netif_init();
    // The display driver may already have created the default loop; that is fine.
    const esp_err_t loop = esp_event_loop_create_default();
    if (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE) ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(loop));
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    // The handler takes (base, id, data) as plain values, so adapt ESP-IDF's signature.
    auto trampoline = [](void* arg, esp_event_base_t base, int32_t id, void* data) { Wifi::on_event(arg, base, int(id), data); };
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, trampoline, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, trampoline, this, nullptr));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t config = {};
    std::strncpy(reinterpret_cast<char*>(config.sta.ssid), WIFI_SSID, sizeof config.sta.ssid - 1);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));

    // The campus network uses no anonymous outer identity, so the login is both the outer and inner name.
    ESP_ERROR_CHECK(esp_eap_client_set_identity(reinterpret_cast<const unsigned char*>(WIFI_IDENTITY), int(std::strlen(WIFI_IDENTITY))));
    ESP_ERROR_CHECK(esp_eap_client_set_username(reinterpret_cast<const unsigned char*>(WIFI_IDENTITY), int(std::strlen(WIFI_IDENTITY))));
    ESP_ERROR_CHECK(esp_eap_client_set_password(reinterpret_cast<const unsigned char*>(WIFI_PASSWORD), int(std::strlen(WIFI_PASSWORD))));
    ESP_ERROR_CHECK(esp_wifi_sta_enterprise_enable());

    ESP_ERROR_CHECK(esp_wifi_start());
    // The display is always powered, and a sleeping radio is how a quiet session goes stale.
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "joining %s", WIFI_SSID);
}

}  // namespace dev
