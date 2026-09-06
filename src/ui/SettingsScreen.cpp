#include "ui/SettingsScreen.hpp"
#include "ui/Widgets.hpp"
#include "core/Block.hpp"
#include "Chunk.hpp" // CHUNK_SIZE - fog distance is expressed in blocks, render distance in chunks

#include "raylib.h"

#include <algorithm>

namespace {
    constexpr float ROW_HEIGHT = 34.0f;
    constexpr float ROW_SPACING = 6.0f;
    constexpr int TITLE_FONT_SIZE = 34;
    constexpr int SECTION_FONT_SIZE = 20;
    constexpr float BOTTOM_BUTTON_WIDTH = 200.0f;
    constexpr float BOTTOM_BUTTON_HEIGHT = 48.0f;

    constexpr int RENDER_DISTANCE_MIN = 4;
    constexpr int RENDER_DISTANCE_MAX = 16;
    constexpr int FOG_DISTANCE_MIN = 32;
    constexpr int FPS_MIN = 30;
    constexpr int FPS_MAX = 240;

    void apply_texture_filter(TextureFilterMode mode)
    {
        SetTextureFilter(get_block_atlas_texture(),
                          mode == TextureFilterMode::Bilinear ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    }
}

SettingsScreen::Action SettingsScreen::update(Settings& settings)
{
    // Resolve any rebind-in-progress BEFORE anything below can newly start
    // one - otherwise the very click that arms a rebind (still "pressed"
    // for the rest of this same frame) would immediately be consumed as
    // its own new binding the instant it's checked.
    if (rebinding_action) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            rebinding_action.reset();
        } else if (auto captured = poll_any_binding_pressed()) {
            settings.keybindings[static_cast<size_t>(*rebinding_action)] = *captured;
            SettingsIO::save(settings);
            rebinding_action.reset();
        }
    }

    int screen_width = GetScreenWidth();
    int screen_height = GetScreenHeight();

    ui::label({0.0f, 24.0f, static_cast<float>(screen_width), 46.0f}, "Настройки", TITLE_FONT_SIZE, WHITE);

    constexpr float COLUMN_WIDTH = 440.0f;
    constexpr float COLUMN_GAP = 40.0f;
    float left_x = screen_width / 2.0f - COLUMN_WIDTH - COLUMN_GAP / 2.0f;
    float right_x = screen_width / 2.0f + COLUMN_GAP / 2.0f;
    float top_y = 110.0f;

    // --- Left column: keybindings ---
    ui::label({left_x, top_y - 28.0f, COLUMN_WIDTH, 24.0f}, "Управление", SECTION_FONT_SIZE, LIGHTGRAY);
    float y = top_y;
    for (size_t i = 0; i < settings.keybindings.size(); ++i) {
        GameAction action = static_cast<GameAction>(i);
        Rectangle label_bounds = {left_x, y, COLUMN_WIDTH * 0.55f, ROW_HEIGHT};
        Rectangle bind_bounds = {left_x + COLUMN_WIDTH * 0.55f + 10.0f, y, COLUMN_WIDTH * 0.45f - 10.0f, ROW_HEIGHT};

        ui::label(label_bounds, game_action_display_name(action), 18, WHITE);

        bool is_rebinding = rebinding_action && *rebinding_action == action;
        std::string bind_label = is_rebinding ? "..." : binding_display_name(settings.keybindings[i]);
        if (ui::button(bind_bounds, bind_label, is_rebinding) && !rebinding_action) {
            rebinding_action = action;
        }

        y += ROW_HEIGHT + ROW_SPACING;
    }

    // --- Right column: render/fog distance, texture filter, FPS ---
    ui::label({right_x, top_y - 28.0f, COLUMN_WIDTH, 24.0f}, "Графика", SECTION_FONT_SIZE, LIGHTGRAY);
    float ry = top_y;

    Rectangle render_row = {right_x, ry, COLUMN_WIDTH, ROW_HEIGHT};
    if (ui::slider_int(render_row, "Дальность рендера", settings.render_distance_chunks, RENDER_DISTANCE_MIN, RENDER_DISTANCE_MAX)) {
        settings.fog_distance_blocks = std::min(settings.fog_distance_blocks, settings.render_distance_chunks * CHUNK_SIZE);
        SettingsIO::save(settings);
    }
    ry += ROW_HEIGHT + ROW_SPACING * 3.0f;

    int fog_max = settings.render_distance_chunks * CHUNK_SIZE;
    Rectangle fog_row = {right_x, ry, COLUMN_WIDTH, ROW_HEIGHT};
    if (ui::slider_int(fog_row, "Дальность видимости", settings.fog_distance_blocks, FOG_DISTANCE_MIN, fog_max)) {
        SettingsIO::save(settings);
    }
    ry += ROW_HEIGHT + ROW_SPACING * 3.0f;

    Rectangle fps_row = {right_x, ry, COLUMN_WIDTH, ROW_HEIGHT};
    if (ui::slider_int(fps_row, "Ограничение FPS", settings.target_fps, FPS_MIN, FPS_MAX)) {
        SetTargetFPS(settings.target_fps);
        SettingsIO::save(settings);
    }
    ry += ROW_HEIGHT + ROW_SPACING * 3.0f;

    ui::label({right_x, ry - 24.0f, COLUMN_WIDTH, 20.0f}, "Способ отрисовки текстур", 16, LIGHTGRAY);
    Rectangle point_bounds = {right_x, ry, COLUMN_WIDTH * 0.5f - 5.0f, ROW_HEIGHT + 6.0f};
    Rectangle bilinear_bounds = {right_x + COLUMN_WIDTH * 0.5f + 5.0f, ry, COLUMN_WIDTH * 0.5f - 5.0f, ROW_HEIGHT + 6.0f};
    if (ui::button(point_bounds, "Точная (пиксели)", settings.texture_filter == TextureFilterMode::Point)) {
        settings.texture_filter = TextureFilterMode::Point;
        apply_texture_filter(settings.texture_filter);
        SettingsIO::save(settings);
    }
    if (ui::button(bilinear_bounds, "Плавная", settings.texture_filter == TextureFilterMode::Bilinear)) {
        settings.texture_filter = TextureFilterMode::Bilinear;
        apply_texture_filter(settings.texture_filter);
        SettingsIO::save(settings);
    }

    Action result;
    float back_x = screen_width / 2.0f - BOTTOM_BUTTON_WIDTH / 2.0f;
    float back_y = screen_height - BOTTOM_BUTTON_HEIGHT - 30.0f;
    if (ui::button({back_x, back_y, BOTTOM_BUTTON_WIDTH, BOTTOM_BUTTON_HEIGHT}, "Назад")) {
        result = {ActionType::Back};
    }

    return result;
}
