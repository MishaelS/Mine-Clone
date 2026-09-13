#pragma once

#include "raylib.h"

#include <cstdint>

class World;

// Minecraft's F3 debug screen, condensed to what this engine actually
// tracks: position, facing, chunk/biome/light info where the camera stands,
// what block (if any) the crosshair is aimed at, and the game's tick count.
// Anchored to the top-left corner regardless of window size. Stateless - the
// caller (GameEngine) owns the show/hide toggle and just calls this when
// it's on.
namespace ui {
    void draw_debug_overlay(const Camera3D& camera, const World& world, float aim_reach, float move_speed, uint64_t game_tick);
}
