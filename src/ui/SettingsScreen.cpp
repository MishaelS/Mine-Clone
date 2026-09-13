#include "ui/SettingsScreen.hpp"
#include "ui/Widgets.hpp"
#include "core/Block.hpp"
#include "world/Chunk.hpp"

#include "raylib.h"

#include <algorithm>

namespace {
    constexpr float ROW_HEIGHT = 34.0f;
    constexpr float ROW_SPACING = 6.0f;
    constexpr int TITLE_FONT_SIZE = 34;
    constexpr float PANEL_WIDTH = 520.0f;
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
    ui::label({0.0f, 22.0f, static_cast<float>(screen_width), 44.0f},
              "Настройки", TITLE_FONT_SIZE, WHITE);

    constexpr float TAB_WIDTH = 180.0f;
    constexpr float TAB_GAP = 10.0f;
    float tabs_width = TAB_WIDTH * 3.0f + TAB_GAP * 2.0f;
    float tab_x = (screen_width - tabs_width) * 0.5f;
    float tab_y = 78.0f;
    if (ui::button({tab_x, tab_y, TAB_WIDTH, 42.0f}, "Управление", section == Section::Controls)) {
        section = Section::Controls;
        rebinding_action.reset();
    }
    tab_x += TAB_WIDTH + TAB_GAP;
    if (ui::button({tab_x, tab_y, TAB_WIDTH, 42.0f}, "Графика", section == Section::Graphics)) {
        section = Section::Graphics;
        rebinding_action.reset();
    }
    tab_x += TAB_WIDTH + TAB_GAP;
    if (ui::button({tab_x, tab_y, TAB_WIDTH, 42.0f}, "Звуки", section == Section::Sound)) {
        section = Section::Sound;
        rebinding_action.reset();
    }

    float x = screen_width * 0.5f - PANEL_WIDTH * 0.5f;
    float y = 158.0f;

    if (section == Section::Controls) {
        for (size_t i = 0; i < settings.keybindings.size(); ++i) {
            GameAction action = static_cast<GameAction>(i);
            Rectangle label_bounds = {x, y, PANEL_WIDTH * 0.55f, ROW_HEIGHT};
            Rectangle bind_bounds = {x + PANEL_WIDTH * 0.55f + 10.0f, y,
                                     PANEL_WIDTH * 0.45f - 10.0f, ROW_HEIGHT};
            ui::label(label_bounds, game_action_display_name(action), 18, WHITE);
            bool rebinding = rebinding_action && *rebinding_action == action;
            std::string text = rebinding ? "..." : binding_display_name(settings.keybindings[i]);
            if (ui::button(bind_bounds, text, rebinding) && !rebinding_action) rebinding_action = action;
            y += ROW_HEIGHT + ROW_SPACING;
        }
    } else if (section == Section::Graphics) {
        if (ui::slider_int({x, y, PANEL_WIDTH, ROW_HEIGHT}, "Дальность рендера",
                           settings.render_distance_chunks, RENDER_DISTANCE_MIN, RENDER_DISTANCE_MAX)) {
            settings.fog_distance_blocks = std::min(settings.fog_distance_blocks,
                                                     settings.render_distance_chunks * CHUNK_SIZE);
            SettingsIO::save(settings);
        }
        y += ROW_HEIGHT + ROW_SPACING * 3.0f;
        int fog_max = settings.render_distance_chunks * CHUNK_SIZE;
        if (ui::slider_int({x, y, PANEL_WIDTH, ROW_HEIGHT}, "Дальность видимости",
                           settings.fog_distance_blocks, FOG_DISTANCE_MIN, fog_max)) SettingsIO::save(settings);
        y += ROW_HEIGHT + ROW_SPACING * 3.0f;
        if (ui::slider_int({x, y, PANEL_WIDTH, ROW_HEIGHT}, "Ограничение FPS",
                           settings.target_fps, FPS_MIN, FPS_MAX)) {
            SetTargetFPS(settings.target_fps);
            SettingsIO::save(settings);
        }
        y += ROW_HEIGHT + ROW_SPACING * 3.0f;
        ui::label({x, y - 24.0f, PANEL_WIDTH, 20.0f}, "Фильтрация текстур", 16, LIGHTGRAY);
        if (ui::button({x, y, PANEL_WIDTH * 0.5f - 5.0f, ROW_HEIGHT + 6.0f},
                       "Точная (пиксели)", settings.texture_filter == TextureFilterMode::Point)) {
            settings.texture_filter = TextureFilterMode::Point;
            apply_texture_filter(settings.texture_filter);
            SettingsIO::save(settings);
        }
        if (ui::button({x + PANEL_WIDTH * 0.5f + 5.0f, y,
                        PANEL_WIDTH * 0.5f - 5.0f, ROW_HEIGHT + 6.0f},
                       "Плавная", settings.texture_filter == TextureFilterMode::Bilinear)) {
            settings.texture_filter = TextureFilterMode::Bilinear;
            apply_texture_filter(settings.texture_filter);
            SettingsIO::save(settings);
        }
    } else {
        auto volume_slider = [&](const char* label, int& value) {
            if (ui::slider_int({x, y, PANEL_WIDTH, ROW_HEIGHT}, label, value, 0, 100)) SettingsIO::save(settings);
            y += ROW_HEIGHT + ROW_SPACING * 3.0f;
        };
        volume_slider("Общая громкость", settings.master_volume);
        volume_slider("Блоки и шаги", settings.effects_volume);
        volume_slider("Окружение", settings.ambient_volume);
        volume_slider("Музыка", settings.music_volume);
    }

    float back_x = screen_width * 0.5f - BOTTOM_BUTTON_WIDTH * 0.5f;
    float back_y = screen_height - BOTTOM_BUTTON_HEIGHT - 30.0f;
    if (ui::button({back_x, back_y, BOTTOM_BUTTON_WIDTH, BOTTOM_BUTTON_HEIGHT}, "Назад")) {
        return {ActionType::Back};
    }
    return {};
}
