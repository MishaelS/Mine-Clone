#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Every gameplay action a player can rebind in the Settings screen - the
// free-look camera's own movement, the two mouse-button actions (break/
// place), and a few single-key UI actions (inventory/drop/chat). Debug
// toggles (F3/F4/F5/F6) are deliberately not here: they stay hardcoded in
// GameEngine::update(), not exposed for rebinding.
enum class GameAction : uint8_t {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    Jump,
    Sneak,
    Sprint,
    BreakBlock,
    PlaceBlock,
    ToggleInventory,
    DropItem,
    OpenChat,
    Count, // not a real action; sentinel for array sizing
};

// A binding can be either a keyboard key or a mouse button - break/place are
// mouse buttons today, movement is keyboard, and a player should be free to
// rebind either action onto either kind of input.
enum class BindingKind : uint8_t { Key, MouseButton };

struct Binding {
    BindingKind kind = BindingKind::Key;
    int code = 0; // a raylib KeyboardKey or MouseButton value, per `kind`
};

// IsKeyDown/IsMouseButtonDown and IsKeyPressed/IsMouseButtonPressed,
// dispatched on `kind` - replaces GameEngine::update()'s old hardcoded reads.
bool binding_down(const Binding& binding);
bool binding_pressed(const Binding& binding);

// Short label for a settings row, e.g. "W", "Shift", "Мышь: ЛКМ".
std::string binding_display_name(const Binding& binding, bool english = false);

// The first key or mouse button newly pressed this frame, if any - drains
// GetKeyPressed()'s queue and scans the mouse buttons. Skips KEY_ESCAPE,
// which cancels a rebind-in-progress instead of becoming its new binding.
// Used only by SettingsScreen's "click to rebind, then capture" flow.
std::optional<Binding> poll_any_binding_pressed();

// Stable string key for this action in settings.json - see Settings.cpp.
const char* game_action_json_key(GameAction action);

// Russian label for this action's row in the Settings screen.
const char* game_action_display_name(GameAction action, bool english = false);

// W/S/A/D + Space/Shift/Ctrl, mouse Left/Right for break/place, E/Q/T for
// inventory/drop/chat. MoveLeft/MoveRight keep driving `movement.y` (not
// `.x`) via UpdateCameraPro - unchanged behavior, just named.
std::array<Binding, static_cast<size_t>(GameAction::Count)> default_keybindings();
