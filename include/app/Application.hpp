#pragma once

#include "audio/AudioSystem.hpp"
#include "core/GameEngine.hpp"
#include "core/GameState.hpp"
#include "core/Settings.hpp"
#include "ui/LoadingScreen.hpp"
#include "ui/MainMenuScreen.hpp"
#include "ui/PauseMenuScreen.hpp"
#include "ui/SettingsScreen.hpp"
#include "ui/WorldCreateScreen.hpp"
#include "ui/WorldListScreen.hpp"

#include "raylib.h"

#include <memory>
#include <optional>
#include <string>

// The application shell around the game: owns the window and the main
// loop, the settings and audio shared with the game, and every screen
// that isn't the game itself - main menu, world list/creation, settings,
// pause menu and the world loading screen. While Playing it hands each
// frame to GameEngine, which simulates and draws the world and its in-game
// HUD; everything else is drawn here.
class Application {
public:
    Application(int screen_width, int screen_height, const char* title);
    ~Application();

    void run();

private:
    // Draws/updates whichever menu screen `state` currently is (anything
    // but Playing), inside its own BeginDrawing()/EndDrawing() pair.
    // Dispatches each screen's returned action to enter_state()/
    // start_singleplayer_world()/quit_requested.
    void update_and_draw_menu();

    // One Playing frame: GameEngine updates and draws the world, then a
    // pause it asked for (Esc) switches to the pause menu.
    void update_and_draw_game(float delta_time);

    // Switches `state`, plus whatever side effect that transition needs:
    // DisableCursor() only when entering Playing (a menu needs a visible,
    // clickable cursor - EnableCursor() otherwise), and refreshing
    // WorldListScreen/WorldCreateScreen/SettingsScreen when they become
    // active.
    void enter_state(GameState new_state);

    // Has GameEngine open folder_name's world while showing loading_screen,
    // then enters Playing. Must be called outside any BeginDrawing()/
    // EndDrawing() pair (it draws its own frames) - the menus queue it via
    // pending_world_folder.
    void start_singleplayer_world(const std::string& folder_name);

    // "Выйти и сохранить игру" from the pause menu: GameEngine saves and
    // closes the world, then back to MainMenu.
    void return_to_main_menu();

    void release_pause_snapshot();

    GameState   state = GameState::MainMenu;
    Settings    settings;
    AudioSystem audio;

    // Created once the window (and so a GL context) exists - see the
    // constructor. Destroyed before the window closes.
    std::unique_ptr<GameEngine> game;

    MainMenuScreen    main_menu_screen;
    WorldListScreen   world_list_screen;
    WorldCreateScreen world_create_screen;
    SettingsScreen    settings_screen;
    PauseMenuScreen   pause_menu_screen;

    // "Generating/Loading world" screen, drawn from GameEngine::open_world()'s
    // progress callback.
    LoadingScreen loading_screen;
    double last_loading_frame_time = 0.0;
    // Picked in the world list/create screens, started right after that
    // menu frame ends - see start_singleplayer_world()'s own comment.
    std::optional<std::string> pending_world_folder;

    // Frozen, downsampled and softly blurred copy of the last gameplay
    // frame (GameEngine::take_pause_snapshot()). The pause menu draws this
    // instead of the dirt background.
    Texture2D pause_snapshot{};

    // Where SettingsScreen's own "Назад" button returns to - MainMenu when
    // Settings was reached from there, Paused when reached via the in-game
    // Esc menu instead. Set right before every enter_state(Settings) call.
    GameState settings_return_state = GameState::MainMenu;

    bool quit_requested = false; // set by "Закрыть игру" - checked alongside WindowShouldClose() in run()
};
