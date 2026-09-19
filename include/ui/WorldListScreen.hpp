#pragma once

#include "core/WorldSave.hpp"

#include <optional>
#include <string>
#include <vector>

// Scrollable world list with a selected row and shared footer actions.
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
    std::optional<size_t> selected_world;
    int first_visible = 0;

    // Confirmation is tied to the folder, and cleared when selection changes.
    std::optional<std::string> pending_delete_folder;

    // Double-clicking a row opens that world, same as Play.
    double last_click_time = -1.0;
    std::optional<size_t> last_clicked_world;
};
