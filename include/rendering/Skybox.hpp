#pragma once

#include "raylib.h"

// Rendering module: draws a giant cube centered on the camera, gradient-
// colored (blue up top, lighter near the horizon, by day - see the .cpp's
// own DAY_/NIGHT_/SUNSET_ colors) and depth-tested off so it always reads
// as infinitely far away. No texture/cubemap - just vertex-colored
// geometry. `sun_direction` (see core/DayNightCycle.hpp) blends the whole
// gradient toward night's own darker colors as the sun sinks, plus a warm
// glow at the horizon specifically while it's near the horizon line
// (sunrise/sunset) - skybox_horizon_color()/skybox_sky_color() below
// report back whatever this call actually drew, for anything else (fog)
// that needs to match.
void draw_skybox(Vector3 camera_position, Vector3 sun_direction);

// Sun/moon billboards - two textured quads (assets/sprites/sky/sun.png,
// moon.png) positioned along `sun_direction` and its exact opposite (see
// core/DayNightCycle.hpp), at a fixed distance from the camera and
// depth-tested off, same "always infinitely far away" trick draw_skybox()
// itself uses. Call right after draw_skybox() so they sit in front of its
// gradient but still behind every real block. `sun_direction` need not be
// literally DayNightCycle::sun_direction() - any unit vector works - but
// that's the intended source.
void draw_celestial_bodies(Vector3 camera_position, Vector3 sun_direction);

// The color the skybox itself is *currently* fading to at the horizon -
// i.e. whatever the last draw_skybox() call actually drew with, day/night
// and sunset glow already blended in, not a fixed constant - for anything
// else that needs to blend into the sky the same way (World's distance
// fog, so the render-distance edge reads as fading into the horizon
// instead of a hard cutoff where chunks just stop).
Color skybox_horizon_color();

// The skybox's own color straight up, at the top of its vertical gradient -
// paired with skybox_horizon_color() so distance fog can blend toward
// whichever of the two actually sits behind a given fragment (see
// set_chunk_fog/chunk.fs): fogging every fragment with the flat horizon
// color alone left a visible mismatch wherever terrain rose noticeably
// above the horizon line - a mountain's fogged silhouette reading a shade
// too pale/white against the bluer sky actually behind it at that height.
Color skybox_sky_color();
