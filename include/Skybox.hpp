#pragma once

#include "raylib.h"

// Draws a giant cube centered on the camera, gradient-colored (sky blue up
// top, lighter near the horizon) and depth-tested off so it always reads as
// infinitely far away. No texture/cubemap — just vertex-colored geometry.
void draw_skybox(Vector3 camera_position);

// The color the skybox itself fades to at the horizon — for anything else
// that needs to blend into the sky the same way (World's distance fog, so
// the render-distance edge reads as fading into the horizon instead of a
// hard cutoff where chunks just stop).
Color skybox_horizon_color();
