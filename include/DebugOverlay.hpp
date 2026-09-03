#pragma once

#include "raylib.h"

class World;

// Minecraft's F3 debug screen, condensed to what this engine actually
// tracks: position, facing, chunk/light info where the camera stands, and
// what block (if any) the crosshair is aimed at. Anchored to the top-left
// corner regardless of window size. Stateless — the caller (GameEngine)
// owns the show/hide toggle and just calls this when it's on.
void draw_debug_overlay(const Camera3D& camera, const World& world, float aim_reach, float move_speed);
