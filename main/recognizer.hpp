#pragma once

#include <atomic>
#include <mutex>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "reply.hpp"

namespace dev {

class Listener;

// Asks Shazam about the clip the listener just fingerprinted, fetches the cover, and lays the
// result out for the screen, on a task of its own so the animation never waits on the network.
class Recognizer {
public:
    explicit Recognizer(Listener& listener) : listener_(listener) {}
    bool begin();
    // Starts a recognition of what the listener holds. Ignored while one is running.
    void ask();
    // True once, when an answer (found, none or error) is ready.
    bool take(shaza::Reply& out);

private:
    static void entry(void* self);
    void run();
    shaza::Reply recognise();

    Listener& listener_;
    TaskHandle_t task_ = nullptr;
    std::mutex lock_;
    shaza::Reply reply_;
    bool ready_ = false;
    std::atomic<bool> busy_{false};
};

}  // namespace dev
