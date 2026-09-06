#include "core/GameEngine.hpp"
#include "core/Block.hpp"
#include "core/TextureManager.hpp"
#include "core/FontManager.hpp"
#include "core/Keybindings.hpp"
#include "core/WorldSave.hpp"
#include "Skybox.hpp"
#include "DebugOverlay.hpp"

#include "raymath.h"
#include "rlgl.h"

namespace {
    constexpr float CAMERA_MOVE_SPEED_DEFAULT = 10.0f; // world units per second
    constexpr float CAMERA_MOVE_SPEED_MIN = 2.0f;
    constexpr float CAMERA_MOVE_SPEED_MAX = 100.0f;
    constexpr float CAMERA_MOVE_SPEED_SCROLL_STEP = 2.0f; // per wheel notch
    constexpr float CAMERA_MOUSE_SENSITIVITY = 0.08f;

    constexpr float BREAK_REACH = 10.0f; // max block-breaking distance, in blocks
    constexpr float PLACE_REACH = 15.0f; // max block-placing distance, in blocks

    // World::find_spawn_position() returns ground level (a standing
    // player's feet) - this is how far above that the free-look camera's
    // own position (its "eyes") sits, Minecraft's own player eye height.
    constexpr float CAMERA_EYE_HEIGHT = 1.62f;

    // Minecraft's tick rate: game logic (once there is any beyond the
    // counter itself) runs at a fixed 20 steps per second, independent of
    // however fast frames are actually rendering.
    constexpr int TICKS_PER_SECOND = 20;
    constexpr float TICK_DURATION = 1.0f / TICKS_PER_SECOND; // seconds per tick (50ms)

    // Caps how many catch-up ticks run() will run in a single frame after a
    // stall (a dropped frame, the window being dragged, a breakpoint).
    // Without this, a long-enough stall leaves a backlog so big that
    // draining it makes every subsequent frame slow too, which creates more
    // backlog than it drains - a "spiral of death". Instead, past this many
    // ticks, the rest of the backlog is dropped (see run()): time is lost,
    // same as it would visibly be anyway, but the game recovers in one
    // frame instead of never.
    constexpr int MAX_TICKS_PER_FRAME = 5;

    // Minecraft-style block-selection outline: very slightly larger than
    // the block itself so its wireframe doesn't z-fight with the block's
    // own faces.
    constexpr float TARGET_OUTLINE_SIZE = 1.002f;
    constexpr Color TARGET_OUTLINE_COLOR = {0, 0, 0, 200};

    constexpr float CROSSHAIR_ARM_LENGTH = 10.0f; // pixels, from center to tip
    constexpr float CROSSHAIR_THICKNESS = 2.0f;   // pixels
    // Slightly off white: with the invert blend below, pure white would
    // fully negate the background; backing off a little keeps the classic
    // Minecraft "soft" look instead of a stark negative.
    constexpr unsigned char CROSSHAIR_INTENSITY = 235;

    // Minecraft's crosshair trick: instead of drawing an opaque or
    // alpha-blended "+", render it with the framebuffer's own color fed
    // back into the blend so each pixel becomes (roughly) its own inverse -
    // result = src*(1-dst) + dst*(1-src). That's what makes it read as
    // legible (and faintly "see-through") over both light and dark terrain,
    // rather than a flat-colored icon that disappears against a similar
    // background.
    void draw_crosshair(int screen_width, int screen_height)
    {
        Color color = {CROSSHAIR_INTENSITY, CROSSHAIR_INTENSITY, CROSSHAIR_INTENSITY, 255};
        float center_x = screen_width / 2.0f;
        float center_y = screen_height / 2.0f;

        rlSetBlendFactors(RL_ONE_MINUS_DST_COLOR, RL_ONE_MINUS_SRC_COLOR, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM);

        DrawRectangle(static_cast<int>(center_x - CROSSHAIR_ARM_LENGTH),
                      static_cast<int>(center_y - CROSSHAIR_THICKNESS / 2.0f),
                      static_cast<int>(CROSSHAIR_ARM_LENGTH * 2.0f),
                      static_cast<int>(CROSSHAIR_THICKNESS), color);
        DrawRectangle(static_cast<int>(center_x - CROSSHAIR_THICKNESS / 2.0f),
                      static_cast<int>(center_y - CROSSHAIR_ARM_LENGTH),
                      static_cast<int>(CROSSHAIR_THICKNESS),
                      static_cast<int>(CROSSHAIR_ARM_LENGTH * 2.0f), color);

        EndBlendMode();
    }

