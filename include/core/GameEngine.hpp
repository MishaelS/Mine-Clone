#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "raylib.h"
#include "core/GameObject.hpp"
#include "core/GameState.hpp"
#include "core/Settings.hpp"
#include "player/Inventory.hpp"
#include "player/DroppedItem.hpp"
#include "effects/ParticleSystem.hpp"
#include "audio/AudioSystem.hpp"
#include "ui/MainMenuScreen.hpp"
#include "ui/WorldListScreen.hpp"
#include "ui/WorldCreateScreen.hpp"
#include "ui/SettingsScreen.hpp"
#include "ui/PauseMenuScreen.hpp"
#include "ui/InventoryHud.hpp"
#include "world/World.hpp"

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

    // One tick's worth of physics (gravity/drag/water buoyancy/ground
    // collision - DroppedItem::tick_physics()) for every dropped item,
    // then one pass merging any that ended up close enough together (see
    // DroppedItem::try_merge()) - called from tick(), not every frame, so
    // falling items move at Minecraft's own fixed rate.
    void tick_dropped_items();

    // Every-frame (not tick-locked) part of dropped-item handling: pulls
    // anything within magnet range toward the player (DroppedItem::
    // update_magnet_pull(), continuous so the pull tracks smooth camera
    // motion instead of visibly stepping at 20Hz) and actually collects
    // whatever's now close enough. Called from update().
    void update_dropped_items(float delta_time);

    // Q - throws `stack` (already split off the slot it came from, by
    // take_one_item()) out in front of the player, same forward-and-up
    // toss real Minecraft gives a manually dropped item. Shared by both Q
    // paths: the selected hotbar slot while playing normally, and
    // whatever InventoryHud::update_grid() reports was Q'd while the
    // inventory screen is open. No-op if there's no world or `stack` is
    // empty (take_one_item() on an already-empty slot returns one).
    void spawn_dropped_item(const ItemStack& stack);

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
    AudioSystem audio;
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

    // Free-look camera (WASD + mouse), currently also representing the
    // player viewpoint until a dedicated controller is introduced.
    Camera3D camera;
    std::vector<std::unique_ptr<GameObject>> objects;
    std::unique_ptr<World> world;
    bool show_debug_overlay = false; // toggled by F3, Minecraft-style
    bool show_chunk_borders = false; // toggled by F4 - World::draw_chunk_borders()
    bool show_wireframe = false;     // toggled by F5 - wireframe chunk meshes instead of textured, for inspecting mesh/culling
    float camera_move_speed;         // world units/second; mouse wheel adjusts this

    // Vertical-only physics velocity (blocks/second) - gravity/jump, see
    // update()'s own comment. Horizontal movement has no equivalent
    // per-frame state of its own; WASD sets that directly every frame.
    float player_vertical_velocity = 0.0f;

    // Survival-style stacks collected from broken-block drops. E toggles
    // the storage panel; number keys 1-9 select the active hotbar slot.
    // WorldSave persists both the hotbar and storage grids.
    Inventory inventory = default_inventory();
    InventoryHud inventory_hud;
    std::vector<std::unique_ptr<DroppedItem>> dropped_items;
    ParticleSystem particles;
    float footstep_particle_distance = 0.0f;

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

    // Hold-to-break progress (see break_seconds_required() in the .cpp):
    // accumulates while BreakBlock is held down and the aimed-at block
    // hasn't changed since the hold started, resets to 0 whenever it does
    // (or the button is released) - draw() reads it to show a small
    // progress indicator. is_breaking is separate from
    // breaking_progress > 0 so "just started this frame, 0 progress so
    // far" still counts as actively breaking rather than reading as idle.
    bool is_breaking = false;
    int breaking_x = 0, breaking_y = 0, breaking_z = 0;
    float breaking_progress = 0.0f; // 0..1
};
