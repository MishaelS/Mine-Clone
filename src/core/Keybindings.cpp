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
    const char* key_display_name(int key)
    {
        switch (key) {
            case KEY_W: return "W";
            case KEY_A: return "A";
            case KEY_S: return "S";
            case KEY_D: return "D";
            case KEY_SPACE: return "Пробел";
            case KEY_LEFT_SHIFT: return "Shift";
            case KEY_LEFT_CONTROL: return "Ctrl";
            case KEY_LEFT_ALT: return "Alt";
            case KEY_TAB: return "Tab";
            case KEY_UP: return "Стрелка вверх";
            case KEY_DOWN: return "Стрелка вниз";
            case KEY_LEFT: return "Стрелка влево";
            case KEY_RIGHT: return "Стрелка вправо";
            default: return nullptr;
        }
    }

    const char* mouse_display_name(int button)
    {
        switch (button) {
            case MOUSE_BUTTON_LEFT: return "Мышь: ЛКМ";
            case MOUSE_BUTTON_RIGHT: return "Мышь: ПКМ";
            case MOUSE_BUTTON_MIDDLE: return "Мышь: СКМ";
            default: return "Мышь: кнопка";
        }
    }
}

std::string binding_display_name(const Binding& binding)
{
    if (binding.kind == BindingKind::MouseButton) return mouse_display_name(binding.code);

    if (const char* name = key_display_name(binding.code)) return name;

    // Printable ASCII keys (letters/digits not already named above, plus
    // punctuation) - raylib's KeyboardKey values for these match their own
    // ASCII codepoint, so this covers any of them without a giant switch.
    if (binding.code >= 32 && binding.code < 127) {
        return std::string(1, static_cast<char>(binding.code));
    }

    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "Клавиша #%d", binding.code);
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
        "move_forward", "move_backward", "move_left", "move_right",
        "fly_up", "fly_down", "break_block", "place_block",
    };
    constexpr const char* ACTION_DISPLAY_NAMES[] = {
        "Вперёд", "Назад", "Влево", "Вправо",
        "Полёт вверх", "Полёт вниз", "Ломать блок", "Ставить блок",
    };
}

const char* game_action_json_key(GameAction action)
{
    return ACTION_JSON_KEYS[static_cast<size_t>(action)];
}

const char* game_action_display_name(GameAction action)
{
    return ACTION_DISPLAY_NAMES[static_cast<size_t>(action)];
}

std::array<Binding, static_cast<size_t>(GameAction::Count)> default_keybindings()
{
    std::array<Binding, static_cast<size_t>(GameAction::Count)> bindings{};
    bindings[static_cast<size_t>(GameAction::MoveForward)]  = {BindingKind::Key, KEY_W};
    bindings[static_cast<size_t>(GameAction::MoveBackward)] = {BindingKind::Key, KEY_S};
    bindings[static_cast<size_t>(GameAction::MoveLeft)]     = {BindingKind::Key, KEY_A};
    bindings[static_cast<size_t>(GameAction::MoveRight)]    = {BindingKind::Key, KEY_D};
    bindings[static_cast<size_t>(GameAction::FlyUp)]        = {BindingKind::Key, KEY_SPACE};
    bindings[static_cast<size_t>(GameAction::FlyDown)]      = {BindingKind::Key, KEY_LEFT_SHIFT};
    bindings[static_cast<size_t>(GameAction::BreakBlock)]   = {BindingKind::MouseButton, MOUSE_BUTTON_LEFT};
    bindings[static_cast<size_t>(GameAction::PlaceBlock)]   = {BindingKind::MouseButton, MOUSE_BUTTON_RIGHT};
    return bindings;
}