    // Outlines the block a raycast hit, in world space - the block's own
    // vertices are chunk-mesh-local (0..CHUNK_SIZE within that chunk), but
    // World::raycast already reports hit coordinates in world space, and a
    // wireframe cube doesn't care which chunk (if any) it's logically
    // "in".
    void draw_target_outline(const World::RaycastHit& hit)
    {
        Vector3 center = {hit.x + 0.5f, hit.y + 0.5f, hit.z + 0.5f};
        DrawCubeWires(center, TARGET_OUTLINE_SIZE, TARGET_OUTLINE_SIZE, TARGET_OUTLINE_SIZE, TARGET_OUTLINE_COLOR);
    }
}

GameEngine::GameEngine(int screen_width, int screen_height, const char* title)
    : camera_move_speed(CAMERA_MOVE_SPEED_DEFAULT)
{
    InitWindow(screen_width, screen_height, title);

    // Esc defaults to closing the window (WindowShouldClose()'s other
    // trigger) - disabled so SettingsScreen can use it to cancel a
    // keybind-rebind-in-progress instead of quitting the whole game out
    // from under it. The only quit paths left are "Закрыть игру"
    // (quit_requested) and the OS window-close control.
    SetExitKey(KEY_NULL);

    settings = SettingsIO::load();
    SetTargetFPS(settings.target_fps);

    Load_block_definitions(); // needs a GL context, so only after InitWindow
    SetTextureFilter(get_block_atlas_texture(),
                      settings.texture_filter == TextureFilterMode::Bilinear ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    FontManager::get(); // load the game's text font up front, same reason
    load_chunk_shader(); // same reason

    // position/target are placeholders until set_world() actually has a
    // World to find real ground in - everything else here doesn't depend
    // on one.
    camera.position = {0.0f, 100.0f, 0.0f};
    camera.target = {0.0f, 100.0f, -1.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // No DisableCursor() here - the game starts on MainMenu, which needs a
    // visible, clickable cursor. enter_state() disables it only when
    // actually transitioning into Playing.
}

GameEngine::~GameEngine()
{
    // Covers quitting the app outright while a World is loaded (the OS
    // window-close control, or force-quit) - return_to_main_menu() covers
    // the other exit path (the pause menu), but this one has no earlier
    // hook to call it from.
    save_player_state();

    EnableCursor();
    TextureManager::unload_all();
    FontManager::unload();
    unload_chunk_fog_shader();
    CloseWindow();
}

void GameEngine::add_object(std::unique_ptr<GameObject> object)
{
    objects.push_back(std::move(object));
}

void GameEngine::set_world(std::unique_ptr<World> new_world)
{
    world = std::move(new_world);
    if (world) {
        // On dry land, never Sea/Ocean, with clear air to actually appear
        // in (find_spawn_position() itself generates whatever chunk it
        // needs to confirm this, so the world's already populated around
        // the result - no separate update_chunk_states() call needed here
        // the way there used to be for the old fixed starting position).
        camera.position = world->find_spawn_position();
        camera.position.y += CAMERA_EYE_HEIGHT;
        // North: -Z in this engine's convention (see Chunk.cpp's
        // CUBE_FACES comment). Level, not angled down - the old downward
        // tilt was there to see a bird's-eye view from high above the
        // world; standing on real ground, a level look is the natural one.
        camera.target = {camera.position.x, camera.position.y, camera.position.z - 10.0f};
        spawn_settle_frames = 3; // see its own comment - 2 measured, +1 margin
    }
}

void GameEngine::tick()
{
    ++game_tick;
    // Chunk loading/unloading is the first real occupant of this clock -
    // see World::update_chunk_states. Everything else (day/night,
    // scheduled block updates, random ticks) hooks in the same way, from
    // here.
    if (world) {
        world->update_chunk_states(camera.position);
        world->update_fluids();
        world->update_falling_blocks();
    }
}

void GameEngine::update(float delta_time)
{
    // Mouse wheel adjusts fly speed (Minecraft creative/spectator-style):
    // one notch = one CAMERA_MOVE_SPEED_SCROLL_STEP, clamped so it can
    // never scroll down to a standstill or up to an uncontrollable blur.
    float wheel_move = GetMouseWheelMove();
    if (wheel_move != 0.0f) {
        camera_move_speed = Clamp(camera_move_speed + wheel_move * CAMERA_MOVE_SPEED_SCROLL_STEP,
                                   CAMERA_MOVE_SPEED_MIN, CAMERA_MOVE_SPEED_MAX);
    }

    // Inventory: E toggles the picker grid open/closed (hardcoded, like
    // F3/F4/F5 below - not one of Settings' rebindable actions), freeing/
    // recapturing the cursor to match. Number keys pick a hotbar slot
    // directly, only while the grid isn't stealing input.
    if (world && IsKeyPressed(KEY_E)) {
        inventory_hud.toggle();
        if (inventory_hud.is_open()) EnableCursor(); else DisableCursor();
    }
    if (inventory_hud.is_open() && IsKeyPressed(KEY_ESCAPE)) {
        inventory_hud.close();
        DisableCursor();
    } else if (world && IsKeyPressed(KEY_ESCAPE)) {
        enter_state(GameState::Paused);
        return;
    }
    if (!inventory_hud.is_open()) {
        for (int slot = 0; slot < HOTBAR_SIZE; ++slot) {
            if (IsKeyPressed(KEY_ONE + slot)) inventory.selected_slot = slot;
        }
    }

    if (IsKeyPressed(KEY_F3)) {
        show_debug_overlay = !show_debug_overlay;
    }
    if (IsKeyPressed(KEY_F4)) {
        show_chunk_borders = !show_chunk_borders;
    }
    if (IsKeyPressed(KEY_F5)) {
        show_wireframe = !show_wireframe;
    }

    // While the inventory grid is open, it owns input instead of the
    // camera/world below (drawn and handled together in draw(), the same
    // immediate-mode pattern every menu screen already uses) - same idea
    // as spawn_settle_frames suppressing rotation, just gated on is_open()
    // rather than a frame counter. Debug toggles above still work either
    // way, same as F3 staying live over Minecraft's own inventory screen.
    if (inventory_hud.is_open()) {
        targeted_block = std::nullopt;
        for (auto& object : objects) {
            if (object->is_active()) object->update(delta_time, world.get());
        }
        return;
    }

    // Free-look test camera: rebindable keys (Settings) to move, mouse to
    // look. Today's defaults are still W/A/S/D + Space/Shift - see
    // default_keybindings() - just no longer hardcoded here.
    auto is_action_down = [this](GameAction action) {
        return binding_down(settings.keybindings[static_cast<size_t>(action)]);
    };
    Vector3 movement = {0.0f, 0.0f, 0.0f};
    if (is_action_down(GameAction::MoveForward)) movement.x += camera_move_speed * delta_time;
    if (is_action_down(GameAction::MoveBackward)) movement.x -= camera_move_speed * delta_time;
    if (is_action_down(GameAction::MoveRight)) movement.y += camera_move_speed * delta_time;
    if (is_action_down(GameAction::MoveLeft)) movement.y -= camera_move_speed * delta_time;

    if (is_action_down(GameAction::FlyUp))   movement.z += camera_move_speed * delta_time;
    if (is_action_down(GameAction::FlyDown)) movement.z -= camera_move_speed * delta_time;

    Vector2 mouse_delta = GetMouseDelta();
    Vector3 rotation = {mouse_delta.x * CAMERA_MOUSE_SENSITIVITY, mouse_delta.y * CAMERA_MOUSE_SENSITIVITY, 0.0f};
    if (spawn_settle_frames > 0) {
        // See spawn_settle_frames's own comment: this delta might still be
        // a spurious startup jump, not real player input.
        rotation = {0.0f, 0.0f, 0.0f};
        --spawn_settle_frames;
    }

    UpdateCameraPro(&camera, movement, rotation, 0.0f);

    // camera.target isn't a unit vector (it's an arbitrary point ahead of
    // the camera), so the aim direction needs normalizing before it's used
    // as a ray direction.
    Vector3 aim = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    // Recomputed every frame (not just on click) so draw() can outline
    // whatever's targeted, out to the longer of the two reaches (place's)
    // so the outline still shows a block that's placeable but too far to
    // break.
    targeted_block = world ? world->raycast(camera.position, aim, PLACE_REACH) : std::nullopt;

    // Left click breaks whatever solid block the crosshair is aimed at,
    // within BREAK_REACH blocks. Right click places one block against the
    // face the crosshair is aimed at (the cell just outside the targeted
    // block, in the direction of the hit face's own outward normal),
    // within the longer PLACE_REACH.
    if (world && binding_pressed(settings.keybindings[static_cast<size_t>(GameAction::BreakBlock)])) {
        if (auto hit = world->raycast(camera.position, aim, BREAK_REACH)) {
            world->break_block(hit->x, hit->y, hit->z);
        }
    } else if (world && binding_pressed(settings.keybindings[static_cast<size_t>(GameAction::PlaceBlock)])) {
        if (targeted_block) {
            int place_x = targeted_block->x + static_cast<int>(targeted_block->normal.x);
            int place_y = targeted_block->y + static_cast<int>(targeted_block->normal.y);
            int place_z = targeted_block->z + static_cast<int>(targeted_block->normal.z);
            world->place_block(place_x, place_y, place_z, inventory.hotbar[inventory.selected_slot]);
        }
    }

    for (auto& object : objects) {
        if (object->is_active()) {
            object->update(delta_time, world.get());
        }
    }
}

void GameEngine::draw()
{
    BeginDrawing();
    ClearBackground(RAYWHITE);

    BeginMode3D(camera);
    draw_skybox(camera.position);
    if (world) {
        // Wireframe ("skeleton") debug view: draws the exact same chunk
        // meshes, just as GL_LINE edges instead of filled/textured
        // triangles - every block's own face boundaries end up visible,
        // which is what actually reveals block positions/mesh structure,
        // rather than a separate position-label overlay.
        if (show_wireframe) rlEnableWireMode();
        world->draw(camera);
        if (show_wireframe) rlDisableWireMode();

        if (show_chunk_borders) {
            world->draw_chunk_borders();
        }
    }
    for (auto& object : objects) {
        if (object->is_active()) {
            object->draw();
        }
    }
    if (targeted_block) {
        draw_target_outline(*targeted_block);
    }
    EndMode3D();

    draw_crosshair(GetScreenWidth(), GetScreenHeight());

    if (world) {
        inventory_hud.draw_hotbar(inventory);
        // Drawn and click-handled together here (not from update()) - the
        // same immediate-mode pattern every menu screen already uses, and
        // simplest since update() already returned early while it's open.
        if (inventory_hud.is_open()) inventory_hud.update_grid(inventory);
    }

    if (show_debug_overlay && world) {
        draw_debug_overlay(camera, *world, BREAK_REACH, camera_move_speed, game_tick);
    }

    EndDrawing();
}

void GameEngine::run()
{
    while (!WindowShouldClose() && !quit_requested) {
        if (state != GameState::Playing) {
            update_and_draw_menu();
            continue;
        }

        float delta_time = GetFrameTime();

        // Fixed-timestep tick loop: run as many 50ms ticks as delta_time
        // has accumulated (usually 0 or 1 at 60+ FPS, more only after a
        // stall), each one always the same fixed size - game logic that
        // reads game_tick sees a steady 20/second clock no matter the
        // frame rate. See MAX_TICKS_PER_FRAME for the catch-up cap.
        tick_accumulator += delta_time;
        int ticks_this_frame = 0;
        while (tick_accumulator >= TICK_DURATION && ticks_this_frame < MAX_TICKS_PER_FRAME) {
            tick();
            tick_accumulator -= TICK_DURATION;
            ++ticks_this_frame;
        }
        if (ticks_this_frame == MAX_TICKS_PER_FRAME) {
            tick_accumulator = 0.0f; // drop the rest of the backlog instead of chasing it forever
        }

        update(delta_time);
        draw();
    }
}

void GameEngine::update_and_draw_menu()
{
    BeginDrawing();
    ClearBackground(Color{24, 24, 28, 255}); // flat background - no world/skybox exists yet

    switch (state) {
        case GameState::MainMenu: {
            switch (main_menu_screen.update()) {
                case MainMenuScreen::Action::Singleplayer:
                    enter_state(GameState::WorldList);
                    break;
                case MainMenuScreen::Action::Settings:
                    settings_return_state = GameState::MainMenu;
                    enter_state(GameState::Settings);
                    break;
                case MainMenuScreen::Action::Quit:
                    quit_requested = true;
                    break;
                default: break; // None, or Multiplayer (the button is disabled - never actually returned)
            }
            break;
        }
        case GameState::WorldList: {
            WorldListScreen::Action action = world_list_screen.update();
            if (action.type == WorldListScreen::ActionType::LoadWorld) {
                start_singleplayer_world(action.folder_name);
            } else if (action.type == WorldListScreen::ActionType::CreateWorld) {
                enter_state(GameState::WorldCreate);
            } else if (action.type == WorldListScreen::ActionType::Back) {
                enter_state(GameState::MainMenu);
            }
            break;
        }
        case GameState::WorldCreate: {
            WorldCreateScreen::Action action = world_create_screen.update();
            if (action.type == WorldCreateScreen::ActionType::Create) {
                WorldSave::create_world(action.world);
                start_singleplayer_world(action.world.folder_name);
            } else if (action.type == WorldCreateScreen::ActionType::Cancel) {
                enter_state(GameState::WorldList);
            }
            break;
        }
        case GameState::Settings: {
            if (settings_screen.update(settings).type == SettingsScreen::ActionType::Back) {
                enter_state(settings_return_state); // MainMenu, or Paused if opened via the in-game Esc menu
            }
            break;
        }
        case GameState::Paused: {
            switch (pause_menu_screen.update()) {
                case PauseMenuScreen::Action::Resume:
                    enter_state(GameState::Playing);
                    break;
                case PauseMenuScreen::Action::Settings:
                    settings_return_state = GameState::Paused;
                    enter_state(GameState::Settings);
                    break;
                case PauseMenuScreen::Action::MainMenu:
                    return_to_main_menu();
                    break;
                default: break;
            }
            break;
        }
        case GameState::Playing:
            break; // unreachable - run() only calls this method when state != Playing
    }

    EndDrawing();
}

void GameEngine::enter_state(GameState new_state)
{
    state = new_state;
    if (state == GameState::Playing) {
        DisableCursor(); // mouse-look needs the cursor captured
    } else {
        EnableCursor(); // every menu screen needs a visible, clickable cursor
        if (state == GameState::WorldList) world_list_screen.enter();
        if (state == GameState::WorldCreate) world_create_screen.enter();
    }
}

void GameEngine::start_singleplayer_world(const std::string& folder_name)
{
    std::optional<WorldInfo> info = WorldSave::load_world_info(folder_name);
    if (!info) return; // shouldn't happen - fail safe, stay on the current screen instead of crashing

    WorldConfig config;
    config.seed = info->seed;
    config.save_directory = WorldSave::world_directory(folder_name);
    config.loaded_radius_chunks = settings.render_distance_chunks;
    config.fog_distance_blocks = settings.fog_distance_blocks;
    // active_radius_chunks keeps WorldConfig's own default - simulation
    // distance isn't a Settings field (see Settings.hpp), only render
    // distance and fog distance are user-configurable.

    current_world_folder = folder_name;
    set_world(std::make_unique<World>(config));

    // Resume exactly where the player left off last time, if they ever
    // have before (see save_player_state()) - overrides set_world()'s own
    // default spawn-search placement and default_inventory(), otherwise
    // left untouched (a brand new world has no player.json yet).
    if (std::optional<PlayerSaveState> saved = WorldSave::load_player_state(folder_name)) {
        camera.position = saved->position;
        camera.target = Vector3Add(camera.position, Vector3Scale(saved->forward, 10.0f));
        inventory = saved->inventory;
    }

    enter_state(GameState::Playing);
}

void GameEngine::save_player_state()
{
    if (!world) return;

    PlayerSaveState state;
    state.position = camera.position;
    state.forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    state.inventory = inventory;
    WorldSave::save_player_state(current_world_folder, state);
}

void GameEngine::return_to_main_menu()
{
    save_player_state();
    world.reset(); // ~World() flushes any modified chunks still resident - same guarantee quitting the app outright already relies on
    current_world_folder.clear();
    inventory_hud.close();
    enter_state(GameState::MainMenu);
}
