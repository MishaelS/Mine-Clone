#pragma once

#include "core/Keybindings.hpp"
#include "core/Settings.hpp"

#include <optional>

// Keybinding remap + render/fog distance + texture filter + target FPS.
// Every change is applied immediately (texture filter/FPS take effect live;
// render/fog distance have nothing to apply to until a world exists - see
// GameEngine::start_singleplayer_world()) and persisted via SettingsIO::save
// right away, not just when leaving the screen.
class SettingsScreen {
public:
    enum class ActionType { None, Back };
    struct Action { ActionType type = ActionType::None; };

    // Edits `settings` in place. Reads/writes GameEngine's one Settings
    // instance directly rather than a private copy, so a change is visible
    // immediately to whatever reads Settings elsewhere (e.g. the next
    // world started).
    Action update(Settings& settings);

private:
    enum class Section { Controls, Graphics, Sound };
    Section section = Section::Graphics;

    // Which action (if any) is currently waiting for its next key/mouse
    // press to become its new binding - see the .cpp for why update() must
    // check this *before* any row's own button() call can newly set it.
    std::optional<GameAction> rebinding_action;
};
