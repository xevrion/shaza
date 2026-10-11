#include "recognizer.hpp"

#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "listener.hpp"
#include "shazam.hpp"
#include "song.hpp"
#include "timesync.hpp"

namespace dev {

static const char* TAG = "shazam";

namespace {

constexpr size_t kReplyCap = 128 * 1024, kCoverCap = 160 * 1024;

// One request into buf; returns the byte count, or -1 on failure or a status other than 200.
int request(const char* url, const char* post, uint8_t* buf, size_t cap, int timeout_ms) {
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = timeout_ms;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 2048;
    cfg.user_agent = shaza::shazam::kUserAgent;
    cfg.method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    const int post_len = post ? int(std::strlen(post)) : 0;
    if (post) {
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_header(c, "X-Shazam-Platform", "IPHONE");
        esp_http_client_set_header(c, "X-Shazam-AppVersion", "14.1.0");
        esp_http_client_set_header(c, "Accept", "*/*");
        esp_http_client_set_header(c, "Accept-Language", "en-US");
    }
    int got = -1;
    if (esp_http_client_open(c, post_len) == ESP_OK && (!post || esp_http_client_write(c, post, post_len) == post_len)) {
        esp_http_client_fetch_headers(c);
        const int status = esp_http_client_get_status_code(c);
        if (status == 200) {
            got = 0;
            while (size_t(got) < cap) {
                const int r = esp_http_client_read(c, reinterpret_cast<char*>(buf) + got, int(cap) - got);
                if (r < 0) {
                    got = -1;
                    break;
                }
                if (r == 0) break;
                got += r;
            }
        } else {
            ESP_LOGW(TAG, "%.40s... answered %d", url, status);
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return got;
}

// Apple serves the cover at exactly the size asked for, so the decoded picture is used as is.
bool decode_cover(const uint8_t* jpeg, size_t n, shaza::Picture& out) {
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_handle_t dec = nullptr;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) return false;
    jpeg_dec_io_t io = {};
    io.inbuf = const_cast<uint8_t*>(jpeg);
    io.inbuf_len = int(n);
    jpeg_dec_header_info_t info = {};
    bool ok = false;
    if (jpeg_dec_parse_header(dec, &io, &info) == JPEG_ERR_OK && info.width == shaza::kCoverPx && info.height == shaza::kCoverPx) {
        int len = 0;
        jpeg_dec_get_outbuf_len(dec, &len);
        uint8_t* px = static_cast<uint8_t*>(jpeg_calloc_align(size_t(len), 16));
        if (px) {
            io.outbuf = px;
            if (jpeg_dec_process(dec, &io) == JPEG_ERR_OK) {
                out.w = out.h = shaza::kCoverPx;
                out.px.assign(reinterpret_cast<uint16_t*>(px), reinterpret_cast<uint16_t*>(px) + size_t(out.w) * size_t(out.h));
                ok = true;
            }
            jpeg_free_align(px);
        }
    } else {
        ESP_LOGW(TAG, "cover is %ux%u, not a %d px square jpeg", info.width, info.height, shaza::kCoverPx);
    }
    jpeg_dec_close(dec);
    return ok;
}

shaza::Reply failure(const char* why) {
    shaza::Reply r;
    r.status = shaza::Reply::Status::Error;
    r.error = why;
    return r;
}

}  // namespace

bool Recognizer::begin() {
    // The stack lives in PSRAM: the TLS handshake wants a big one, and internal RAM is what the display
    // needs. Core 1, with the listener, which is idle by the time this runs: on core 0 the handshake
    // outranked the frame loop and froze the animation for up to 300 ms.
    return xTaskCreatePinnedToCoreWithCaps(entry, "shazam", 16384, this, 4, &task_, 1, MALLOC_CAP_SPIRAM) == pdPASS;
}

void Recognizer::ask() {
    if (busy_.exchange(true)) return;
    xTaskNotifyGive(task_);
}

bool Recognizer::take(shaza::Reply& out) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!ready_) return false;
    out = std::move(reply_);
    ready_ = false;
    return true;
}

void Recognizer::entry(void* self) { static_cast<Recognizer*>(self)->run(); }

void Recognizer::run() {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        shaza::Reply r = recognise();
        std::lock_guard<std::mutex> lock(lock_);
        reply_ = std::move(r);
        ready_ = true;
        busy_ = false;
    }
}

shaza::Reply Recognizer::recognise() {
    const int64_t t0 = esp_timer_get_time();
    if (!sync_time()) return failure("clock not set");

    std::string body;
    listener_.with_fingerprint([&](const shaza::Fingerprint& fp) {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        body = shaza::shazam::body(fp, uint64_t(tv.tv_sec) * 1000 + uint64_t(tv.tv_usec / 1000));
        ESP_LOGI(TAG, "fingerprint: %u ms of audio, %u + %u + %u + %u peaks", unsigned(fp.sample_ms()), unsigned(fp.band(0).size()), unsigned(fp.band(1).size()), unsigned(fp.band(2).size()),
                 unsigned(fp.band(3).size()));
    });
    uint8_t ids[32];
    esp_fill_random(ids, sizeof ids);
    const std::string url = std::string("https://") + shaza::shazam::kHost + shaza::shazam::path(shaza::shazam::uuid(ids), shaza::shazam::uuid(ids + 16));

    uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(kReplyCap, MALLOC_CAP_SPIRAM));
    if (!buf) return failure("out of memory");
    const int n = request(url.c_str(), body.c_str(), buf, kReplyCap, 15000);
    const int64_t t1 = esp_timer_get_time();
    const shaza::shazam::Match m = n > 0 ? shaza::shazam::parse(reinterpret_cast<const char*>(buf), size_t(n)) : shaza::shazam::Match();
    heap_caps_free(buf);
    if (n <= 0) return failure("no answer from Shazam");
    if (m.result == shaza::shazam::Match::Result::Bad) return failure("unexpected answer");
    if (m.result == shaza::shazam::Match::Result::None) {
        ESP_LOGI(TAG, "no match (%.1f s)", double(t1 - t0) / 1e6);
        shaza::Reply none;
        none.status = shaza::Reply::Status::None;
        return none;
    }
    ESP_LOGI(TAG, "%s / %s (%.1f s)", m.title.c_str(), m.artist.c_str(), double(t1 - t0) / 1e6);

    shaza::Picture cover;
    if (!m.cover_url.empty()) {
        uint8_t* jpeg = static_cast<uint8_t*>(heap_caps_malloc(kCoverCap, MALLOC_CAP_SPIRAM));
        if (jpeg) {
            const int got = request(shaza::shazam::cover_at(m.cover_url, shaza::kCoverPx).c_str(), nullptr, jpeg, kCoverCap, 10000);
            if (got > 0 && !decode_cover(jpeg, size_t(got), cover)) ESP_LOGW(TAG, "cover did not decode (%d bytes)", got);
            heap_caps_free(jpeg);
        }
    }
    shaza::Reply r = shaza::make_reply(m, std::move(cover));
    ESP_LOGI(TAG, "ready after %.1f s, accent %02x%02x%02x", double(esp_timer_get_time() - t0) / 1e6, r.accent.r, r.accent.g, r.accent.b);
    return r;
}

}  // namespace dev
