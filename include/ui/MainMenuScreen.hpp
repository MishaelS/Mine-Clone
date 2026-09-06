#pragma once

// The startup screen: Singleplayer / Multiplayer (stub) / Settings / Quit.
// GameEngine owns one instance for its whole lifetime and calls update()
// once per frame while GameState::MainMenu is active - see
// GameEngine::update_and_draw_menu().
class MainMenuScreen {
public:
    enum class Action { None, Singleplayer, Multiplayer, Settings, Quit };

    // Draws the 4 buttons and returns which one (if any) was clicked this
    // frame.
    Action update();
};
