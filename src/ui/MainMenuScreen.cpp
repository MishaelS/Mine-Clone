#include "ui/MainMenuScreen.hpp"
#include "ui/Widgets.hpp"
#include "core/TextureManager.hpp"

#include "raylib.h"

#include <algorithm>

namespace {
    constexpr float BUTTON_WIDTH = 320.0f;
    constexpr float BUTTON_HEIGHT = 52.0f;
    constexpr float BUTTON_SPACING = 16.0f;

    const char* LOGO_TEXTURE_PATH = "sprites/gui/titleLogo.png";
    constexpr float LOGO_WIDTH_FRACTION = 0.5f; // of screen width, before the cap below
    constexpr float LOGO_MAX_WIDTH = 560.0f;
    constexpr float LOGO_TOP_FRACTION = 0.10f; // of screen height
}

MainMenuScreen::Action MainMenuScreen::update()
{
    int screen_width = GetScreenWidth();
    int screen_height = GetScreenHeight();

    const Texture2D& logo = TextureManager::get(LOGO_TEXTURE_PATH);
    float logo_width = std::min(static_cast<float>(screen_width) * LOGO_WIDTH_FRACTION, LOGO_MAX_WIDTH);
    float logo_height = logo_width * (static_cast<float>(logo.height) / static_cast<float>(logo.width));
    Rectangle logo_source = {0.0f, 0.0f, static_cast<float>(logo.width), static_cast<float>(logo.height)};
    Rectangle logo_destination = {
        (screen_width - logo_width) / 2.0f, screen_height * LOGO_TOP_FRACTION, logo_width, logo_height,
    };
    DrawTexturePro(logo, logo_source, logo_destination, {0.0f, 0.0f}, 0.0f, WHITE);

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
