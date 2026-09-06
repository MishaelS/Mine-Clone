#include "ui/WorldCreateScreen.hpp"
#include "ui/Widgets.hpp"

#include "raylib.h"

namespace {
    constexpr float FIELD_WIDTH = 460.0f;
    constexpr float FIELD_HEIGHT = 44.0f;
    constexpr float MODE_BUTTON_WIDTH = 220.0f;
    constexpr float MODE_BUTTON_HEIGHT = 44.0f;
    constexpr float BOTTOM_BUTTON_WIDTH = 200.0f;
    constexpr float BOTTOM_BUTTON_HEIGHT = 48.0f;
    constexpr int TITLE_FONT_SIZE = 36;
    constexpr int LABEL_FONT_SIZE = 18;
    constexpr int ERROR_FONT_SIZE = 18;

    bool is_blank(const std::string& text)
    {
        return text.find_first_not_of(" \t\r\n") == std::string::npos;
    }
}

void WorldCreateScreen::enter()
{
    name_field = ui::TextInputState{};
    seed_field = ui::TextInputState{};
    seed_field.max_codepoints = 24;
    selected_mode = GameMode::Creative;
    focused_field = 0;
    error_message.clear();
}

WorldCreateScreen::Action WorldCreateScreen::update()
{
    int screen_width = GetScreenWidth();
    int screen_height = GetScreenHeight();
    float center_x = screen_width / 2.0f;

    ui::label({0.0f, screen_height * 0.14f, static_cast<float>(screen_width), 50.0f}, "Создание мира", TITLE_FONT_SIZE, WHITE);

    float field_x = center_x - FIELD_WIDTH / 2.0f;
    float y = screen_height * 0.30f;

    ui::label({field_x, y - 24.0f, FIELD_WIDTH, 20.0f}, "Название мира", LABEL_FONT_SIZE, LIGHTGRAY);
    Rectangle name_bounds = {field_x, y, FIELD_WIDTH, FIELD_HEIGHT};
    if (ui::text_input(name_bounds, name_field, focused_field == 0)) focused_field = 0;

    y += FIELD_HEIGHT + 50.0f;
    ui::label({field_x, y - 24.0f, FIELD_WIDTH, 20.0f}, "Сид (необязательно)", LABEL_FONT_SIZE, LIGHTGRAY);
    Rectangle seed_bounds = {field_x, y, FIELD_WIDTH, FIELD_HEIGHT};
    if (ui::text_input(seed_bounds, seed_field, focused_field == 1)) focused_field = 1;

    y += FIELD_HEIGHT + 50.0f;
    ui::label({field_x, y - 24.0f, FIELD_WIDTH, 20.0f}, "Режим игры", LABEL_FONT_SIZE, LIGHTGRAY);
    Rectangle creative_bounds = {field_x, y, MODE_BUTTON_WIDTH, MODE_BUTTON_HEIGHT};
    Rectangle survival_bounds = {field_x + FIELD_WIDTH - MODE_BUTTON_WIDTH, y, MODE_BUTTON_WIDTH, MODE_BUTTON_HEIGHT};
    if (ui::button(creative_bounds, "Творческий", selected_mode == GameMode::Creative)) {
        selected_mode = GameMode::Creative;
    }
    // Survival has no real mechanics yet - selectable (the choice is saved
    // for later) but drawn as a stub, same spirit as Multiplayer's own
    // disabled main-menu button.
    if (ui::button(survival_bounds, "Выживание (скоро)", selected_mode == GameMode::Survival)) {
        selected_mode = GameMode::Survival;
    }

    if (!error_message.empty()) {
        y += MODE_BUTTON_HEIGHT + 24.0f;
        ui::label({field_x, y, FIELD_WIDTH, 20.0f}, error_message, ERROR_FONT_SIZE, RED);
    }

    Action result;
    float bottom_y = screen_height - BOTTOM_BUTTON_HEIGHT - 40.0f;
    float create_x = center_x - BOTTOM_BUTTON_WIDTH - 10.0f;
    float cancel_x = center_x + 10.0f;

    if (ui::button({create_x, bottom_y, BOTTOM_BUTTON_WIDTH, BOTTOM_BUTTON_HEIGHT}, "Создать мир")) {
        if (is_blank(name_field.text)) {
            error_message = "Введите название мира";
        } else {
            WorldInfo info;
            info.display_name = name_field.text;
            info.folder_name = WorldSave::next_available_folder_name(name_field.text);
            info.seed = WorldSave::derive_seed(name_field.text, seed_field.text);
            info.game_mode = selected_mode;
            result = {ActionType::Create, info};
        }
    }
    if (ui::button({cancel_x, bottom_y, BOTTOM_BUTTON_WIDTH, BOTTOM_BUTTON_HEIGHT}, "Отмена")) {
        result = {ActionType::Cancel, {}};
    }

    return result;
}
