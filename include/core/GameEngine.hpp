#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "raylib.h"
#include "core/GameObject.hpp"
#include "core/GameState.hpp"
#include "core/Inventory.hpp"
#include "core/Settings.hpp"
#include "ui/MainMenuScreen.hpp"
#include "ui/WorldListScreen.hpp"
#include "ui/WorldCreateScreen.hpp"
#include "ui/SettingsScreen.hpp"
#include "ui/PauseMenuScreen.hpp"
#include "ui/InventoryHud.hpp"
#include "World.hpp"

// Owns the window, the main loop, every GameObject in the game, and the
// voxel World terrain (kept separate from the GameObject list since camera
// interaction with it - aiming, breaking blocks - needs to reach across
// chunk borders in a way a single GameObject's update()/draw() can't). Also
// owns the pre-game menu flow (GameState/the ui:: screen classes) - the
// menu exists entirely before a World does, so it lives here rather than on
// World itself.
class GameEngine {
public:
    GameEngine(int screen_width, int screen_height, const char* title);
    ~GameEngine();

    void run();

    void add_object(std::unique_ptr<GameObject> object);
    void set_world(std::unique_ptr<World> new_world);

private:
    // Fixed-rate game-logic step, called exactly 20 times per second of real
    // time regardless of the render frame rate (see run()) - Minecraft's own
    // tick rate. Nothing hooks into it yet; it's the clock future world
    // simulation (day/night, scheduled block updates, random ticks) will run
    // on, same role Minecraft's tick serves.
    void tick();

    void update(float delta_time);
    void draw();

    // Draws/updates whichever menu screen `state` currently is (anything
    // but Playing) - its own BeginDrawing()/EndDrawing() pair, since
    // run()'s Playing branch already has its own. Dispatches each screen's
    // returned action to enter_state()/start_singleplayer_world()/
    // quit_requested.
    void update_and_draw_menu();

    // Switches `state`, plus whatever side effect that transition needs:
    // DisableCursor() only when entering Playing (a menu needs a visible,
    // clickable cursor - EnableCursor() otherwise), and refreshing
    // WorldListScreen/WorldCreateScreen's own state when they become
    // active.
    void enter_state(GameState new_state);

    // Loads folder_name's WorldInfo, builds a WorldConfig from it plus the
    // current Settings (render/fog distance), constructs the World, and
    // hands it to the existing set_world() - then enters Playing.
    void start_singleplayer_world(const std::string& folder_name);

    // "Выйти и сохранить игру" from the pause menu: saves player state
    // (see save_player_state()), destroys the World (~World() flushes any
    // modified chunks still resident, same as quitting the app outright)
    // and returns to MainMenu.
    void return_to_main_menu();

    // Writes camera position/facing + the current Inventory to
    // saves/<current_world_folder>/player.json (WorldSave::save_player_state)
    // - called from return_to_main_menu() and, since that's not the only
    // way a World can go away, from the destructor too, whenever a World
    // is actually loaded. No-op if !world (nothing to save).
    void save_player_state();

    GameState state = GameState::MainMenu;
    Settings settings;
    MainMenuScreen main_menu_screen;
    WorldListScreen world_list_screen;
    WorldCreateScreen world_create_screen;
    SettingsScreen settings_screen;
    PauseMenuScreen pause_menu_screen;

    // Where SettingsScreen's own "Назад" button returns to - MainMenu when
    // Settings was reached from there, Paused when reached via the in-game
    // Esc menu instead. Set right before every enter_state(Settings) call.
    GameState settings_return_state = GameState::MainMenu;

    bool quit_requested = false; // set by "Закрыть игру" - checked alongside WindowShouldClose() in run()

    // Which saves/<folder>/ the current `world` was loaded from - empty
    // when no world is loaded. Set by start_singleplayer_world(), read by
    // save_player_state().
    std::string current_world_folder;

    // Free-look test camera (WASD + mouse). Swap back to IsoCamera once
    // testing doesn't need to fly around and inspect the world freely.
    Camera3D camera;
    std::vector<std::unique_ptr<GameObject>> objects;
    std::unique_ptr<World> world;
    bool show_debug_overlay = false; // toggled by F3, Minecraft-style
    bool show_chunk_borders = false; // toggled by F4 - World::draw_chunk_borders()
    bool show_wireframe = false;     // toggled by F5 - wireframe chunk meshes instead of textured, for inspecting mesh/culling
    float camera_move_speed;         // world units/second; mouse wheel adjusts this

    // Creative-style: unlimited access to every block, no stacks/collecting
    // - see core/Inventory. Not persisted; reset to default_inventory()
    // each session. E toggles inventory_hud's picker grid open/closed
    // (hardcoded, like F3/F4/F5 - not part of the rebindable Keybindings
    // set); number keys 1-9 set inventory.selected_slot directly.
    Inventory inventory = default_inventory();
    InventoryHud inventory_hud;

    // Counts down to 0 over the first few update() calls right after
    // set_world() places the camera at its spawn orientation (facing
    // north) - each of those calls skips applying the mouse's rotation
    // delta instead of just the very first. DisableCursor() capturing the
    // cursor takes a couple of frames to settle (measured: frame 0 reports
    // a delta from wherever the OS cursor physically was to the window's
    // center, and frame 1 reports a second, still-spurious jump before
    // GetMouseDelta() actually reads (0, 0) from frame 2 on) - one skipped
    // frame alone still let that second jump rotate the camera away from
    // the exact orientation just set.
    int spawn_settle_frames = 0;

    // Fixed-timestep accumulator (see run()): seconds of real frame time not
    // yet consumed by a tick. Carries any leftover fraction of a tick
    // forward to the next frame instead of dropping it, so the tick rate
    // averages out to exactly 20/second over time rather than drifting.
    float tick_accumulator = 0.0f;

    // Ticks elapsed since the world started - Minecraft calls the equivalent
    // the world's "age". Nothing reads this yet beyond the debug overlay;
    // it's here for future systems (day/night, scheduled updates) to key
    // off of.
    uint64_t game_tick = 0;

    // Whatever block the crosshair is currently aimed at, within block-
    // placing range - recomputed every frame in update() so draw() can
    // outline it, and independent of the click handlers' own raycasts
    // (which use break/place's own, different, reach distances).
    std::optional<World::RaycastHit> targeted_block;
};
