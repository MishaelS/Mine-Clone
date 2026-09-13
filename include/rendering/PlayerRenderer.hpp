#pragma once

#include "raylib.h"

class World;

// Renders the classic (four-pixel arm) Minecraft player model. Geometry uses
// the skin atlas' native 8/12/4-pixel body-part proportions, so every skin
// region fits its intended face instead of being stretched over an arbitrary
// coloured cuboid.
class PlayerRenderer {
public:
    void draw(Vector3 feet_position, Vector3 forward, const World& world) const;

    // Same model, but rotated by explicit yaw/pitch (degrees) instead of a
    // world-space forward vector, and lit by a single fixed `tint` instead
    // of sampling entity_environment_tint() per body part - for a UI
    // preview (the inventory screen's own player model) that has no World
    // to light from and shouldn't visually darken just because the actual
    // world happens to be dark right now, same as real Minecraft's own
    // inventory character preview always reading as evenly lit.
    void draw_flat(Vector3 feet_position, float yaw_degrees, float pitch_degrees, Color tint) const;
};
