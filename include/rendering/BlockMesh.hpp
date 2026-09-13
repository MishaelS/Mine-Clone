#pragma once

#include "core/Block.hpp"
#include "raylib.h"

// Rendering module: draws one full block-textured cube in world space,
// using the same atlas UVs/tints/per-face shading a chunk mesh would for
// this BlockType - shared by anything that needs to render a single block
// outside of chunk meshing (dropped items, falling-block entities) instead
// of each reimplementing its own copy of the same 6-quad geometry.
//
// Caller is responsible for whatever transform (translation, scale,
// rotation for a dropped item's spin/bob) is active before calling this -
// it draws a unit cube centered on the origin of the *current* rlgl
// matrix, the same convention DrawCube-family raylib functions use.
// `alpha` multiplies every vertex color's own alpha, for fading a dropped
// item out or a block-breaking overlay in - 255 (opaque) for a normal
// solid block.
void draw_block_cube(BlockType type, unsigned char alpha = 255);

// Same unit cube and calling convention as draw_block_cube(), but with one
// UV rectangle sampled identically on all 6 faces at a flat `tint`
// (alpha included) instead of a BlockType's own per-face atlas layout -
// for the one thing that isn't a block's own texture at all: the block-
// breaking crack overlay (ui::block_breaking_overlay()), one fixed atlas
// tile stamped over whatever block is being broken.
void draw_textured_cube(const Texture2D& texture, Rectangle uv, Color tint);
