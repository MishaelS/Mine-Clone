#pragma once

// Esc-during-Playing screen (GameState::Paused) - Resume / Settings / back
// to the main menu. World simulation stops entirely while this is up (see
// GameEngine::run() - Paused takes the same "not Playing" branch as every
// menu screen, so tick()/update_chunk_states()/update_fluids()/
// update_falling_blocks() simply don't run), the same way real Minecraft's
// own singleplayer pause works.
class PauseMenuScreen {
public:
    enum class Action { None, Resume, Settings, MainMenu };
    Action update();
};
