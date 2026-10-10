#pragma once

#include <cstdint>
#include <memory>

#include "app.hpp"

namespace shaza {

// One thing on screen: what it is, a key that changes whenever it would look different, and a box
// it never paints outside of.
struct Item {
    int id;
    uint32_t key;
    rk::Rect box;
};

// A visual direction. Each frame it lists its items; Look compares them with the last frame and
// repaints only the boxes that changed, clearing each one and redrawing everything that overlaps
// it, clipped to it. A screen that is not moving sends nothing to the panel.
class Look {
public:
    static constexpr int kMaxItems = 24;

    virtual ~Look() = default;
    virtual const char* name() const = 0;
    // now is seconds since start, for motion that does not depend on the phase.
    void frame(rk::Surface& sf, const App& app, float now);
    // The next frame repaints the whole screen.
    void invalidate() { full_ = true; }

protected:
    // Fills items back to front and keeps whatever paint() will need to draw them.
    virtual int layout(const App& app, float now, Item* items) = 0;
    virtual void paint(rk::Surface& sf, int id) = 0;

private:
    Item last_[kMaxItems];
    int last_n_ = 0;
    bool full_ = true;
};

constexpr int kLooks = 3;
std::unique_ptr<Look> make_look(int which);

// For the tests: the rectangles frame() decided to repaint last time.
int last_zones(rk::Rect* out, int max);

}  // namespace shaza
