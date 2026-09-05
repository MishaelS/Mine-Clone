#pragma once

#include <cstdint>
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
    // Fixed-rate game-logic step, called exactly 20 times per second of real
    // time regardless of the render frame rate (see run()) — Minecraft's own
    // tick rate. Nothing hooks into it yet; it's the clock future world
    // simulation (day/night, scheduled block updates, random ticks) will run
    // on, same role Minecraft's tick serves.
    void tick();

    void update(float delta_time);
    void draw();

    // Free-look test camera (WASD + mouse). Swap back to IsoCamera once
    // testing doesn't need to fly around and inspect the world freely.
    Camera3D camera;
    std::vector<std::unique_ptr<GameObject>> objects;
    std::unique_ptr<World> world;
    bool show_debug_overlay = false; // toggled by F3, Minecraft-style
    bool show_chunk_borders = false; // toggled by F4 — World::draw_chunk_borders()
    bool show_wireframe = false;     // toggled by F5 — wireframe chunk meshes instead of textured, for inspecting mesh/culling
    float camera_move_speed;         // world units/second; mouse wheel adjusts this

    // Fixed-timestep accumulator (see run()): seconds of real frame time not
    // yet consumed by a tick. Carries any leftover fraction of a tick
    // forward to the next frame instead of dropping it, so the tick rate
    // averages out to exactly 20/second over time rather than drifting.
    float tick_accumulator = 0.0f;

    // Ticks elapsed since the world started — Minecraft calls the equivalent
    // the world's "age". Nothing reads this yet beyond the debug overlay;
    // it's here for future systems (day/night, scheduled updates) to key
    // off of.
    uint64_t game_tick = 0;

    // Whatever block the crosshair is currently aimed at, within block-
    // placing range — recomputed every frame in update() so draw() can
    // outline it, and independent of the click handlers' own raycasts
    // (which use break/place's own, different, reach distances).
    std::optional<World::RaycastHit> targeted_block;
};
