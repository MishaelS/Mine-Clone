#pragma once

#include <cstdint>

// Top-level phase GameEngine::run() is in this frame - which screen (if any)
// gets input/drawn, and whether the fixed-tick world simulation runs at all.
// See GameEngine::run()/update_and_draw_menu()/enter_state().
enum class GameState : uint8_t {
    MainMenu,
    WorldList,
    WorldCreate,
    Settings,
    Playing,
    Paused, // Esc during Playing - see GameEngine::update()/return_to_main_menu()
};
