#pragma once

#include <memory>

#include "audio.hpp"
#include "reply.hpp"

namespace shaza {

enum class Phase { Ready, Listening, Thinking, Found, NotFound, Offline, Failed };

const char* phase_name(Phase p);

// The whole product as a state machine, with no clocks, audio or network of its own. The board and
// the simulator feed it taps, microphone levels, the network state and the relay's reply, and ask it
// what to show and when a clip is ready to send.
class App {
public:
    static constexpr float kListenS = 5.0f;    // how much of the room is recorded
    static constexpr float kThinkLimitS = 20.0f;
    static constexpr float kNoticeS = 7.0f;    // how long "no match" and errors stay up
    static constexpr float kResultS = 90.0f;   // then the result settles back to the ready orb

    void tap();
    void set_online(bool up) { online_ = up; }
    void level(float dbfs, float dt) { meter_.update(dbfs, dt); }
    void update(float dt);
    // True once, when a recording has just finished: the caller sends it and later calls reply().
    bool take_clip();
    void reply(Reply r);

    Phase phase() const { return phase_; }
    Phase previous() const { return previous_; }
    float phase_time() const { return t_; }
    float level() const { return meter_.level(); }
    bool recording() const { return phase_ == Phase::Listening; }
    float listen_progress() const { return phase_ == Phase::Listening ? t_ / kListenS : 0.0f; }
    // The last song found, or null. It outlives the Found phase so Ready can mention it.
    const Reply* song() const { return song_.get(); }
    // Seconds since the last song was found, or a negative number if none has been.
    float since_song() const { return song_ ? since_song_ : -1.0f; }
    const std::string& error() const { return error_; }

private:
    void go(Phase p);

    Phase phase_ = Phase::Ready, previous_ = Phase::Ready;
    float t_ = 0, since_song_ = 0;
    bool online_ = true, clip_ready_ = false;
    LevelMeter meter_;
    std::shared_ptr<Reply> song_;
    std::string error_;
};

}  // namespace shaza
