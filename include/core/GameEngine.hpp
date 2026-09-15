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
#include "player/PlayerController.hpp"
#include "player/PlayerHealth.hpp"
#include "effects/ParticleSystem.hpp"
#include "rendering/PlayerRenderer.hpp"
#include "audio/AudioSystem.hpp"
#include "ui/MainMenuScreen.hpp"
#include "ui/WorldListScreen.hpp"
#include "ui/WorldCreateScreen.hpp"
#include "ui/SettingsScreen.hpp"
#include "ui/PauseMenuScreen.hpp"
#include "ui/InventoryHud.hpp"
#include "world/World.hpp"

// A leaf block found disconnected from every nearby log (see
// GameEngine::check_leaf_decay_near) - not removed on the spot, just
// queued with a random delay so a felled tree's canopy visibly thins out
// over a few seconds instead of vanishing all at once in a single frame.
// Counted down in ticks (see update_leaf_decay()), not real time, same as
// every other world-simulation timer here - so it (like the chunk/fluid/
// falling-block/dropped-item simulation tick() already drives) keeps
// advancing at Minecraft's own steady 20/second regardless of the render
// frame rate, and keeps running even while the inventory screen owns
// input (see update()'s own early-return once the grid is open - it
// happens after tick()'s own dispatch in run(), not inside it, so nothing
// here is gated on the grid being closed the way player movement/camera
// control is).
struct PendingLeafDecay {
    int x, y, z;
    int remaining_ticks;
};

