#include "DebugOverlay.hpp"
#include "World.hpp"
#include "core/Block.hpp"
#include "core/FontManager.hpp"

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

    // Compass name for a (normalized) look direction, clockwise from North —
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

void draw_debug_overlay(const Camera3D& camera, const World& world, float aim_reach, float move_speed)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    int block_x = static_cast<int>(std::floor(camera.position.x));
    int block_y = static_cast<int>(std::floor(camera.position.y));
    int block_z = static_cast<int>(std::floor(camera.position.z));

    World::ChunkCoordinates chunk = world.chunk_coordinates(block_x, block_z);
    int light = world.get_light(block_x, block_y, block_z);

    char looking_at[64];
    if (auto hit = world.raycast(camera.position, forward, aim_reach)) {
        std::snprintf(looking_at, sizeof(looking_at), "%s (%d, %d, %d)",
                      get_block_name(world.get_block(hit->x, hit->y, hit->z)).c_str(),
                      hit->x, hit->y, hit->z);
    } else {
        std::snprintf(looking_at, sizeof(looking_at), "None");
    }

    char lines[8][96];
    int line_count = 0;
    std::snprintf(lines[line_count++], sizeof(lines[0]), "%d fps", GetFPS());
    std::snprintf(lines[line_count++], sizeof(lines[0]), "XYZ: %.3f / %.3f / %.3f", camera.position.x, camera.position.y, camera.position.z);
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Block: %d %d %d", block_x, block_y, block_z);
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Chunk: %d %d", chunk.x, chunk.z);
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Facing: %s (%.2f / %.2f / %.2f)", cardinal_direction(forward), forward.x, forward.y, forward.z);
    std::snprintf(lines[line_count++], sizeof(lines[0]), "Light: %d", light);
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
