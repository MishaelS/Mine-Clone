#include "core/Keybindings.hpp"

#include "raylib.h"

bool binding_down(const Binding& binding)
{
    if (binding.kind == BindingKind::MouseButton) return IsMouseButtonDown(binding.code);
    return IsKeyDown(binding.code);
}

bool binding_pressed(const Binding& binding)
{
    if (binding.kind == BindingKind::MouseButton) return IsMouseButtonPressed(binding.code);
    return IsKeyPressed(binding.code);
}

std::optional<Binding> poll_any_binding_pressed()
{
    for (int button = MOUSE_BUTTON_LEFT; button <= MOUSE_BUTTON_MIDDLE; ++button) {
        if (IsMouseButtonPressed(button)) return Binding{BindingKind::MouseButton, button};
    }

    int key = GetKeyPressed();
    while (key != 0) {
        if (key != KEY_ESCAPE) return Binding{BindingKind::Key, key};
        key = GetKeyPressed(); // drain the rest of the queue even after skipping Esc
    }
    return std::nullopt;
}

namespace {
    constexpr const char* ACTION_JSON_KEYS[] = {
        "move_forward",
        "move_backward",
        "move_left",
        "move_right",
        "jump", "sneak", "sprint", "break_block", "place_block",
        "toggle_inventory", "drop_item", "open_chat",
    };
}

const char* game_action_json_key(GameAction action)
{
    return ACTION_JSON_KEYS[static_cast<size_t>(action)];
}

std::array<Binding, static_cast<size_t>(GameAction::Count)> default_keybindings()
{
    std::array<Binding, static_cast<size_t>(GameAction::Count)> bindings{};
    bindings[static_cast<size_t>(GameAction::MoveForward)]  = {BindingKind::Key, KEY_W};
    bindings[static_cast<size_t>(GameAction::MoveBackward)] = {BindingKind::Key, KEY_S};
    bindings[static_cast<size_t>(GameAction::MoveLeft)]     = {BindingKind::Key, KEY_A};
    bindings[static_cast<size_t>(GameAction::MoveRight)]    = {BindingKind::Key, KEY_D};
    bindings[static_cast<size_t>(GameAction::Jump)]         = {BindingKind::Key, KEY_SPACE};
    bindings[static_cast<size_t>(GameAction::Sneak)]        = {BindingKind::Key, KEY_LEFT_SHIFT};
    bindings[static_cast<size_t>(GameAction::Sprint)]       = {BindingKind::Key, KEY_LEFT_CONTROL};
    bindings[static_cast<size_t>(GameAction::BreakBlock)]   = {BindingKind::MouseButton, MOUSE_BUTTON_LEFT};
    bindings[static_cast<size_t>(GameAction::PlaceBlock)]   = {BindingKind::MouseButton, MOUSE_BUTTON_RIGHT};
    bindings[static_cast<size_t>(GameAction::ToggleInventory)] = {BindingKind::Key, KEY_E};
    bindings[static_cast<size_t>(GameAction::DropItem)]        = {BindingKind::Key, KEY_Q};
    bindings[static_cast<size_t>(GameAction::OpenChat)]        = {BindingKind::Key, KEY_T};
    return bindings;
}
