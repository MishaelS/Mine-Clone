#include "ui/MainMenuScreen.hpp"
#include "ui/Widgets.hpp"
#include "core/FontManager.hpp"

#include "raylib.h"

namespace {
    constexpr float BUTTON_WIDTH = 320.0f;
    constexpr float BUTTON_HEIGHT = 52.0f;
    constexpr float BUTTON_SPACING = 16.0f;
    constexpr int TITLE_FONT_SIZE = 48;
}

MainMenuScreen::Action MainMenuScreen::update()
{
    int screen_width = GetScreenWidth();
    int screen_height = GetScreenHeight();

    ui::label({0.0f, screen_height * 0.18f, static_cast<float>(screen_width), 60.0f},
               "Mine-Clone", TITLE_FONT_SIZE, WHITE);

    float x = (screen_width - BUTTON_WIDTH) / 2.0f;
    float y = screen_height * 0.42f;

    Action result = Action::None;

    if (ui::button({x, y, BUTTON_WIDTH, BUTTON_HEIGHT}, "Одиночная игра")) result = Action::Singleplayer;
    y += BUTTON_HEIGHT + BUTTON_SPACING;

    // Not implemented yet - drawn disabled so it's visibly inert instead of
    // silently doing nothing when clicked.
    ui::button({x, y, BUTTON_WIDTH, BUTTON_HEIGHT}, "Сетевая игра", false, false);
    y += BUTTON_HEIGHT + BUTTON_SPACING;

    if (ui::button({x, y, BUTTON_WIDTH, BUTTON_HEIGHT}, "Настройки")) result = Action::Settings;
    y += BUTTON_HEIGHT + BUTTON_SPACING;

    if (ui::button({x, y, BUTTON_WIDTH, BUTTON_HEIGHT}, "Закрыть игру")) result = Action::Quit;

    return result;
}
