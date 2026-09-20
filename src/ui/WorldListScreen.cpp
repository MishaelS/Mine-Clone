#include "ui/WorldListScreen.hpp"
#include "ui/Widgets.hpp"
#include "ui/Localization.hpp"
#include "core/TextureManager.hpp"

#include <algorithm>
#include <cmath>

namespace {
    const char* WORLD_ICON_TEXTURE_PATH = "sprites/gui/missingWorldIcon.png";
}

void WorldListScreen::enter()
{
    worlds = WorldSave::list_worlds();
    pending_delete_folder.reset();
    selected_world.reset();
    first_visible = 0;
    last_click_time = -1.0;
    last_clicked_world.reset();
}

WorldListScreen::Action WorldListScreen::update()
{
    const float width = ui::scaled(ui::MENU_WIDTH);
    const float x = (GetScreenWidth() - width) * 0.5f;
    const float height = ui::scaled(ui::BUTTON_HEIGHT);
    const float gap = ui::scaled(ui::BUTTON_GAP);
    const float half = (width - gap) * 0.5f;
    ui::label({0, ui::scaled(28), static_cast<float>(GetScreenWidth()), height}, ui::tr("worlds.title"));
    const float top = ui::scaled(100);
    const float bottom = GetScreenHeight() - ui::scaled(136);
    const float row_height = ui::scaled(64);
    const int count = std::max(1, static_cast<int>((bottom - top) / row_height));
    Rectangle list = {x, top, width, bottom - top};
    ui::panel(list, Color{0, 0, 0, 115});
    if (CheckCollisionPointRec(GetMousePosition(), list)) {
        first_visible -= static_cast<int>(std::round(GetMouseWheelMove()));
    }
    first_visible = std::clamp(first_visible, 0, std::max(0, static_cast<int>(worlds.size()) - count));

    if (worlds.empty()) ui::label({x, top, width, height}, ui::tr("worlds.empty"), LIGHTGRAY);
    for (int row = 0; row < count && first_visible + row < static_cast<int>(worlds.size()); ++row) {
        const size_t index = static_cast<size_t>(first_visible + row);
        const WorldInfo& world = worlds[index];
        Rectangle bounds = {x + ui::scaled(4), top + row * row_height + ui::scaled(2), width - ui::scaled(8), row_height - ui::scaled(4)};
        if (CheckCollisionPointRec(GetMousePosition(), bounds) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            constexpr double DOUBLE_CLICK_SECONDS = 0.3;
            const double now = GetTime();
            if (last_clicked_world == index && now - last_click_time <= DOUBLE_CLICK_SECONDS) {
                return {ActionType::LoadWorld, world.folder_name};
            }
            last_click_time = now;
            last_clicked_world = index;
            selected_world = index;
            pending_delete_folder.reset();
        }
        if (selected_world && *selected_world == index) {
            ui::panel(bounds, Color{0, 0, 0, 180});
            DrawRectangleLinesEx(bounds, ui::scaled(2), LIGHTGRAY);
        }
        // World icon, left of its name - always the "no screenshot yet"
        // placeholder for now, since nothing captures a per-world image.
        const Texture2D& icon = TextureManager::get(WORLD_ICON_TEXTURE_PATH);
        const float icon_size = bounds.height - ui::scaled(8);
        const Rectangle icon_destination = {
            bounds.x + ui::scaled(4), bounds.y + (bounds.height - icon_size) * 0.5f, icon_size, icon_size,
        };
        DrawTexturePro(icon, {0.0f, 0.0f, static_cast<float>(icon.width), static_cast<float>(icon.height)},
                       icon_destination, {0.0f, 0.0f}, 0.0f, WHITE);

        const float text_x = icon_destination.x + icon_size + ui::scaled(8);
        const float text_width = bounds.x + bounds.width - ui::scaled(6) - text_x;
        ui::label({text_x, bounds.y + ui::scaled(4), text_width, ui::scaled(26)},
                  world.display_name, WHITE, ui::TextAlign::Left);
        ui::label({text_x, bounds.y + ui::scaled(30), text_width, ui::scaled(24)},
                  ui::tr(world.game_mode == GameMode::Survival ? "mode.survival" : "mode.creative") +
                  " / " + ui::tr("worlds.seed") + ": " + std::to_string(world.seed), GRAY, ui::TextAlign::Left);
    }

    const bool has_selection = selected_world && *selected_world < worlds.size();
    const float footer = GetScreenHeight() - ui::scaled(116);
    if (ui::button({x, footer, half, height}, ui::tr("worlds.play"), false, has_selection) ||
        (has_selection && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))))
        return {ActionType::LoadWorld, worlds[*selected_world].folder_name};
    if (ui::button({x + half + gap, footer, half, height}, ui::tr("worlds.create")))
        return {ActionType::CreateWorld, {}};
    const bool armed = has_selection && pending_delete_folder &&
                       *pending_delete_folder == worlds[*selected_world].folder_name;
    if (ui::button({x, footer + height + gap, half, height}, ui::tr(armed ? "worlds.confirm" : "worlds.delete"), false, has_selection)) {
        if (armed) {
            WorldSave::delete_world(worlds[*selected_world].folder_name);
            enter();
        } else pending_delete_folder = worlds[*selected_world].folder_name;
    }
    if (ui::button({x + half + gap, footer + height + gap, half, height}, ui::tr("common.back")) || IsKeyPressed(KEY_ESCAPE))
        return {ActionType::Back, {}};
    return {};
}
