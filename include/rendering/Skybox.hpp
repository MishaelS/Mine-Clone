#pragma once

#include "raylib.h"

#include <cstdint>

// Rendering module: draws a giant cube centered on the camera, gradient-
// colored (blue up top, lighter near the horizon, by day - see the .cpp's
// own DAY_/NIGHT_/SUNSET_ colors) and depth-tested off so it always reads
// as infinitely far away. No texture/cubemap - just vertex-colored
// geometry. `celestial_angle` (see core/DayNightCycle.hpp) drives the same
// cosine day/night curve Minecraft uses for sky/fog color, plus a warm
// horizon glow while the sun is near the horizon. skybox_horizon_color()/
// skybox_sky_color() below report back whatever this call actually drew,
// for anything else (fog) that needs to match.
void draw_skybox(Vector3 camera_position, float celestial_angle);

// Sun/moon billboards - two textured quads (assets/sprites/sky/sun.png,
// moon.png) positioned along `sun_direction` and its exact opposite (see
// core/DayNightCycle.hpp), at a fixed distance from the camera and
// depth-tested off, same "always infinitely far away" trick draw_skybox()
// itself uses. Call right after draw_skybox() so they sit in front of its
// gradient but still behind every real block. `sun_direction` need not be
// literally DayNightCycle::sun_direction() - any unit vector works - but
// that's the intended source.
void draw_celestial_bodies(Vector3 camera_position, Vector3 sun_direction);

// Seed-bound sky details. Stars and clouds both sample the same small
// deterministic sky-noise stream so a world's seed owns its whole sky
// pattern the same way it already owns terrain. `celestial_angle` fades
// stars in at night; `game_tick` scrolls the cloud sheet. Cloud visibility
// tracks render distance with a small extra margin, while `cloud_volume`
// controls how many soft layers make up the sheet.
void draw_seeded_stars(Vector3 camera_position, uint32_t world_seed, float celestial_angle);
void draw_seeded_clouds(Vector3 camera_position, uint32_t world_seed, uint64_t game_tick, float celestial_angle,
                        int render_distance_blocks, int cloud_volume);

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
