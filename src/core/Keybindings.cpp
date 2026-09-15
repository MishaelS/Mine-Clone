#include "core/Keybindings.hpp"

#include "raylib.h"

#include <cstdio>

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

namespace {
    // Only the keys default_keybindings() (or a reasonable rebind) actually
    // uses need a friendly name - anything else falls back to its raw
    // raylib key code so the row still shows *something* legible.
    const char* key_display_name(int key, bool english) {
        switch (key) {
            case KEY_W           : return "W";
            case KEY_A           : return "A";
            case KEY_S           : return "S";
            case KEY_D           : return "D";
            case KEY_SPACE       : return english ? "Space" : "Пробел";
            case KEY_LEFT_SHIFT  : return "Shift";
            case KEY_LEFT_CONTROL: return "Ctrl";
            case KEY_LEFT_ALT    : return "Alt";
            case KEY_TAB         : return "Tab";
            case KEY_UP          : return english ? "Up" : "Стрелка вверх";
            case KEY_DOWN        : return english ? "Down" : "Стрелка вниз";
            case KEY_LEFT        : return english ? "Left" : "Стрелка влево";
            case KEY_RIGHT       : return english ? "Right" : "Стрелка вправо";
            default:
                return nullptr;
        }
    }

    const char* mouse_display_name(int button, bool english) {
        switch (button) {
            case MOUSE_BUTTON_LEFT  : return english ? "Mouse: Left" : "Мышь: ЛКМ";
            case MOUSE_BUTTON_RIGHT : return english ? "Mouse: Right" : "Мышь: ПКМ";
            case MOUSE_BUTTON_MIDDLE: return english ? "Mouse: Middle" : "Мышь: СКМ";
            default: return english ? "Mouse button" : "Мышь: кнопка";
        }
    }
}

std::string binding_display_name(const Binding& binding, bool english)
{
    if (binding.kind == BindingKind::MouseButton) return mouse_display_name(binding.code, english);

    if (const char* name = key_display_name(binding.code, english)) return name;

    // Printable ASCII keys (letters/digits not already named above, plus
    // punctuation) - raylib's KeyboardKey values for these match their own
    // ASCII codepoint, so this covers any of them without a giant switch.
    if (binding.code >= 32 && binding.code < 127) {
        return std::string(1, static_cast<char>(binding.code));
    }

    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), english ? "Key #%d" : "Клавиша #%d", binding.code);
    return buffer;
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

    constexpr const char* ACTION_DISPLAY_NAMES[] = {
        "Вперёд",
        "Назад",
        "Влево",
        "Вправо",
        "Прыжок", "Красться / вниз", "Бег", "Ломать блок", "Ставить блок",
        "Инвентарь", "Выбросить предмет", "Открыть чат",
    };
    constexpr const char* ACTION_DISPLAY_NAMES_EN[] = {
        "Forward", "Back", "Left", "Right", "Jump", "Sneak / down", "Sprint", "Break block", "Place block",
        "Inventory", "Drop item", "Open chat",
    };
}

const char* game_action_json_key(GameAction action)
{
    return ACTION_JSON_KEYS[static_cast<size_t>(action)];
}

const char* game_action_display_name(GameAction action, bool english)
{
    return (english ? ACTION_DISPLAY_NAMES_EN : ACTION_DISPLAY_NAMES)[static_cast<size_t>(action)];
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
