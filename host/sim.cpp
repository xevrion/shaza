// The simulator: the real core on a laptop.
//
//   ./host/build/sim                 listens through the laptop's microphone and asks the relay
//   ./host/build/sim --fake          made-up levels and the saved replies in host/fixtures, no network
//   ./host/build/sim --shots dir     every state of every look as PPM, with how much each frame sends
//   ./host/build/sim --gallery dir   the result screen for every saved reply, in every look
//   ./host/build/sim --frames dir N  a scripted run of look N as PPM frames, for a GIF
//
// Keys: click or space taps the screen, D changes look, W toggles Wi-Fi, Esc quits.

#include <SDL.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app.hpp"
#include "look.hpp"

using namespace shaza;

namespace {

constexpr int kW = 466;
constexpr int kRate = 16000, kBlock = 320;
const char* kFixtures[] = {"gurenge_jp", "kesariya", "get_lucky", "blinding_lights", "vichudo", "golden_hour", "bad_guy", "loser", "laufey", "gurenge"};
constexpr int kFixtureCount = int(sizeof kFixtures / sizeof kFixtures[0]);

struct Screen {
    std::vector<uint16_t> px = std::vector<uint16_t>(kW * kW, 0);
    rk::Surface sf{reinterpret_cast<uint8_t*>(px.data()), kW, kW, kW * 2};
    // What the panel would be sent for the frame just drawn.
    long take_sent() {
        rk::Rect r[rk::Surface::kMaxDirty];
        const int n = sf.take_dirty_rects(r, rk::Surface::kMaxDirty);
        long sum = 0;
        for (int i = 0; i < n; ++i) sum += long(r[i].x1 - r[i].x0 + 1) * long(r[i].y1 - r[i].y0 + 1);
        return sum;
    }
};

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

bool load_fixture(int i, Reply& r) {
    const auto data = read_file(std::string("host/fixtures/") + kFixtures[i % kFixtureCount] + ".reply");
    return parse_reply(data.data(), data.size(), r);
}

void write_ppm(const Screen& s, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", kW, kW);
    for (int y = 0; y < kW; ++y) {
        for (int x = 0; x < kW; ++x) {
            const float dx = x + 0.5f - kW / 2.0f, dy = y + 0.5f - kW / 2.0f;
            // The panel is round; outside it is the bezel.
            const float edge = std::sqrt(dx * dx + dy * dy) - kW / 2.0f;
            const Rgb c = unpack(s.px[y * kW + x]);
            const float k = clamp01(0.5f - edge);
            const uint8_t bez = 22;
            const uint8_t rgb[3] = {uint8_t(c.r * k + bez * (1 - k)), uint8_t(c.g * k + bez * (1 - k)), uint8_t(c.b * k + bez * (1 - k))};
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
}

// A loudness that moves like music: a kick on every beat, some hats, a little noise.
float fake_db(float t) {
    const float beat = std::fmod(t * 2.0f, 1.0f), half = std::fmod(t * 4.0f + 0.5f, 1.0f);
    const float env = std::exp(-beat * 6.0f) + 0.4f * std::exp(-half * 9.0f);
    const float wobble = 0.5f + 0.5f * std::sin(t * 1.3f) * std::sin(t * 0.37f);
    return -40.0f + 22.0f * std::min(1.0f, env) + 6.0f * wobble;
}

// Sends the clip to the relay with curl and leaves the reply for the main loop.
class Relay {
public:
    void send(std::vector<uint8_t> wav) {
        if (busy_) return;
        busy_ = true;
        std::thread([this, wav = std::move(wav)] {
            const std::string dir = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp");
            const std::string in = dir + "/shaza-clip.wav", out = dir + "/shaza-reply.bin";
            std::ofstream(in, std::ios::binary).write(reinterpret_cast<const char*>(wav.data()), std::streamsize(wav.size()));
            const char* home = std::getenv("HOME");
            const std::string cmd = "curl -s -m 25 -o '" + out + "' --data-binary @'" + in + "' -H \"X-Shaza-Token: $(cat '" + std::string(home ? home : "") +
                                    "/.config/shaza/token')\" 'http://127.0.0.1:8788/listen?art=200&tw=360&ts=34&aw=320&as=24'";
            const int rc = std::system(cmd.c_str());
            Reply r;
            const auto data = read_file(out);
            if (rc != 0 || !parse_reply(data.data(), data.size(), r)) {
                r = Reply();
                r.status = Reply::Status::Error;
                r.error = "relay unreachable";
                std::printf("relay: no usable reply (curl exit %d, %zu bytes). Is it running? uv run relay/shaza_relay.py\n", rc, data.size());
            }
            std::lock_guard<std::mutex> lock(m_);
            reply_ = std::move(r);
            ready_ = true;
            busy_ = false;
        }).detach();
    }
    bool take(Reply& r) {
        std::lock_guard<std::mutex> lock(m_);
        if (!ready_) return false;
        r = std::move(reply_);
        ready_ = false;
        return true;
    }

private:
    std::mutex m_;
    Reply reply_;
    bool ready_ = false;
    std::atomic<bool> busy_{false};
};

struct Stats {
    long frames = 0, sent_sum = 0, sent_max = 0, busy = 0;
    void add(long sent) {
        ++frames, sent_sum += sent, sent_max = std::max(sent_max, sent);
        busy += sent > 0;
    }
};

// One scripted run through every state. at(t) drives the app; returns false when done.
struct Script {
    App app;
    int fixture = 4;  // Vichudo, the first song the simulator recognised through a real microphone
    float t = 0;
    int step = 0;
    void advance(float dt) {
        t += dt;
        app.level(app.recording() ? fake_db(t) : -80.0f, dt);
        app.update(dt);
        auto at = [&](float when) { return step == int(when * 1000) ? false : t >= when; };
        (void)at;
        if (step == 0 && t >= 1.5f) app.tap(), step = 1;                                       // listen
        if (step == 1 && app.take_clip()) step = 2;
        if (step == 2 && app.phase_time() >= 1.4f) {                                          // thinking, then found
            Reply r;
            load_fixture(fixture, r);
            app.reply(std::move(r));
            step = 3;
        }
        if (step == 3 && app.phase_time() >= 3.5f) app.tap(), step = 4;                       // listen again
        if (step == 4 && app.take_clip()) step = 5;
        if (step == 5 && app.phase_time() >= 1.0f) {
            Reply none;
            none.status = Reply::Status::None;
            app.reply(none);
            step = 6;
        }
        if (step == 6 && app.phase_time() >= App::kNoticeS + 2.5f) step = 7;                  // back to ready
    }
    bool done() const { return step == 7; }
};

int run_shots(const std::string& dir) {
    struct Shot {
        const char* name;
        float at;
    };
    // Times in the script above where each state is at its most typical.
    const Shot shots[] = {{"1_ready", 1.0f}, {"2_listening", 4.3f}, {"3_thinking", 7.6f}, {"4_found", 10.8f}, {"5_not_found", 22.0f}, {"6_ready_after", 31.0f}};
    for (int look_i = 0; look_i < kLooks; ++look_i) {
        auto look = make_look(look_i);
        Screen screen;
        Script s;
        Stats per_phase[7];
        size_t next = 0;
        const float dt = 1.0f / 60;
        long render_us = 0, render_max = 0;
        while (!s.done()) {
            s.advance(dt);
            const auto t0 = std::chrono::steady_clock::now();
            look->frame(screen.sf, s.app, s.t);
            const long us = long(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
            render_us += us, render_max = std::max(render_max, us);
            const long sent = screen.take_sent();
            if (s.t > 0.1f) per_phase[int(s.app.phase())].add(sent);
            if (next < std::size(shots) && s.t >= shots[next].at) {
                write_ppm(screen, dir + "/" + look->name() + "_" + shots[next].name + ".ppm");
                ++next;
            }
        }
        std::printf("%s: laptop render avg %.2f ms, worst %.2f ms\n", look->name(), render_us / 1000.0 / per_phase[0].frames, render_max / 1000.0);
        for (int p = 0; p < 7; ++p) {
            const Stats& st = per_phase[p];
            if (!st.frames) continue;
            std::printf("  %-10s pixels sent per frame: avg %6ld, worst %6ld  (%ld of %ld frames send anything)\n", phase_name(Phase(p)), st.sent_sum / st.frames, st.sent_max, st.busy, st.frames);
        }
    }
    return 0;
}

// The result screen for every saved reply, in every look.
int run_gallery(const std::string& dir) {
    for (int look_i = 0; look_i < kLooks; ++look_i) {
        for (int f = 0; f < kFixtureCount; ++f) {
            auto look = make_look(look_i);
            Screen screen;
            App app;
            app.tap();
            for (int i = 0; i < 60 * 6; ++i) app.update(1.0f / 60);
            Reply r;
            load_fixture(f, r);
            app.reply(std::move(r));
            for (int i = 0; i < 60 * 2; ++i) app.update(1.0f / 60);
            look->frame(screen.sf, app, 8.0f);
            write_ppm(screen, dir + "/" + look->name() + "_" + kFixtures[f] + ".ppm");
        }
    }
    return 0;
}

int run_frames(const std::string& dir, int look_i) {
    auto look = make_look(look_i);
    Screen screen;
    Script s;
    int n = 0;
    const float dt = 1.0f / 30;
    while (!s.done() && s.t < 16.5f) {
        for (int k = 0; k < 2; ++k) s.advance(dt / 2);
        look->frame(screen.sf, s.app, s.t);
        screen.take_sent();
        char name[64];
        std::snprintf(name, sizeof name, "/f%04d.ppm", n++);
        write_ppm(screen, dir + name);
    }
    std::printf("%d frames\n", n);
    return 0;
}

int run_window(bool fake) {
    if (SDL_Init(SDL_INIT_VIDEO | (fake ? 0 : SDL_INIT_AUDIO)) != 0) {
        std::printf("SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("shaza", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kW, kW, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, kW, kW);
    // The bezel: transparent inside the circle, dark grey outside.
    SDL_Texture* bezel = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, kW, kW);
    {
        std::vector<uint32_t> px(kW * kW);
        for (int y = 0; y < kW; ++y)
            for (int x = 0; x < kW; ++x) {
                const float dx = x + 0.5f - kW / 2.0f, dy = y + 0.5f - kW / 2.0f;
                const float a = clamp01(std::sqrt(dx * dx + dy * dy) - kW / 2.0f + 0.5f);
                px[y * kW + x] = uint32_t(a * 255) << 24 | 0x161616;
            }
        SDL_UpdateTexture(bezel, nullptr, px.data(), kW * 4);
        SDL_SetTextureBlendMode(bezel, SDL_BLENDMODE_BLEND);
    }

    SDL_AudioDeviceID mic = 0;
    if (!fake) {
        SDL_AudioSpec want = {}, have = {};
        want.freq = kRate, want.format = AUDIO_S16SYS, want.channels = 1, want.samples = kBlock;
        mic = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0);
        if (!mic) {
            std::printf("no microphone (%s), falling back to --fake\n", SDL_GetError());
            fake = true;
        } else {
            std::printf("microphone open: %s, %d Hz\n", SDL_GetAudioDeviceName(0, 1) ? SDL_GetAudioDeviceName(0, 1) : "default", have.freq);
            SDL_PauseAudioDevice(mic, 0);
        }
    }

    App app;
    Screen screen;
    int look_i = 0, fixture = 0;
    auto look = make_look(look_i);
    Relay relay;
    std::vector<int16_t> clip, pending;
    bool online = true;
    float now = 0, fake_wait = -1;
    Stats stats;
    auto last = std::chrono::steady_clock::now();

    for (bool running = true; running;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_MOUSEBUTTONDOWN) app.tap();
            else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_SPACE: app.tap(); break;
                case SDLK_d:
                    look_i = (look_i + 1) % kLooks;
                    look = make_look(look_i);
                    std::printf("look: %s\n", look->name());
                    break;
                case SDLK_w:
                    online = !online;
                    app.set_online(online);
                    std::printf("wi-fi %s\n", online ? "on" : "off");
                    break;
                }
            }
        }
        const auto t = std::chrono::steady_clock::now();
        const float dt = std::min(0.1f, std::chrono::duration<float>(t - last).count());
        last = t, now += dt;

        if (fake) {
            app.level(app.recording() ? fake_db(now) : -80.0f, dt);
        } else {
            int16_t buf[4096];
            for (Uint32 got; (got = SDL_DequeueAudio(mic, buf, sizeof buf)) > 0;) pending.insert(pending.end(), buf, buf + got / 2);
            size_t used = 0;
            for (; used + kBlock <= pending.size(); used += kBlock) {
                app.level(block_dbfs(pending.data() + used, kBlock), float(kBlock) / kRate);
                if (app.recording()) clip.insert(clip.end(), pending.begin() + long(used), pending.begin() + long(used + kBlock));
            }
            pending.erase(pending.begin(), pending.begin() + long(used));
        }
        app.update(dt);
        if (app.phase() == Phase::Listening && app.phase_time() < dt * 1.5f) clip.clear();

        if (app.take_clip()) {
            if (fake) {
                fake_wait = 1.2f;
            } else {
                std::vector<uint8_t> wav(kWavHeader + clip.size() * 2);
                wav_header(wav.data(), kRate, 1, uint32_t(clip.size() * 2));
                std::memcpy(wav.data() + kWavHeader, clip.data(), clip.size() * 2);
                std::printf("sending %.1f s to the relay (rms %.0f dBFS)\n", clip.size() / float(kRate), double(block_dbfs(clip.data(), int(clip.size()))));
                relay.send(std::move(wav));
            }
        }
        if (fake && fake_wait > 0 && (fake_wait -= dt) <= 0) {
            Reply r;
            // Every third try is a miss, so the not-found state gets seen too.
            if (++fixture % 3 == 0) r.status = Reply::Status::None;
            else load_fixture(fixture, r);
            app.reply(std::move(r));
        }
        Reply r;
        if (relay.take(r)) {
            std::printf("reply: %s %s%s%s\n", r.status == Reply::Status::Found ? "found" : r.status == Reply::Status::None ? "no match" : "error", r.title.c_str(),
                        r.artist.empty() ? "" : " / ", r.artist.c_str());
            app.reply(std::move(r));
        }

        look->frame(screen.sf, app, now);
        stats.add(screen.take_sent());
        if (stats.frames % 120 == 0) {
            char title[160];
            std::snprintf(title, sizeof title, "shaza  |  %s  |  %s  |  sent avg %ld px/frame, worst %ld", look->name(), phase_name(app.phase()), stats.sent_sum / stats.frames, stats.sent_max);
            SDL_SetWindowTitle(win, title);
            stats = Stats();
        }
        SDL_UpdateTexture(tex, nullptr, screen.px.data(), kW * 2);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderCopy(ren, bezel, nullptr, nullptr);
        SDL_RenderPresent(ren);
    }
    if (mic) SDL_CloseAudioDevice(mic);
    SDL_Quit();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    bool fake = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--fake") fake = true;
        else if (a == "--shots" && i + 1 < argc) return run_shots(argv[i + 1]);
        else if (a == "--gallery" && i + 1 < argc) return run_gallery(argv[i + 1]);
        else if (a == "--frames" && i + 1 < argc) return run_frames(argv[i + 1], i + 2 < argc ? std::atoi(argv[i + 2]) : 0);
    }
    return run_window(fake);
}
