#pragma once

#include "raylib.h"

#include <cstdint>

// Groundwork for a day/night cycle, driven by GameEngine's own game_tick -
// the same fixed 20-ticks/second clock every other tick-based system
// (falling blocks, dropped items, sapling growth) already shares, so the
// sun/moon advance in lockstep with the rest of world simulation rather
// than off wall-clock time (pausing the accumulator - a stall, a frozen
// background tab - pauses them too).
//
// Nothing reads sun_direction() for actual lighting yet - Chunk's own sky/
// block light stays exactly as static as before ("open air always reads as
// fully lit"). This only drives the visible sun/moon sprites for now (see
// rendering/Skybox.hpp's draw_celestial_bodies()), but exposes the one
// value any future ambient-lighting work would need first, computed the
// same authoritative way the visuals already do - so that work starts from
// a single source of truth instead of re-deriving its own angle.
namespace DayNightCycle {
    // One full day, in ticks - real Minecraft's own length: 24000 ticks at
    // 20 ticks/second is exactly 20 real-world minutes.
    constexpr uint64_t DAY_LENGTH_TICKS = 24000;

    // 0 at dawn (sunrise) - a freshly created world's game_tick == 0 starts
    // at first light rather than midnight, same as vanilla - 0.25 noon, 0.5
    // dusk, 0.75 midnight, wrapping back to 1.0 == 0.0.
    float time_of_day(uint64_t game_tick);

    // Normalized world-space direction from the player toward the sun -
    // arcs east to west through the zenith along one fixed vertical plane
    // (world +Z is always exactly perpendicular to it), the same
    // simplified single-plane path real Minecraft's own sun/moon follow,
    // rather than a full azimuth/elevation sky with seasonal drift.
    Vector3 sun_direction(uint64_t game_tick);

    // Always exactly opposite sun_direction() - the moon is up whenever the
    // sun isn't, same as vanilla's own paired sun/moon.
    Vector3 moon_direction(uint64_t game_tick);

    // How strongly sky light actually illuminates the world right now, 0..1 -
    // 1 at full day (a cell's raw, always-fully-lit sky light - see World::
    // get_sky_light() - passes through unchanged), MIN_NIGHT_SKY_LIGHT_FACTOR
    // at full night (real Minecraft's own "internal sky light" floor: a
    // fully sky-exposed cell reads as roughly 4 out of 15, not 0 - night
    // outdoors is dim, not pitch black), following the sun's own height
    // above the horizon (sun_direction().y) in between so dawn/dusk fade
    // gradually instead of snapping. Block light (torches, lava) is never
    // touched by this at all - matches real Minecraft's own "only sky
    // light dims at night" rule. Multiply this into a cell's raw sky light
    // to get its actual current brightness contribution; the chunk mesh
    // shader (Chunk.hpp's set_chunk_daylight()) and entity rendering
    // (EntityLighting.hpp's set_entity_daylight_factor()) both do exactly
    // that, fed this same value once per frame, so terrain/entities/debug
    // readouts all agree.
    constexpr float MIN_NIGHT_SKY_LIGHT_FACTOR = 4.0f / 15.0f;
    float sky_light_factor(uint64_t game_tick);
}
