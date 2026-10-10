#include "app.hpp"

namespace shaza {

const char* phase_name(Phase p) {
    switch (p) {
    case Phase::Ready: return "ready";
    case Phase::Listening: return "listening";
    case Phase::Thinking: return "thinking";
    case Phase::Found: return "found";
    case Phase::NotFound: return "not found";
    case Phase::Offline: return "offline";
    case Phase::Failed: return "failed";
    }
    return "?";
}

void App::go(Phase p) {
    previous_ = phase_, phase_ = p, t_ = 0;
}

void App::tap() {
    switch (phase_) {
    case Phase::Listening:
        // A second tap means "never mind".
        clip_ready_ = false;
        go(Phase::Ready);
        break;
    case Phase::Thinking:
        break;
    default:
        if (!online_) {
            error_ = "No Wi-Fi";
            go(Phase::Offline);
        } else {
            go(Phase::Listening);
        }
    }
}

void App::update(float dt) {
    t_ += dt;
    since_song_ += dt;
    switch (phase_) {
    case Phase::Listening:
        if (t_ >= kListenS) {
            clip_ready_ = true;
            go(Phase::Thinking);
        }
        break;
    case Phase::Thinking:
        if (t_ >= kThinkLimitS) {
            error_ = "No answer";
            go(Phase::Failed);
        }
        break;
    case Phase::Found:
        if (t_ >= kResultS) go(Phase::Ready);
        break;
    case Phase::NotFound:
    case Phase::Offline:
    case Phase::Failed:
        if (t_ >= kNoticeS) go(Phase::Ready);
        break;
    case Phase::Ready:
        break;
    }
}

bool App::take_clip() {
    const bool ready = clip_ready_;
    clip_ready_ = false;
    return ready;
}

void App::reply(Reply r) {
    // A late answer after a timeout or a cancel is dropped rather than popping up out of nowhere.
    if (phase_ != Phase::Thinking) return;
    switch (r.status) {
    case Reply::Status::Found:
        song_ = std::make_shared<Reply>(std::move(r));
        since_song_ = 0;
        go(Phase::Found);
        break;
    case Reply::Status::None:
        go(Phase::NotFound);
        break;
    case Reply::Status::Error:
        error_ = r.error.empty() ? "Something went wrong" : "Couldn't reach the service";
        go(Phase::Failed);
        break;
    }
}

}  // namespace shaza
