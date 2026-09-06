#include "ui/PauseMenuScreen.hpp"
#include "ui/Widgets.hpp"

#include "raylib.h"

namespace {
    constexpr float BUTTON_WIDTH = 320.0f;
    constexpr float BUTTON_HEIGHT = 52.0f;
    constexpr float BUTTON_SPACING = 16.0f;
    constexpr int TITLE_FONT_SIZE = 44;
    constexpr Color OVERLAY_COLOR = {0, 0, 0, 160}; // dims the flat background behind the buttons
}

PauseMenuScreen::Action PauseMenuScreen::update()
{
    int screen_width = GetScreenWidth();
    int screen_height = GetScreenHeight();

    ui::panel({0.0f, 0.0f, static_cast<float>(screen_width), static_cast<float>(screen_height)}, OVERLAY_COLOR);
    ui::label({0.0f, screen_height * 0.2f, static_cast<float>(screen_width), 60.0f}, "Пауза", TITLE_FONT_SIZE, WHITE);

    float x = (screen_width - BUTTON_WIDTH) / 2.0f;
    float y = screen_height * 0.42f;

    Action result = Action::None;

    if (ui::button({x, y, BUTTON_WIDTH, BUTTON_HEIGHT}, "Продолжить игру")) result = Action::Resume;
    y += BUTTON_HEIGHT + BUTTON_SPACING;

    if (ui::button({x, y, BUTTON_WIDTH, BUTTON_HEIGHT}, "Настройки")) result = Action::Settings;
    y += BUTTON_HEIGHT + BUTTON_SPACING;

    if (ui::button({x, y, BUTTON_WIDTH, BUTTON_HEIGHT}, "Выйти и сохранить игру")) result = Action::MainMenu;

    return result;
}
