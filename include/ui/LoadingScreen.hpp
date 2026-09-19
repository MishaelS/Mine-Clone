#pragma once

#include "raylib.h"

#include <random>
#include <string>
#include <vector>

// Full-screen "Generating/Loading world" screen: dirt menu background, a
// title, a progress bar that fills smoothly (it eases toward the reported
// progress and never moves backwards) and throws a spray of sparks off its
// leading edge - more the faster it's filling - plus the current stage and
// percentage. Draws one complete frame per draw() call; GameEngine calls it
// from World's load-progress callback while a world loads.
class LoadingScreen {
public:
    // Fresh bar at 0 and no sparks - call when a new load starts.
    void reset();

    // Call between BeginDrawing()/EndDrawing(). `progress` is 0..1; time
    // for the animation comes from GetTime(), so irregular call spacing
    // still animates at real speed.
    void draw(const std::string& title, const std::string& stage, float progress);

    // True once the load reported 100% and the bar has visibly caught up
    // to it - the bar deliberately fills slower than a fast load, so the
    // caller keeps drawing until this before leaving the screen.
    bool finished() const;

private:
    struct Spark {
        Vector2 position;  // screen pixels
        Vector2 velocity;  // pixels / second
        float life;        // seconds left
        float max_life;
        float size;        // pixels
        Color color;
    };

    void update_sparks(float dt, Rectangle fill, float fill_speed);

    std::vector<Spark> sparks;
    float target_progress = 0.0f;
    float shown_progress = 0.0f;
    float spawn_budget = 0.0f;
    double last_time = -1.0;
    std::mt19937 rng{0x5eed};
};
