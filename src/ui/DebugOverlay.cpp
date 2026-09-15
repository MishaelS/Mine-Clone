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

namespace {
    constexpr int FONT_SIZE    = 18;
    constexpr float TEXT_SPACING = 1.0f; // pixels between glyphs, DrawTextEx-style
    constexpr int LINE_SPACING = 4;
    constexpr int PADDING      = 6;
    constexpr int MARGIN       = 8;
    constexpr Color BACKGROUND = {0, 0, 0, 140};

    // Compass name for a (normalized) look direction, clockwise from North -
    // matches the world's own North = -Z / East = +X convention (see
    // Chunk.cpp's CUBE_FACES comment).
    const char* cardinal_direction(Vector3 forward)
    {
        float angle_deg = atan2f(forward.x, -forward.z) * RAD2DEG;
        if (angle_deg < 0.0f) angle_deg += 360.0f;

        static const char* NAMES[8] = {
            "North", "Northeast", "East", "Southeast",
            "South", "Southwest", "West", "Northwest",
        };
        int index = static_cast<int>(std::lround(angle_deg / 45.0f)) % 8;
        return NAMES[index];
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
    int block_light = world.get_block_light(block_x, block_y, block_z);
    int raw_sky_light = world.get_sky_light(block_x, block_y, block_z);
    int effective_sky_light = static_cast<int>(std::round(raw_sky_light * sky_factor));
    int light = world.get_effective_light(block_x, block_y, block_z, sky_factor);
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
    char looking_at[112];
    if (auto hit = world.raycast(camera.position, forward, aim_reach)) {
        BlockType looked_at_type = world.get_block(hit->x, hit->y, hit->z);
        char growth_info[48] = "";
        if (looked_at_type == BlockType::OakSapling) {
            std::snprintf(growth_info, sizeof(growth_info), " [growth: 1/7 per random tick]");
        }
        std::snprintf(looking_at, sizeof(looking_at), "%s (%d, %d, %d)%s",
                      ui::block_display_name(looked_at_type).c_str(),
                      hit->x, hit->y, hit->z, growth_info);
    } else {
        std::snprintf(looking_at, sizeof(looking_at), "None");
    }

    // Day/night: the exact same game_tick-derived values draw_celestial_
    // bodies() itself feeds DayNightCycle::sun_direction() - not a
    // separately-tracked debug-only copy, so this always agrees with what's
    // actually rendered.
    uint64_t day_number = game_tick / DayNightCycle::DAY_LENGTH_TICKS;
    uint64_t tick_of_day = game_tick % DayNightCycle::DAY_LENGTH_TICKS;
    Vector3 sun_dir = DayNightCycle::sun_direction(game_tick);

    char lines[12][96];
    int line_count = 0;
    std::snprintf(lines[line_count++], sizeof(lines[0]), "%d fps", GetFPS());
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Ticks: %llu (20/s)", static_cast<unsigned long long>(game_tick));
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Day %llu, time %llu/%llu (%.0f%%)",
                  static_cast<unsigned long long>(day_number), static_cast<unsigned long long>(tick_of_day),
                  static_cast<unsigned long long>(DayNightCycle::DAY_LENGTH_TICKS),
                  DayNightCycle::time_of_day(game_tick) * 100.0f);
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Sun dir: %.2f / %.2f / %.2f", sun_dir.x, sun_dir.y, sun_dir.z);
    std::snprintf(lines[line_count++], sizeof(lines[0]), "XYZ: %.3f / %.3f / %.3f", camera.position.x, camera.position.y, camera.position.z);
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Block: %d %d %d", block_x, block_y, block_z);
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Chunk: %d %d %d in %d %d", local_x, block_y, local_z, chunk.x, chunk.z);
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Biome: %s", get_biome_name(biome).c_str());
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Facing: %s (%.2f / %.2f / %.2f)", cardinal_direction(forward), forward.x, forward.y, forward.z);
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Light: %d (block %d, sky %d/%d)",
                  light, block_light, effective_sky_light, raw_sky_light);
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Looking at: %s", looking_at);
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Speed: %.1f blocks/s (scroll to change)", move_speed);

    const Font& font = FontManager::get();

    int text_width = 0;
    for (int i = 0; i < line_count; ++i) {
        int width = static_cast<int>(MeasureTextEx(font, lines[i], FONT_SIZE, TEXT_SPACING).x);
        if (width > text_width) text_width = width;
    }

    int box_width = text_width + PADDING * 2;
    int box_height = line_count * (FONT_SIZE + LINE_SPACING) - LINE_SPACING + PADDING * 2;
    DrawRectangle(MARGIN, MARGIN, box_width, box_height, BACKGROUND);

    for (int i = 0; i < line_count; ++i) {
        Vector2 position = {static_cast<float>(MARGIN + PADDING), static_cast<float>(MARGIN + PADDING + i * (FONT_SIZE + LINE_SPACING))};
        DrawTextEx(font, lines[i], position, FONT_SIZE, TEXT_SPACING, WHITE);
    }
}
