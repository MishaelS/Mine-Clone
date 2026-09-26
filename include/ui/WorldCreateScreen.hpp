#pragma once

#include "core/WorldSave.hpp"
#include "ui/CustomWorldScreen.hpp"
#include "ui/Widgets.hpp"

#include <string>

// World name + seed + game mode + world type (generator preset),
// "Создать мир" / "Отмена". Reachable from
// WorldListScreen's "Создать мир" button.
class WorldCreateScreen {
public:
    enum class ActionType { None, Create, Cancel };
    struct Action {
        ActionType type = ActionType::None;
        WorldInfo world; // set for Create
    };

    // Resets both text fields, mode, and any error message - call every
    // time this screen becomes active (GameEngine::enter_state()).
    void enter();

    Action update();

private:
    ui::TextInputState name_field;
    ui::TextInputState seed_field;
    GameMode selected_mode = GameMode::Creative;
    WorldType selected_type = WorldType::Normal;
    CustomWorld custom_world;            // WorldType::Custom's setup, edited on custom_screen
    CustomWorldScreen custom_screen;
    bool custom_screen_open = false;
    bool allow_commands = false;
    int focused_field = 0; // 0 = name, 1 = seed
    std::string error_message;
};
