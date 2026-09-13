#include "ui/PauseMenuScreen.hpp"
#include "ui/Widgets.hpp"
#include "ui/Localization.hpp"

#include "raylib.h"

namespace {
    constexpr float BUTTON_WIDTH = 400.0f;
    constexpr float BUTTON_HEIGHT = ui::BUTTON_HEIGHT;
    constexpr float BUTTON_SPACING = ui::BUTTON_GAP;
    constexpr Color OVERLAY_COLOR = {0, 0, 0, 105};
}

PauseMenuScreen::Action PauseMenuScreen::update()
{
    int screen_width  = GetScreenWidth();
    int screen_height = GetScreenHeight();
    const float scale = ui::scale_factor();
    const float button_width   = BUTTON_WIDTH * scale;
    const float button_height  = BUTTON_HEIGHT * scale;
    const float button_spacing = BUTTON_SPACING * scale;

    ui::panel({0.0f, 0.0f, static_cast<float>(screen_width), static_cast<float>(screen_height)}, OVERLAY_COLOR);
    ui::label({0.0f, screen_height * 0.2f, static_cast<float>(screen_width), ui::scaled(60.0f)},
              ui::tr("pause.title"));

    float x = (screen_width - button_width) / 2.0f;
    float y = screen_height * 0.42f;

    Action result = Action::None;

    if (ui::button({x, y, button_width, button_height}, ui::tr("pause.resume")) || IsKeyPressed(KEY_ESCAPE)) result = Action::Resume;
    y += button_height + button_spacing + ui::scaled(36);

    if (ui::button({x, y, button_width, button_height}, ui::tr("main.settings"))) result = Action::Settings;
    y += button_height + button_spacing;

    if (ui::button({x, y, button_width, button_height}, ui::tr("pause.save_quit"))) result = Action::MainMenu;

    return result;
}
