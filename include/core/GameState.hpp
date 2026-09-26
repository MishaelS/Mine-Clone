#pragma once

#include <cstdint>

// Top-level phase Application::run() is in this frame - which screen (if
// any) gets input/drawn, and whether the game (GameEngine) runs at all.
// See Application::run()/update_and_draw_menu()/enter_state().
enum class GameState : uint8_t {
    MainMenu,
    WorldList,
    WorldCreate,
    Settings,
    Playing,
    Paused, // Esc during Playing - see GameEngine::take_pause_request()/Application::return_to_main_menu()
};
