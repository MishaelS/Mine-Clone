#pragma once

#include "core/WorldSave.hpp"

#include <optional>
#include <string>
#include <vector>

// Lists saved worlds (WorldSave::list_worlds()) with per-row Play/Delete,
// plus "Create World" and "Back". Reachable from MainMenuScreen's
// Singleplayer button - see GameEngine::update_and_draw_menu().
class WorldListScreen {
public:
    enum class ActionType { None, LoadWorld, CreateWorld, Back };
    struct Action {
        ActionType type = ActionType::None;
        std::string folder_name; // set for LoadWorld
    };

    // Refreshes the world list from disk - call every time this screen
    // becomes active (GameEngine::enter_state()), not just once, so a
    // world created or deleted elsewhere is reflected immediately.
    void enter();

    Action update();

private:
    std::vector<WorldInfo> worlds;

    // First "Удалить" click on a row arms it for a two-click confirm
    // (re-rendered as "Точно?"); a second click on that *same* row
    // actually deletes. Clicking anything else clears this first, without
    // acting, so a stray click can never confirm the wrong world.
    std::optional<std::string> pending_delete_folder;
};
