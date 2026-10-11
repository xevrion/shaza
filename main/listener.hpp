#pragma once

#include <atomic>
#include <mutex>

#include "mic.hpp"
#include "signature.hpp"

namespace dev {

// Owns the microphones. A task reads them in 20 ms blocks without stopping: it publishes how loud
// the room is for the animation and, between start() and stop(), feeds the left channel to the
// fingerprint as it arrives, so the signature is ready the moment listening ends.
class Listener {
public:
    bool begin();
    void start();
    // Stops feeding. Afterwards the fingerprint can be read through with_fingerprint().
    void stop();
    float latest_dbfs() const { return dbfs_; }
    template <typename F>
    void with_fingerprint(F f) {
        std::lock_guard<std::mutex> lock(lock_);
        f(*fp_);
    }

private:
    static void entry(void* self);
    void run();

    Mic mic_;
    shaza::Fingerprint* fp_ = nullptr;
    std::mutex lock_;
    std::atomic<float> dbfs_{-96.0f};
    std::atomic<bool> capturing_{false}, restart_{false};
};

}  // namespace dev
