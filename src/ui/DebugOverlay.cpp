#include "ui/DebugOverlay.hpp"
#include "world/World.hpp"
#include "core/Biome.hpp"
#include "core/Block.hpp"
#include "core/DayNightCycle.hpp"
#include "ui/FontManager.hpp"
#include "ui/Localization.hpp"

#include "raymath.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
    constexpr int FONT_SIZE = 18;
    constexpr float TEXT_SPACING = 1.0f; // pixels between glyphs, DrawTextEx-style
    constexpr int LINE_SPACING = 4;
    constexpr int PADDING      = 6;
    constexpr int MARGIN       = 8;
    constexpr Color BACKGROUND = {0, 0, 0, 140};

    // Compass name for a (normalized) look direction, clockwise from North -
    // matches the world's own North = -Z / East = +X convention (see
    // Chunk.cpp's CUBE_FACES comment).
    const std::string& cardinal_direction(Vector3 forward) {
        float angle_deg = atan2f(forward.x, -forward.z) * RAD2DEG;
        if (angle_deg < 0.0f) angle_deg += 360.0f;

        static const char* KEYS[8] = {
            "direction.north", "direction.northeast", "direction.east", "direction.southeast",
            "direction.south", "direction.southwest", "direction.west", "direction.northwest",
        };
        int index = static_cast<int>(std::lround(angle_deg / 45.0f)) % 8;
        return ui::tr(KEYS[index]);
    }

    // A number with a fixed count of decimals, for tr_format() arguments.
    std::string fixed(float value, int decimals) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
        return buffer;
    }
}

void ui::draw_debug_overlay(const Camera3D& camera, const World& world, float aim_reach, float move_speed, uint64_t game_tick)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    int block_x = static_cast<int>(std::floor(camera.position.x));
    int block_y = static_cast<int>(std::floor(camera.position.y));
    int block_z = static_cast<int>(std::floor(camera.position.z));

    World::ChunkCoordinates chunk = world.chunk_coordinates(block_x, block_z);
    // The real "how lit is this right now" value (day/night-adjusted, see
    // World::get_effective_light()'s own comment) - not get_light() alone,
    // which only ever reports the raw, always-fully-lit-by-day potential.
    float sky_factor = DayNightCycle::sky_light_factor(game_tick);
    int block_light   = world.get_block_light(block_x, block_y, block_z);
    int raw_sky_light = world.get_sky_light(block_x, block_y, block_z);
    int effective_sky_light = static_cast<int>(std::round(raw_sky_light * sky_factor));
    int light   = world.get_effective_light(block_x, block_y, block_z, sky_factor);
    Biome biome = world.get_biome(block_x, block_z);

    // Position within the chunk itself, not just which chunk (world-space
    // Block: line above) or which chunk grid cell (chunk.x/z) - same three-
    // tier breakdown Minecraft's own F3 "Chunk:" line shows (local x/y/z
    // "in" chunk x/z). Y doesn't wrap per chunk here (a chunk spans the
    // whole world height, no vertical stacking), so local_y == block_y.
    int local_x = block_x - chunk.x * CHUNK_SIZE;
    int local_z = block_z - chunk.z * CHUNK_SIZE;

    // Sapling is the only random-tick-growth block right now (see
    // GameEngine::update_random_ticks()) - a future crop would get its own
    // branch here the same way, right next to whatever its own growth-chance
    // constant lives.
    std::string looking_at;
    if (auto hit = world.raycast(camera.position, forward, aim_reach)) {
        BlockType looked_at_type = world.get_block(hit->x, hit->y, hit->z);
        looking_at = ui::block_display_name(looked_at_type) + " (" + std::to_string(hit->x) + ", " +
                     std::to_string(hit->y) + ", " + std::to_string(hit->z) + ")";
        if (looked_at_type == BlockType::OakSapling) looking_at += ui::tr("debug.sapling_growth");
    } else {
        looking_at = ui::tr("debug.none");
    }

    // Day/night: the exact same game_tick-derived values draw_celestial_
    // bodies() itself feeds DayNightCycle::sun_direction() - not a
    // separately-tracked debug-only copy, so this always agrees with what's
    // actually rendered.
    uint64_t day_number = game_tick / DayNightCycle::DAY_LENGTH_TICKS;
    uint64_t tick_of_day = game_tick % DayNightCycle::DAY_LENGTH_TICKS;
    Vector3 sun_dir = DayNightCycle::sun_direction(game_tick);

    auto number = [](auto value) { return std::to_string(value); };
    std::vector<std::string> lines = {
        ui::tr_format("debug.fps", {number(GetFPS())}),
        ui::tr_format("debug.ticks", {number(game_tick)}),
        ui::tr_format("debug.day", {number(day_number), number(tick_of_day),
                                    number(DayNightCycle::DAY_LENGTH_TICKS),
                                    fixed(DayNightCycle::time_of_day(game_tick) * 100.0f, 0)}),
        ui::tr_format("debug.sun", {fixed(sun_dir.x, 2), fixed(sun_dir.y, 2), fixed(sun_dir.z, 2)}),
        ui::tr_format("debug.xyz", {fixed(camera.position.x, 3), fixed(camera.position.y, 3), fixed(camera.position.z, 3)}),
        ui::tr_format("debug.block", {number(block_x), number(block_y), number(block_z)}),
        ui::tr_format("debug.chunk", {number(local_x), number(block_y), number(local_z), number(chunk.x), number(chunk.z)}),
        ui::tr_format("debug.biome", {ui::biome_display_name(biome)}),
        ui::tr_format("debug.facing", {cardinal_direction(forward), fixed(forward.x, 2), fixed(forward.y, 2), fixed(forward.z, 2)}),
        ui::tr_format("debug.light", {number(light), number(block_light), number(effective_sky_light), number(raw_sky_light)}),
        ui::tr_format("debug.looking_at", {looking_at}),
        ui::tr_format("debug.speed", {fixed(move_speed, 1)}),
    };
    const int line_count = static_cast<int>(lines.size());

    const Font& font = FontManager::get();

    int text_width = 0;
    for (int i = 0; i < line_count; ++i) {
        int width = static_cast<int>(MeasureTextEx(font, lines[i].c_str(), FONT_SIZE, TEXT_SPACING).x);
        if (width > text_width) text_width = width;
    }

    int box_width = text_width + PADDING * 2;
    int box_height = line_count * (FONT_SIZE + LINE_SPACING) - LINE_SPACING + PADDING * 2;
    DrawRectangle(MARGIN, MARGIN, box_width, box_height, BACKGROUND);

    for (int i = 0; i < line_count; ++i) {
        Vector2 position = {static_cast<float>(MARGIN + PADDING), static_cast<float>(MARGIN + PADDING + i * (FONT_SIZE + LINE_SPACING))};
        DrawTextEx(font, lines[i].c_str(), position, FONT_SIZE, TEXT_SPACING, WHITE);
    }
}
