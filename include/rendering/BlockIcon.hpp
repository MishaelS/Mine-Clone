#pragma once

#include "raylib.h"

// One face of a cube icon: its UV rectangle (0..1) in the block atlas and
// its tint.
struct CubeIconFace {
    Rectangle uv;
    Color tint;
};

// A full block as the inventory shows it - Minecraft's block item look: an
// orthographic isometric cube inside `bounds`, a shallow diamond top over
// its two nearer sides, the left one (the block's south face - a
// directional block's front) darker than the right one (its east face).
// `side_inset` (0..0.5, blocks - a cactus's) crops the see-through rim a
// flat icon can't pull in. Draws in whatever 2D mode is current. Shared by
// the game's inventory (ui::block_icon) and the model editor's block tab,
// so both show exactly the same icon.
void draw_cube_icon(Rectangle bounds, const Texture2D& atlas, const CubeIconFace& top, const CubeIconFace& left,
                    const CubeIconFace& right, float side_inset);
