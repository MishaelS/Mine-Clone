#pragma once

#include "raymath.h"

// A position simulated at the fixed game-tick rate (see core/Tick.hpp)
// rather than every render frame, with linear interpolation between the
// last two ticks for smooth rendering in between - the same fixed-
// timestep-simulation/interpolated-render split Minecraft itself runs on.
// Shared by anything whose physics advances one tick at a time (dropped
// items, falling-block entities) instead of each duplicating the same
// three lines.
struct TickMotion {
    Vector3 previous{0.0f, 0.0f, 0.0f};
    Vector3 current{0.0f, 0.0f, 0.0f};

    void reset(Vector3 position) { previous = current = position; }

    // Call once at the very start of a tick's physics step, before moving
    // `current` - remembers where this tick started so interpolated()
    // below has both ends of it to blend between.
    void begin_tick() { previous = current; }

    // `alpha` is how far the current render frame already is into the
    // next tick (GameEngine::draw() passes tick_accumulator /
    // TICK_DURATION) - 0 at the instant a tick just finished, approaching
    // 1 right before the next one lands.
    Vector3 interpolated(float alpha) const { return Vector3Lerp(previous, current, alpha); }
};
