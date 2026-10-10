#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "app.hpp"
#include "check.hpp"

using namespace shaza;

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static std::vector<uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

static void run(App& app, float seconds) {
    for (float t = 0; t < seconds; t += 1.0f / 60) app.update(1.0f / 60);
}

static Reply found(const char* title) {
    Reply r;
    r.status = Reply::Status::Found;
    r.title = title;
    return r;
}

static void parses_real_relay_replies() {
    const char* names[] = {"blinding_lights", "get_lucky", "gurenge", "kesariya", "gurenge_jp", "vichudo", "golden_hour", "bad_guy", "loser", "laufey"};
    for (const char* name : names) {
        const auto data = read_file((std::string("host/fixtures/") + name + ".reply").c_str());
        CHECK(!data.empty());
        Reply r;
        CHECK(parse_reply(data.data(), data.size(), r));
        CHECK(r.status == Reply::Status::Found);
        CHECK(!r.title.empty() && !r.artist.empty());
        CHECK(r.cover.w == 200 && r.cover.h == 200 && r.cover.px.size() == 200u * 200u);
        CHECK(r.title_mask.w > 0 && r.title_mask.cov.size() == size_t(r.title_mask.w) * size_t(r.title_mask.h));
        CHECK(r.artist_mask.cov.size() == size_t(r.artist_mask.w) * size_t(r.artist_mask.h));

        // Every truncation fails cleanly; so does one byte too many.
        bool any_accepted = false;
        for (size_t cut = 0; cut < data.size(); cut += data.size() / 97 + 1) {
            Reply t;
            any_accepted |= parse_reply(data.data(), cut, t);
        }
        CHECK(!any_accepted);
        auto longer = data;
        longer.push_back(0);
        Reply t;
        CHECK(!parse_reply(longer.data(), longer.size(), t));
    }
    const auto jp = read_file("host/fixtures/gurenge_jp.reply");
    Reply r;
    parse_reply(jp.data(), jp.size(), r);
    CHECK(r.title == "\xe7\xb4\x85\xe8\x93\xae\xe8\x8f\xaf");  // 紅蓮華
    CHECK(r.accent.r == 0xba && r.accent.g == 0x90 && r.accent.b == 0x82);
}

static void parses_small_replies() {
    Reply r;
    auto none = bytes("shaza1\nstatus none\n\n");
    CHECK(parse_reply(none.data(), none.size(), r) && r.status == Reply::Status::None);
    auto err = bytes("shaza1\nstatus error\nerror shazam: timeout\nsomething_new 1 2 3\n\n");
    CHECK(parse_reply(err.data(), err.size(), r) && r.status == Reply::Status::Error && r.error == "shazam: timeout");

    const char* bad[] = {
        "",
        "shaza1\nstatus found\n\n",                            // found with no title
        "shaza2\nstatus none\n\n",                             // unknown version
        "shaza1\n\n",                                          // no status
        "shaza1\nstatus maybe\n\n",
        "shaza1\nstatus none\naccent 12345g\n\n",
        "shaza1\nstatus found\ntitle x\ncover 2 2 7\n\n1234567",  // wrong size for 2x2 RGB565
        "shaza1\nstatus found\ntitle x\ncover 0 0 0\n\n",
        "shaza1\nstatus found\ntitle x\ncover 9999 1 19998\n\n",
        "shaza1\nstatus found\ntitle x\ntitle_mask 2 2 4\n\n123",  // short blob
        "shaza1\nstatus none",                                 // no blank line
    };
    for (const char* s : bad) {
        auto b = bytes(s);
        Reply t;
        t.title = "untouched";
        CHECK(!parse_reply(b.data(), b.size(), t));
        CHECK(t.title == "untouched");
    }
}

static void happy_path() {
    App app;
    CHECK(app.phase() == Phase::Ready && app.song() == nullptr && app.since_song() < 0);
    app.tap();
    CHECK(app.phase() == Phase::Listening && app.recording());
    run(app, App::kListenS / 2);
    CHECK_NEAR(app.listen_progress(), 0.5, 0.02);
    CHECK(!app.take_clip());
    run(app, App::kListenS / 2 + 0.05f);
    CHECK(app.phase() == Phase::Thinking && !app.recording());
    CHECK(app.take_clip());
    CHECK(!app.take_clip());  // only once
    app.tap();                // ignored while thinking
    CHECK(app.phase() == Phase::Thinking);
    app.reply(found("Gurenge"));
    CHECK(app.phase() == Phase::Found && app.previous() == Phase::Thinking);
    CHECK(app.song() && app.song()->title == "Gurenge");
    run(app, App::kResultS + 0.1f);
    CHECK(app.phase() == Phase::Ready);
    CHECK(app.song() && app.since_song() > App::kResultS);
}

static void tap_from_a_result_listens_again() {
    App app;
    app.tap();
    run(app, App::kListenS + 0.1f);
    app.reply(found("A"));
    app.tap();
    CHECK(app.phase() == Phase::Listening);
    CHECK(app.song()->title == "A");  // kept until a new one arrives
}

static void cancel_while_listening() {
    App app;
    app.tap();
    run(app, 1.0f);
    app.tap();
    CHECK(app.phase() == Phase::Ready);
    run(app, App::kListenS);
    CHECK(!app.take_clip());
}

static void not_found_and_errors_settle_back() {
    App app;
    app.tap();
    run(app, App::kListenS + 0.1f);
    Reply none;
    none.status = Reply::Status::None;
    app.reply(none);
    CHECK(app.phase() == Phase::NotFound);
    app.tap();  // try again straight away
    CHECK(app.phase() == Phase::Listening);
    run(app, App::kListenS + 0.1f);
    Reply err;
    err.status = Reply::Status::Error;
    err.error = "shazam: timeout";
    app.reply(err);
    CHECK(app.phase() == Phase::Failed && !app.error().empty());
    run(app, App::kNoticeS + 0.1f);
    CHECK(app.phase() == Phase::Ready);
    CHECK(app.song() == nullptr);
}

static void slow_relay_times_out_and_late_reply_is_dropped() {
    App app;
    app.tap();
    run(app, App::kListenS + 0.1f);
    run(app, App::kThinkLimitS + 0.1f);
    CHECK(app.phase() == Phase::Failed);
    app.reply(found("Late"));
    CHECK(app.phase() == Phase::Failed && app.song() == nullptr);
}

static void offline() {
    App app;
    app.set_online(false);
    app.tap();
    CHECK(app.phase() == Phase::Offline && app.error() == "No Wi-Fi");
    run(app, App::kNoticeS + 0.1f);
    CHECK(app.phase() == Phase::Ready);
    app.set_online(true);
    app.tap();
    CHECK(app.phase() == Phase::Listening);
}

static void level_follows_the_microphone() {
    App app;
    for (int i = 0; i < 30; ++i) app.level(-14, 1.0f / 60);
    CHECK(app.level() > 0.95f);
    for (int i = 0; i < 200; ++i) app.level(-90, 1.0f / 60);
    CHECK(app.level() < 0.01f);
}

int main() {
    parses_real_relay_replies();
    parses_small_replies();
    happy_path();
    tap_from_a_result_listens_again();
    cancel_while_listening();
    not_found_and_errors_settle_back();
    slow_relay_times_out_and_late_reply_is_dropped();
    offline();
    level_follows_the_microphone();
    return check_report("app");
}
