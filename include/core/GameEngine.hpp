#pragma once

#include <memory>
#include <vector>

#include "raylib.h"
#include "core/GameObject.hpp"

// Owns the window, the main loop, and every GameObject in the game.
class GameEngine {
public:
    GameEngine(int screen_width, int screen_height, const char* title);
    ~GameEngine();

    void run();

    void add_object(std::unique_ptr<GameObject> object);

private:
    void update(float delta_time);
    void draw();

    // Free-look test camera (WASD + mouse). Swap back to IsoCamera once
    // testing doesn't need to fly around and inspect the world freely.
    Camera3D camera;
    std::vector<std::unique_ptr<GameObject>> objects;
};
