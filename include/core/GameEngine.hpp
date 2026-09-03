#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "raylib.h"
#include "core/GameObject.hpp"
#include "World.hpp"

// Owns the window, the main loop, every GameObject in the game, and the
// voxel World terrain (kept separate from the GameObject list since camera
// interaction with it — aiming, breaking blocks — needs to reach across
// chunk borders in a way a single GameObject's update()/draw() can't).
class GameEngine {
public:
    GameEngine(int screen_width, int screen_height, const char* title);
    ~GameEngine();

    void run();

    void add_object(std::unique_ptr<GameObject> object);
    void set_world(std::unique_ptr<World> new_world);

private:
    void update(float delta_time);
    void draw();

    // Free-look test camera (WASD + mouse). Swap back to IsoCamera once
    // testing doesn't need to fly around and inspect the world freely.
    Camera3D camera;
    std::vector<std::unique_ptr<GameObject>> objects;
    std::unique_ptr<World> world;
    bool show_debug_overlay = false; // toggled by F3, Minecraft-style
    float camera_move_speed;         // world units/second; mouse wheel adjusts this

    // Whatever block the crosshair is currently aimed at, within block-
    // placing range — recomputed every frame in update() so draw() can
    // outline it, and independent of the click handlers' own raycasts
    // (which use break/place's own, different, reach distances).
    std::optional<World::RaycastHit> targeted_block;
};