// A planted Sapling block (see GameEngine::queue_sapling_growth) waiting
// to become a tree - one entry per placement, counted down the same way
// PendingLeafDecay is.
struct PendingSaplingGrowth {
    int x, y, z;
    int remaining_ticks;
};

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
    // tick rate. Every piece of world simulation that isn't purely visual
    // hangs off this clock: chunk streaming, fluids, falling blocks,
    // dropped-item physics, leaf decay, and sapling growth. Called
    // unconditionally from run() whenever state == GameState::Playing,
    // independent of update()'s own early-return while the inventory
    // screen is open - so the world keeps existing (chunks load, water
    // flows, a planted sapling keeps counting down) even while the player
    // is just browsing their inventory, not merely while they're actively
    // playing. Day/night and scheduled block updates/random ticks are
    // still future work, but would hook in here the same way.
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

    // Called right after a Log block is removed (natural or creative
    // break) - scans for leaves now out of reach of every remaining log and
    // queues each one into pending_leaf_decay (see update_leaf_decay())
    // rather than removing it immediately. Purely event-triggered off a log
    // disappearing, not a per-tick scan, so it costs nothing on every other
    // frame.
    void check_leaf_decay_near(int log_x, int log_y, int log_z);

    // Every-tick (see tick()) drain of pending_leaf_decay: counts each
    // entry's own delay down by one tick, and once it elapses, re-checks
    // it's still a disconnected leaf (state may have changed since it was
    // queued - a log placed back nearby, or it already came down another
    // way) before actually removing it and rolling its drop, same as a
    // natural break.
    void update_leaf_decay();

    // Called right after a Sapling is successfully placed (World::
    // place_block already restricts that to Grass/Dirt) - queues it into
    // pending_sapling_growth with a randomized delay (see
    // update_sapling_growth()) rather than growing it on the spot.
    void queue_sapling_growth(int x, int y, int z);

    // Every-tick (see tick()) drain of pending_sapling_growth: counts each
    // entry's own delay down by one tick, and once it elapses, re-checks
    // the block there is still a Sapling (it may have been broken, or
    // something else placed over it, since queued) before growing an oak
    // tree from it - make_oak_tree()'s own template (see
    // StructureGenerator, which places the exact same shape at world-
    // generation time), placed here through World::place_structure_block()
    // instead of Chunk::set_block() since this runs at an arbitrary world
    // position at runtime, not bounded to one already-open Chunk. If the
    // trunk's column isn't clear (something built overhead since it was
    // planted), the attempt is deferred and retried shortly instead of
    // discarded, same as vanilla re-rolling a blocked sapling on its next
    // random tick.
    void update_sapling_growth();

    // Called right after any block is removed - ShortGrass (and anything
    // else non-solid that needs ground under it) can't stay floating in
    // place the way real Minecraft's own tufts/flowers can't either: if
    // ShortGrass is sitting directly above the now-empty cell, it pops
    // immediately (not a delayed decay like leaves - support loss is
    // instant in vanilla too), dropping through the same
    // resolve_block_drops() table a manual break would (bare-handed).
    void check_grass_support_above(int x, int y, int z);

    // Survival-only environmental damage - fall, drowning, lava/fire,
    // cactus, suffocation and the void safety net - checked every frame
    // against PlayerController's own freshly-computed contact flags (see
    // its is_in_lava()/is_touching_cactus()/is_head_submerged()/
    // is_suffocating()) plus the void-Y check against camera.position
    // itself. Also advances player_health's own invulnerability clock
    // and, once dead, counts down to respawn_player(). No-op outside
    // Survival - Creative is invulnerable, same as real Minecraft, and
    // player_health simply never leaves full health there.
    void update_player_damage(float delta_time);

    // Applies `amount` half-hearts from `source` via player_health.damage()
    // and, if it actually landed (not blocked by invulnerability), kicks
    // off the red hurt-flash overlay (see hurt_flash_seconds/draw()).
    void apply_damage(int amount, DamageSource source);

    // Teleports the camera back to World::find_spawn_position() (the same
    // fixed point a brand-new session on this world starts at - this
    // project has no bed/respawn-anchor system) and resets every piece of
    // per-life state (health, fire, air, controller velocity) - called
    // automatically by update_player_damage() a couple seconds after
    // death, real Minecraft's own death-screen delay just without the
    // screen or its click-to-respawn button. Inventory is left untouched -
    // there's no drop-on-death here (yet).
    void respawn_player();

    // Zeroes every per-life timer (burning, breath, suffocation, the hurt
    // flash, the death/respawn countdown) - shared by set_world(),
    // start_singleplayer_world()'s saved-state path and respawn_player(),
    // everywhere a life is starting fresh. Doesn't touch player_health
    // itself - callers decide separately whether that means reset() (full
    // health) or restoring a specific saved value (set_health()).
    void reset_life_timers();

    // Called right after a Chest block is removed - spills whatever
    // World::chest_inventory() had stored there as ordinary dropped items
    // (same as a real Minecraft chest) instead of silently deleting its
    // contents along with the block. No-op if the chest was empty/never
    // opened (take_chest_inventory() just returns all-empty then).
    void spill_chest_if_any(int x, int y, int z);

    Camera3D make_render_camera() const;
    void draw_player_model() const;

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

    // Frozen, downsampled and softly blurred copy of the last gameplay
    // frame. The pause menu draws this instead of the dirt background.
    Texture2D pause_snapshot{};
    bool pause_snapshot_pending = false;

    // Where SettingsScreen's own "Назад" button returns to - MainMenu when
    // Settings was reached from there, Paused when reached via the in-game
    // Esc menu instead. Set right before every enter_state(Settings) call.
    GameState settings_return_state = GameState::MainMenu;

    bool quit_requested = false; // set by "Закрыть игру" - checked alongside WindowShouldClose() in run()

    // Which saves/<folder>/ the current `world` was loaded from - empty
    // when no world is loaded. Set by start_singleplayer_world(), read by
    // save_player_state().
    std::string current_world_folder;
    GameMode current_game_mode = GameMode::Creative;

    // The physical eye camera. Third-person cameras are derived only for
    // rendering, so they never move the PlayerController hitbox.
    Camera3D camera;
    PlayerController player_controller;
    PlayerHealth player_health; // Survival only - see update_player_damage()
    PlayerRenderer player_renderer;

    // Drowning: seconds of air left, drained while PlayerController reports
    // the eye position submerged and restored otherwise - once it hits 0
    // and the head is still under, drown_damage_timer paces the resulting
    // damage at one hit/second the same way real Minecraft's own breath
    // meter does.
    float air_seconds        = 15.0f;
    float drown_damage_timer = 0.0f;

    // Burning: real Minecraft sets an entity that touches lava on fire for
    // a fixed duration (independent of how brief the contact was) rather
    // than only hurting it while actually inside the lava - lava contact
    // itself is checked directly against PlayerController::is_in_lava() and
    // doesn't need its own timer since its 0.5s damage interval already
    // matches player_health's own post-hit invulnerability window.
    float fire_seconds_remaining = 0.0f;
    float fire_damage_timer      = 0.0f;

    float suffocation_damage_timer = 0.0f;

    // Death/respawn: set the instant player_health.is_dead() first becomes
    // true (see was_dead_last_frame), counts down in update_player_damage()
    // to the automatic respawn_player() call.
    float death_respawn_timer = 0.0f;
    bool was_dead_last_frame = false;

    // Brief red screen flash whenever apply_damage() actually lands a hit -
    // draw()'s only feedback for taking damage (there's no hurt sound/hit
    // animation asset in this project yet). Counts down to 0 in draw().
    float hurt_flash_seconds = 0.0f;


    enum class CameraView : uint8_t { FirstPerson, ThirdPersonBack, ThirdPersonFront };
    CameraView camera_view = CameraView::FirstPerson;
    std::vector<std::unique_ptr<GameObject>> objects;
    std::unique_ptr<World> world;
    bool show_debug_overlay = false; // toggled by F3, Minecraft-style
    bool show_chunk_borders = false; // toggled by F4 - World::draw_chunk_borders()
    bool show_wireframe = false;     // toggled by F6; F5 cycles camera views
    float camera_move_speed;         // world units/second; mouse wheel adjusts this

    // Survival-style stacks collected from broken-block drops. E toggles
    // the storage panel; number keys 1-9 select the active hotbar slot.
    // WorldSave persists both the hotbar and storage grids.
    Inventory inventory = default_inventory();
    InventoryHud inventory_hud;
    std::vector<std::unique_ptr<DroppedItem>> dropped_items;
    std::vector<PendingLeafDecay> pending_leaf_decay;
    std::vector<PendingSaplingGrowth> pending_sapling_growth;
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
