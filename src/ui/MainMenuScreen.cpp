#include "ui/MainMenuScreen.hpp"
#include "ui/Widgets.hpp"
#include "ui/Localization.hpp"
#include "core/TextureManager.hpp"

#include "raylib.h"

#include <algorithm>

namespace {
    constexpr float BUTTON_WIDTH   = 400.0f;
    constexpr float BUTTON_HEIGHT  = ui::BUTTON_HEIGHT;
    constexpr float BUTTON_SPACING = ui::BUTTON_GAP;

    const char* LOGO_TEXTURE_PATH = "sprites/gui/titleLogo.png";
    constexpr float LOGO_WIDTH_FRACTION = 0.5f; // of screen width, before the cap below
    constexpr float LOGO_MAX_WIDTH      = 560.0f;
    constexpr float LOGO_TOP_FRACTION   = 0.10f; // of screen height
}

MainMenuScreen::Action MainMenuScreen::update()
{
    int screen_width  = GetScreenWidth();
    int screen_height = GetScreenHeight();
    const float scale = ui::scale_factor();

    const Texture2D& logo = TextureManager::get(LOGO_TEXTURE_PATH);
    float logo_width  = std::min(static_cast<float>(screen_width) * LOGO_WIDTH_FRACTION,
                                 LOGO_MAX_WIDTH * scale);
    float logo_height = logo_width * (static_cast<float>(logo.height) / static_cast<float>(logo.width));
    Rectangle logo_source = {0.0f, 0.0f, static_cast<float>(logo.width), static_cast<float>(logo.height)};
    Rectangle logo_destination = {
        (screen_width - logo_width) / 2.0f, screen_height * LOGO_TOP_FRACTION, logo_width, logo_height,
    };
    DrawTexturePro(logo, logo_source, logo_destination, {0.0f, 0.0f}, 0.0f, WHITE);

    const float button_width   = BUTTON_WIDTH   * scale;
    const float button_height  = BUTTON_HEIGHT  * scale;
    const float button_spacing = BUTTON_SPACING * scale;
    float x = (screen_width - button_width) / 2.0f;
    float y = screen_height * 0.42f;

    Action result = Action::None;

    if (ui::button({x, y, button_width, button_height}, ui::tr("main.singleplayer"))) result = Action::Singleplayer;
    y += button_height + button_spacing;

    // Not implemented yet - drawn disabled so it's visibly inert instead of
    // silently doing nothing when clicked.
    ui::button({x, y, button_width, button_height}, ui::tr("main.multiplayer"), false, false);
    y += button_height + button_spacing + ui::scaled(24);

    const float half_width = (button_width - button_spacing) * 0.5f;
    if (ui::button({x, y, half_width, button_height}, ui::tr("main.settings"))) result = Action::Settings;
    if (ui::button({x + half_width + button_spacing, y, half_width, button_height}, ui::tr("main.quit"))) result = Action::Quit;

    return result;
}
