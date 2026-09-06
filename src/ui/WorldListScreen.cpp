#include "ui/WorldListScreen.hpp"
#include "ui/Widgets.hpp"

#include "raylib.h"

#include <algorithm>
#include <cstdio>

namespace {
    constexpr float ROW_HEIGHT = 56.0f;
    constexpr float ROW_SPACING = 10.0f;
    constexpr float ROW_WIDTH = 640.0f;
    constexpr float PLAY_BUTTON_WIDTH = 140.0f;
    constexpr float DELETE_BUTTON_WIDTH = 140.0f;
    constexpr float BOTTOM_BUTTON_WIDTH = 220.0f;
    constexpr float BOTTOM_BUTTON_HEIGHT = 48.0f;
    constexpr int TITLE_FONT_SIZE = 36;
    constexpr int EMPTY_FONT_SIZE = 20;
}

void WorldListScreen::enter()
{
    worlds = WorldSave::list_worlds();
    pending_delete_folder.reset();
}

WorldListScreen::Action WorldListScreen::update()
{
    int screen_width = GetScreenWidth();
    int screen_height = GetScreenHeight();
    float list_x = (screen_width - ROW_WIDTH) / 2.0f;

    ui::label({0.0f, 40.0f, static_cast<float>(screen_width), 50.0f}, "Одиночная игра", TITLE_FONT_SIZE, WHITE);

    Action result;

    float list_top = 120.0f;
    float list_bottom = screen_height - 100.0f;
    int max_rows = std::max(0, static_cast<int>((list_bottom - list_top) / (ROW_HEIGHT + ROW_SPACING)));

    if (worlds.empty()) {
        ui::label({0.0f, list_top, static_cast<float>(screen_width), 40.0f}, "Пока нет сохранённых миров", EMPTY_FONT_SIZE, GRAY);
    }

    float y = list_top;
    int shown = 0;
    for (auto& world : worlds) {
        if (shown >= max_rows) break;
        ++shown;

        Rectangle name_bounds = {list_x, y, ROW_WIDTH - PLAY_BUTTON_WIDTH - DELETE_BUTTON_WIDTH - 20.0f, ROW_HEIGHT};
        Rectangle play_bounds = {name_bounds.x + name_bounds.width + 10.0f, y, PLAY_BUTTON_WIDTH, ROW_HEIGHT};
        Rectangle delete_bounds = {play_bounds.x + PLAY_BUTTON_WIDTH + 10.0f, y, DELETE_BUTTON_WIDTH, ROW_HEIGHT};

        ui::panel(name_bounds, Color{40, 40, 46, 255});
        char label_text[192];
        std::snprintf(label_text, sizeof(label_text), "%s  (seed %u)", world.display_name.c_str(), world.seed);
        ui::label(name_bounds, label_text, 20, WHITE);

        if (ui::button(play_bounds, "Играть")) {
            result = {ActionType::LoadWorld, world.folder_name};
            pending_delete_folder.reset();
        }

        bool armed = pending_delete_folder && *pending_delete_folder == world.folder_name;
        if (armed) {
            if (ui::button(delete_bounds, "Точно?")) {
                WorldSave::delete_world(world.folder_name);
                enter(); // refresh from disk - `worlds` is invalidated, stop iterating it now
                return {ActionType::None, {}};
            }
        } else {
            if (ui::button(delete_bounds, "Удалить")) {
                pending_delete_folder = world.folder_name;
            }
        }

        y += ROW_HEIGHT + ROW_SPACING;
    }

    float bottom_y = screen_height - BOTTOM_BUTTON_HEIGHT - 30.0f;
    float create_x = screen_width / 2.0f - BOTTOM_BUTTON_WIDTH - 10.0f;
    float back_x = screen_width / 2.0f + 10.0f;

    if (ui::button({create_x, bottom_y, BOTTOM_BUTTON_WIDTH, BOTTOM_BUTTON_HEIGHT}, "Создать мир")) {
        result = {ActionType::CreateWorld, {}};
        pending_delete_folder.reset();
    }
    if (ui::button({back_x, bottom_y, BOTTOM_BUTTON_WIDTH, BOTTOM_BUTTON_HEIGHT}, "Назад")) {
        result = {ActionType::Back, {}};
        pending_delete_folder.reset();
    }

    return result;
}
