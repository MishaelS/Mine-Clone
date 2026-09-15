#pragma once

#include "raylib.h"

// Rendering module: draws a giant cube centered on the camera, gradient-colored (sky blue up
// top, lighter near the horizon) and depth-tested off so it always reads as
// infinitely far away. No texture/cubemap - just vertex-colored geometry.
void draw_skybox(Vector3 camera_position);

// The color the skybox itself fades to at the horizon - for anything else
// that needs to blend into the sky the same way (World's distance fog, so
// the render-distance edge reads as fading into the horizon instead of a
// hard cutoff where chunks just stop).
Color skybox_horizon_color();

// The skybox's own color straight up, at the top of its vertical gradient -
// paired with skybox_horizon_color() so distance fog can blend toward
// whichever of the two actually sits behind a given fragment (see
// set_chunk_fog/chunk.fs): fogging every fragment with the flat horizon
// color alone left a visible mismatch wherever terrain rose noticeably
// above the horizon line - a mountain's fogged silhouette reading a shade
// too pale/white against the bluer sky actually behind it at that height.
Color skybox_sky_color();
