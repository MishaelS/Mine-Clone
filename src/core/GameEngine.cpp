#include "core/GameEngine.hpp"
#include "core/Block.hpp"
#include "core/TextureManager.hpp"
#include "core/Tick.hpp"
#include "player/Item.hpp"
#include "ui/FontManager.hpp"
#include "ui/Widgets.hpp"
#include "core/Keybindings.hpp"
#include "core/WorldSave.hpp"
#include "rendering/Skybox.hpp"
#include "ui/DebugOverlay.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr float CAMERA_MOVE_SPEED_DEFAULT = 10.0f; // world units per second
    constexpr float CAMERA_MOVE_SPEED_MIN = 2.0f;
    constexpr float CAMERA_MOVE_SPEED_MAX = 100.0f;
    constexpr float CAMERA_MOVE_SPEED_SCROLL_STEP = 2.0f; // per wheel notch
    constexpr float CAMERA_MOUSE_SENSITIVITY = 0.08f;

    constexpr float BREAK_REACH = 10.0f; // max block-breaking distance, in blocks
    constexpr float PLACE_REACH = 15.0f; // max block-placing distance, in blocks

    // The hold-to-break progress bar, drawn just under the crosshair - see
    // GameEngine::draw() and the is_breaking/breaking_progress fields.
    constexpr float BREAK_BAR_WIDTH = 60.0f;
    constexpr float BREAK_BAR_HEIGHT = 6.0f;
    constexpr float BREAK_BAR_OFFSET_Y = 28.0f; // below screen center
    constexpr Color BREAK_BAR_BACKGROUND = {0, 0, 0, 150};
    constexpr Color BREAK_BAR_FILL = {255, 255, 255, 220};

    // World::find_spawn_position() returns ground level (a standing
    // player's feet) - this is how far above that the free-look camera's
    // own position (its "eyes") sits, Minecraft's own player eye height.
    constexpr float CAMERA_EYE_HEIGHT = 1.62f;

    // Real Minecraft's own standing hitbox (https://minecraft.wiki/w/Hitbox):
    // 0.6 blocks wide (square in X/Z), 1.8 tall, centered on X/Z at the
    // player's own position - CAMERA_EYE_HEIGHT above is how far above
    // this box's own bottom (the feet) the camera sits.
    constexpr float PLAYER_HALF_WIDTH = 0.3f;
    constexpr float PLAYER_HEIGHT = 1.8f;
    // Keeps a box resting exactly on a cell boundary from re-triggering a
    // false "still blocked" on the very next frame's check.
    constexpr float COLLISION_EPSILON = 0.001f;
    // How far below the feet the "am I standing on something" probe
    // checks - see the grounded check in update().
    constexpr float GROUND_CHECK_EPSILON = 0.05f;

    // Real Minecraft's own per-tick living-entity numbers (see
    // http://minecraft.wiki/w/Entity): 0.08 blocks/tick^2 gravity, 0.98
    // vertical drag, 0.42 blocks/tick jump velocity. Converted to
    // continuous units (delta_time-scaled, like every other movement
    // here) instead of also giving the player its own tick-interpolated
    // motion track just for this: acceleration's units are 1/time^2, so
    // blocks/tick^2 -> blocks/s^2 multiplies by TICKS_PER_SECOND^2;
    // velocity's are 1/time, so blocks/tick -> blocks/s multiplies by
    // TICKS_PER_SECOND once. PLAYER_VERTICAL_DRAG itself stays the raw
    // per-tick figure - applied continuously via std::pow(drag, delta_time
    // * TICKS_PER_SECOND), the same technique ParticleSystem's own
    // std::pow(0.35f, delta_time) drag already uses for a per-second decay
    // rate; here it's a per-*tick* one, hence the extra *TICKS_PER_SECOND.
    constexpr float PLAYER_GRAVITY = 0.08f * TICKS_PER_SECOND * TICKS_PER_SECOND;
    constexpr float PLAYER_JUMP_VELOCITY = 0.42f * TICKS_PER_SECOND;
    constexpr float PLAYER_VERTICAL_DRAG = 0.98f;

    bool player_box_blocked(const World& world, Vector3 feet)
    {
        int min_x = static_cast<int>(std::floor(feet.x - PLAYER_HALF_WIDTH + COLLISION_EPSILON));
        int max_x = static_cast<int>(std::floor(feet.x + PLAYER_HALF_WIDTH - COLLISION_EPSILON));
        int min_y = static_cast<int>(std::floor(feet.y + COLLISION_EPSILON));
        int max_y = static_cast<int>(std::floor(feet.y + PLAYER_HEIGHT - COLLISION_EPSILON));
        int min_z = static_cast<int>(std::floor(feet.z - PLAYER_HALF_WIDTH + COLLISION_EPSILON));
        int max_z = static_cast<int>(std::floor(feet.z + PLAYER_HALF_WIDTH - COLLISION_EPSILON));

        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int z = min_z; z <= max_z; ++z) {
                    if (get_block_properties(world.get_block(x, y, z)).solid) return true;
                }
            }
        }
        return false;
    }

    // Resolves this frame's camera movement against the player's own
    // collision box, one axis at a time (X, then Z, then Y) - each either
    // fully accepted or fully rejected depending on whether it would
    // overlap something solid. Rejecting per-axis independently instead of
    // the whole 3D step at once gives a basic "slide along the wall" for
    // free: moving diagonally into a wall still keeps whichever component
    // wasn't actually blocked, instead of stopping dead. Works in eye-space
    // in, feet-space internally (CAMERA_EYE_HEIGHT converts between them) -
    // this is a creative/spectator-style *fly* camera (see camera_move_
    // speed's own comment), so this only ever stops the player from
    // clipping through solid geometry, the same collision real Minecraft's
    // own Creative flight still has - it doesn't add gravity or a ground
    // state; nothing here stops the player from flying wherever there's
    // open space.
    Vector3 resolve_player_collision(const World& world, Vector3 previous_eye, Vector3 desired_eye)
    {
        Vector3 feet = {previous_eye.x, previous_eye.y - CAMERA_EYE_HEIGHT, previous_eye.z};
        Vector3 desired_feet = {desired_eye.x, desired_eye.y - CAMERA_EYE_HEIGHT, desired_eye.z};

        Vector3 test = feet;
        test.x = desired_feet.x;
        if (!player_box_blocked(world, test)) feet.x = test.x;

        test = feet;
        test.z = desired_feet.z;
        if (!player_box_blocked(world, test)) feet.z = test.z;

        test = feet;
        test.y = desired_feet.y;
        if (!player_box_blocked(world, test)) feet.y = test.y;

        return {feet.x, feet.y + CAMERA_EYE_HEIGHT, feet.z};
    }

    // Q-drop (spawn_dropped_item()): thrown out from just in front of the
    // player - not right at their own position, or it would immediately
    // re-trigger their own pickup radius - forward and slightly up,
    // blocks/tick to match DroppedItem's own velocity unit, the same
    // forward-and-up toss real Minecraft gives a manually dropped item.
    constexpr float DROP_SPAWN_DISTANCE = 0.6f;
    constexpr float DROP_LAUNCH_SPEED = 0.15f;
    constexpr float DROP_LAUNCH_UP = 0.05f;

    // Dropped-item pickup: a small instant-collect radius. Anything a bit
    // further out but still within DroppedItem's own magnet range is
    // pulled toward the player first (DroppedItem::update_magnet_pull(),
    // called unconditionally below - it no-ops outside its own radius, so
    // GameEngine doesn't need to know that distance too).
    constexpr float ITEM_PICKUP_RADIUS = 0.4f;

    // Caps how many catch-up ticks run() will run in a single frame after a
    // stall (a dropped frame, the window being dragged, a breakpoint).
    // Without this, a long-enough stall leaves a backlog so big that
    // draining it makes every subsequent frame slow too, which creates more
    // backlog than it drains - a "spiral of death". Instead, past this many
    // ticks, the rest of the backlog is dropped (see run()): time is lost,
    // same as it would visibly be anyway, but the game recovers in one
    // frame instead of never.
    constexpr int MAX_TICKS_PER_FRAME = 5;

    // No tool requirement anywhere - a mismatched or missing tool just
    // falls back to bare-hand speed (BlockProperties::hardness) rather
    // than refusing to break the block at all. With no crafting system yet
    // to ever replace a lost or broken tool, a hard requirement (real
    // Minecraft's own "needs a pickaxe to drop stone") would risk
    // permanently soft-locking survival once that one tool is gone.
    float break_seconds_required(BlockType type, const ItemStack& selected) {
        const BlockProperties& block_properties = get_block_properties(type);
        float seconds = block_properties.hardness;
        if (selected.is_tool()) {
            const ItemProperties& tool_properties = get_item_properties(selected.tool);
            if (tool_properties.tool_kind == block_properties.effective_tool) {
                seconds /= tool_properties.mining_speed_multiplier;
            }
        }
        return seconds;
    }

    // A just-broken block pops off in roughly the direction it was struck
    // from (the targeted face's own outward normal), not straight up in
    // place - a gentle push plus a little sideways jitter so a cluster of
    // drops scatters instead of stacking in one spot, the same flavor real
    // Minecraft's own drop velocity has. Blocks/tick, matching
    // DroppedItem's own velocity unit.
    Vector3 break_launch_velocity(Vector3 face_normal) {
        constexpr float LAUNCH_ALONG_NORMAL = 0.06f;
        constexpr float LAUNCH_JITTER = 0.03f;
        auto jitter = [] { return (static_cast<float>(GetRandomValue(-100, 100)) / 100.0f) * LAUNCH_JITTER; };
        Vector3 launch = Vector3Scale(face_normal, LAUNCH_ALONG_NORMAL);
        launch.x += jitter();
        launch.z += jitter();
        return launch;
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

    // First-launch loading splash: titleIntroLogo.png, up for exactly as
    // long as the synchronous loads just below actually take. There's no
    // background-loading thread here - the loads block the same as they
    // always did: this just puts a frame on screen before that block
    // starts instead of leaving the window whatever the OS painted it as
    // (usually blank/black) for the whole duration.
    {
        const Texture2D& splash = TextureManager::get("sprites/gui/titleIntroLogo.png");
        BeginDrawing();
        ClearBackground(BLACK);
        Rectangle source = {0.0f, 0.0f, static_cast<float>(splash.width), static_cast<float>(splash.height)};
        Rectangle destination = {0.0f, 0.0f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
        DrawTexturePro(splash, source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
        EndDrawing();
    }

    settings = SettingsIO::load();
    SetTargetFPS(settings.target_fps);

    Load_block_definitions(); // needs a GL context, so only after InitWindow
    Load_item_definitions();
    audio.initialize();
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
    if (inventory_hud.is_open()) inventory_hud.close(inventory);
    save_player_state();

    EnableCursor();
    TextureManager::unload_all();
    FontManager::unload();
    unload_chunk_fog_shader();
    audio.shutdown();
    CloseWindow();
}

void GameEngine::add_object(std::unique_ptr<GameObject> object)
{
    objects.push_back(std::move(object));
}

void GameEngine::set_world(std::unique_ptr<World> new_world)
{
    dropped_items.clear();
    particles.clear();
    footstep_particle_distance = 0.0f;
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
        player_vertical_velocity = 0.0f; // don't carry a stale fall/jump speed into the new spawn point
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
    tick_dropped_items();
}

void GameEngine::tick_dropped_items()
{
    for (auto& item : dropped_items) {
        if (item->is_active()) item->tick_physics(world.get());
    }

    // Item-item magnetism: anything close enough (DroppedItem::
    // try_merge()'s own MERGE_RADIUS) folds into the other stack instead
    // of staying a separate entity - fewer entities to simulate/draw the
    // longer a pile of drops sits around. O(n^2) over dropped_items, fine
    // at the scale a handful of nearby breaks actually produces.
    for (size_t i = 0; i < dropped_items.size(); ++i) {
        if (!dropped_items[i]->is_active()) continue;
        for (size_t j = i + 1; j < dropped_items.size(); ++j) {
            if (!dropped_items[j]->is_active()) continue;
            // Keep offering item i more neighbors even after one merge -
            // it may still have room for another (try_merge() only stops
            // accepting once it's a full stack).
            dropped_items[i]->try_merge(*dropped_items[j]);
        }
    }
}

void GameEngine::update_dropped_items(float delta_time)
{
    for (auto& item : dropped_items) {
        if (!item->is_active()) continue;

        if (!inventory_hud.is_open()) {
            item->update_magnet_pull(delta_time, camera.position);
            if (item->can_pick_up() && Vector3Distance(item->get_position(), camera.position) <= ITEM_PICKUP_RADIUS) {
                const ItemStack& stack = item->get_stack();
                // Blocks merge into a matching stack or fill an empty slot
                // (add()); a tool never merges, but put_back() preserves
                // its exact durability instead of add_tool()'s always-
                // fresh one - either way, success removes the entity.
                bool picked_up = stack.is_tool() ? inventory.put_back(stack) : inventory.add(stack.block, stack.count) == 0;
                if (picked_up) item->set_active(false);
            }
        }
    }
    dropped_items.erase(std::remove_if(dropped_items.begin(), dropped_items.end(),
        [](const auto& item) { return !item->is_active(); }), dropped_items.end());
}

void GameEngine::spawn_dropped_item(const ItemStack& stack)
{
    if (!world || stack.empty()) return;

    Vector3 aim = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 spawn_position = Vector3Add(camera.position, Vector3Scale(aim, DROP_SPAWN_DISTANCE));
    Vector3 launch_velocity = Vector3Scale(aim, DROP_LAUNCH_SPEED);
    launch_velocity.y += DROP_LAUNCH_UP;

    dropped_items.push_back(std::make_unique<DroppedItem>(spawn_position, stack, launch_velocity));
}

void GameEngine::update(float delta_time)
{
    // Mouse wheel adjusts walking speed: one notch = one
    // CAMERA_MOVE_SPEED_SCROLL_STEP, clamped so it can never scroll down
    // to a standstill or up to an uncontrollable blur.
    float wheel_move = GetMouseWheelMove();
    if (wheel_move != 0.0f) {
        camera_move_speed = Clamp(camera_move_speed + wheel_move * CAMERA_MOVE_SPEED_SCROLL_STEP,
                                   CAMERA_MOVE_SPEED_MIN, CAMERA_MOVE_SPEED_MAX);
    }

    // Inventory: E toggles the storage panel open/closed (hardcoded, like
    // F3/F4/F5 below - not one of Settings' rebindable actions), freeing/
    // recapturing the cursor to match. Number keys pick a hotbar slot
    // directly, only while the grid isn't stealing input.
    if (world && IsKeyPressed(KEY_E)) {
        inventory_hud.toggle(inventory);
        if (inventory_hud.is_open()) EnableCursor(); else DisableCursor();
    }
    if (inventory_hud.is_open() && IsKeyPressed(KEY_ESCAPE)) {
        inventory_hud.close(inventory);
        DisableCursor();
    } else if (world && IsKeyPressed(KEY_ESCAPE)) {
        enter_state(GameState::Paused);
        return;
    }
    if (!inventory_hud.is_open()) {
        for (int slot = 0; slot < HOTBAR_SIZE; ++slot) {
            if (IsKeyPressed(KEY_ONE + slot)) inventory.selected_slot = slot;
        }
        // Q: throw one item out of the selected hotbar slot. While the
        // inventory screen is open instead, the equivalent (Q over a
        // hovered slot) is handled inside draw()'s own
        // inventory_hud.update_grid() call - it needs to know which slot
        // the mouse is over, which only that call already tracks.
        if (world && IsKeyPressed(KEY_Q)) {
            spawn_dropped_item(take_one_item(inventory.hotbar[inventory.selected_slot]));
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

    update_dropped_items(delta_time);
    particles.update(delta_time, world.get());

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

    // Free-look camera: rebindable keys (Settings) to move, mouse to look.
    // Today's defaults are still W/A/S/D + Space to jump - see
    // default_keybindings() - just no longer hardcoded here.
    auto is_action_down = [this](GameAction action) {
        return binding_down(settings.keybindings[static_cast<size_t>(action)]);
    };
    Vector3 movement = {0.0f, 0.0f, 0.0f};
    if (is_action_down(GameAction::MoveForward)) movement.x += camera_move_speed * delta_time;
    if (is_action_down(GameAction::MoveBackward)) movement.x -= camera_move_speed * delta_time;
    if (is_action_down(GameAction::MoveRight)) movement.y += camera_move_speed * delta_time;
    if (is_action_down(GameAction::MoveLeft)) movement.y -= camera_move_speed * delta_time;

    // Gravity - no more flying: standing on solid ground and not jumping
    // holds vertical velocity at 0 (otherwise it'd silently keep
    // accumulating downward while collision quietly absorbs it, then
    // dump all of that at once the instant the player walks off a ledge);
    // anything else (airborne, or the ascent right after a jump) is real
    // per-tick-accurate gravity/drag (PLAYER_GRAVITY/PLAYER_VERTICAL_DRAG
    // above).
    if (world) {
        bool grounded = player_box_blocked(*world,
            {camera.position.x, camera.position.y - CAMERA_EYE_HEIGHT - GROUND_CHECK_EPSILON, camera.position.z});
        if (grounded && player_vertical_velocity <= 0.0f) {
            player_vertical_velocity = is_action_down(GameAction::Jump) ? PLAYER_JUMP_VELOCITY : 0.0f;
        } else {
            player_vertical_velocity -= PLAYER_GRAVITY * delta_time;
            player_vertical_velocity *= std::pow(PLAYER_VERTICAL_DRAG, delta_time * TICKS_PER_SECOND);
        }
        movement.z = player_vertical_velocity * delta_time;
    } else {
        player_vertical_velocity = 0.0f;
    }

    Vector2 mouse_delta = GetMouseDelta();
    Vector3 rotation = {mouse_delta.x * CAMERA_MOUSE_SENSITIVITY, mouse_delta.y * CAMERA_MOUSE_SENSITIVITY, 0.0f};
    if (spawn_settle_frames > 0) {
        // See spawn_settle_frames's own comment: this delta might still be
        // a spurious startup jump, not real player input.
        rotation = {0.0f, 0.0f, 0.0f};
        --spawn_settle_frames;
    }

    Vector3 previous_camera_position = camera.position;
    UpdateCameraPro(&camera, movement, rotation, 0.0f);
    if (world) {
        // Collision only ever moves the camera *back* toward where it
        // already was, never sideways to some other, unintended spot - so
        // shifting camera.target by the same correction keeps look
        // direction exactly as UpdateCameraPro() just set it.
        Vector3 resolved_position = resolve_player_collision(*world, previous_camera_position, camera.position);
        Vector3 correction = Vector3Subtract(resolved_position, camera.position);
        camera.position = resolved_position;
        camera.target = Vector3Add(camera.target, correction);
    }

    // Emit by travelled distance, and only near a solid top surface. This
    // keeps the cadence frame-rate independent and prevents dust in flight.
    if (world) {
        float feet_y = camera.position.y - CAMERA_EYE_HEIGHT;
        int ground_x = static_cast<int>(std::floor(camera.position.x));
        int ground_y = static_cast<int>(std::floor(feet_y - 0.06f));
        int ground_z = static_cast<int>(std::floor(camera.position.z));
        BlockType ground_type = world->get_block(ground_x, ground_y, ground_z);
        bool grounded = get_block_properties(ground_type).solid &&
                        std::fabs(feet_y - (ground_y + 1.0f)) <= 0.22f;
        float dx = camera.position.x - previous_camera_position.x;
        float dz = camera.position.z - previous_camera_position.z;
        float horizontal_distance = std::sqrt(dx * dx + dz * dz);
        if (grounded && horizontal_distance > 0.0001f) {
            footstep_particle_distance += horizontal_distance;
            int emitted = 0;
            while (footstep_particle_distance >= 0.55f && emitted < 3) {
                particles.spawn_footstep(ground_type,
                    Vector3{camera.position.x, ground_y + 1.0f, camera.position.z});
                audio.play_step(ground_type,
                    Vector3{camera.position.x, ground_y + 1.0f, camera.position.z}, camera.position);
                footstep_particle_distance -= 0.55f;
                ++emitted;
            }
        } else if (!grounded) {
            footstep_particle_distance = 0.0f;
        }
    }

    // camera.target isn't a unit vector (it's an arbitrary point ahead of
    // the camera), so the aim direction needs normalizing before it's used
    // as a ray direction.
    Vector3 aim = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    // Recomputed every frame (not just on click) so draw() can outline
    // whatever's targeted, out to the longer of the two reaches (place's)
    // so the outline still shows a block that's placeable but too far to
    // break.
    targeted_block = world ? world->raycast(camera.position, aim, PLACE_REACH) : std::nullopt;

    // Left click breaks whatever's aimed at, held down over time rather
    // than instantly - how long depends on the block and, if it's the
    // right kind for the job, the selected tool (break_seconds_required()
    // above). Right click places one block against the face the crosshair
    // is aimed at (the cell just outside the targeted block, in the
    // direction of the hit face's own outward normal), within the longer
    // PLACE_REACH - or opens a container if the targeted block is one.
    bool break_held = world && binding_down(settings.keybindings[static_cast<size_t>(GameAction::BreakBlock)]);
    if (!break_held) {
        is_breaking = false;
        breaking_progress = 0.0f;
    }

    if (break_held) {
        auto hit = world->raycast(camera.position, aim, BREAK_REACH);
        if (!hit) {
            is_breaking = false;
            breaking_progress = 0.0f;
        } else {
            // A fresh block (first frame of the hold, or the aim moved to
            // a different one since) restarts progress from zero.
            if (!is_breaking || hit->x != breaking_x || hit->y != breaking_y || hit->z != breaking_z) {
                is_breaking = true;
                breaking_x = hit->x;
                breaking_y = hit->y;
                breaking_z = hit->z;
                breaking_progress = 0.0f;
            }

            BlockType target_type = world->get_block(breaking_x, breaking_y, breaking_z);
            ItemStack& selected = inventory.hotbar[inventory.selected_slot];
            breaking_progress += delta_time / break_seconds_required(target_type, selected);

            if (breaking_progress >= 1.0f) {
                if (std::optional<BlockType> broken = world->break_block(breaking_x, breaking_y, breaking_z)) {
                    Vector3 center = {breaking_x + 0.5f, breaking_y + 0.5f, breaking_z + 0.5f};
                    particles.spawn_hit(*broken, Vector3Add(center, Vector3Scale(hit->normal, 0.505f)), hit->normal);
                    particles.spawn_destroy(*broken, center);
                    audio.play_break(*broken, center, camera.position);
                    ItemStack broken_stack;
                    broken_stack.block = *broken;
                    broken_stack.count = 1;
                    dropped_items.push_back(std::make_unique<DroppedItem>(
                        center, broken_stack, break_launch_velocity(hit->normal)));

                    // Whatever tool broke it loses 1 durability, whether or
                    // not it was actually the right kind for a speed bonus
                    // - same as real Minecraft.
                    if (selected.is_tool() && --selected.durability <= 0) {
                        selected.clear();
                    }
                }
                is_breaking = false;
                breaking_progress = 0.0f;
            }
        }
    } else if (world && binding_pressed(settings.keybindings[static_cast<size_t>(GameAction::PlaceBlock)])) {
        // Right-clicking a Workbench/Furnace/Chest opens its container
        // screen instead of placing a block against it - same priority
        // real Minecraft gives it (you can't place a block onto one of
        // these by right-clicking any of their faces either).
        std::optional<InventoryHud::ContainerKind> container_kind = targeted_block
            ? container_kind_for_block(world->get_block(targeted_block->x, targeted_block->y, targeted_block->z))
            : std::nullopt;
        if (container_kind) {
            inventory_hud.open_container(*container_kind);
            EnableCursor();
        } else {
            // Right-click-to-place only ever consumes a block stack - a
            // selected tool has nothing to place (and isn't consumed by
            // right-clicking with it either, same as vanilla: tools have
            // no use-on-block action here yet beyond mining).
            ItemStack& selected = inventory.hotbar[inventory.selected_slot];
            if (targeted_block && !selected.empty() && !selected.is_tool()) {
                int place_x = targeted_block->x + static_cast<int>(targeted_block->normal.x);
                int place_y = targeted_block->y + static_cast<int>(targeted_block->normal.y);
                int place_z = targeted_block->z + static_cast<int>(targeted_block->normal.z);
                if (world->place_block(place_x, place_y, place_z, selected.block)) {
                    if (--selected.count <= 0) selected.clear();
                }
            }
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

    // How far the current frame already is into the *next* tick (0 right
    // after one lands, approaching 1 right before the next does) - every
    // tick-simulated entity (dropped items, falling blocks) interpolates
    // its last two tick positions by this instead of snapping between
    // them, so 20Hz physics still reads as smooth motion at render rate.
    float tick_alpha = std::clamp(tick_accumulator / TICK_DURATION, 0.0f, 1.0f);

    BeginMode3D(camera);
    draw_skybox(camera.position);
    if (world) {
        // Wireframe ("skeleton") debug view: draws the exact same chunk
        // meshes, just as GL_LINE edges instead of filled/textured
        // triangles - every block's own face boundaries end up visible,
        // which is what actually reveals block positions/mesh structure,
        // rather than a separate position-label overlay.
        if (show_wireframe) rlEnableWireMode();
        world->draw_opaque(camera);
        world->draw_falling_blocks(tick_alpha);
        if (show_chunk_borders) {
            world->draw_chunk_borders();
        }
    }
    for (auto& object : objects) {
        if (object->is_active()) {
            object->draw();
        }
    }
    for (const auto& item : dropped_items) {
        if (item->is_active()) item->render(tick_alpha);
    }
    particles.draw(camera);
    if (world) {
        // Water/glass/ice, drawn only now - after every opaque and solid-
        // entity thing above - so its own alpha blending correctly
        // composites over whatever's actually underwater (a dropped item,
        // say) instead of always rendering in front of it regardless of
        // real depth.
        world->draw_translucent(camera);
        if (show_wireframe) rlDisableWireMode();
    }
    if (targeted_block) {
        ui::block_outline(targeted_block->x, targeted_block->y, targeted_block->z);
        if (is_breaking && targeted_block->x == breaking_x && targeted_block->y == breaking_y &&
            targeted_block->z == breaking_z) {
            ui::block_breaking_overlay(breaking_x, breaking_y, breaking_z, breaking_progress);
        }
    }
    EndMode3D();

    ui::crosshair();

    if (is_breaking) {
        float bar_x = GetScreenWidth() / 2.0f - BREAK_BAR_WIDTH / 2.0f;
        float bar_y = GetScreenHeight() / 2.0f + BREAK_BAR_OFFSET_Y;
        float fill_width = BREAK_BAR_WIDTH * std::clamp(breaking_progress, 0.0f, 1.0f);
        DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                      static_cast<int>(BREAK_BAR_WIDTH), static_cast<int>(BREAK_BAR_HEIGHT), BREAK_BAR_BACKGROUND);
        DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                      static_cast<int>(fill_width), static_cast<int>(BREAK_BAR_HEIGHT), BREAK_BAR_FILL);
    }

    if (world) {
        inventory_hud.draw_hotbar(inventory);
        // Drawn and click-handled together here (not from update()) - the
        // same immediate-mode pattern every menu screen already uses, and
        // simplest since update() already returned early while it's open.
        if (inventory_hud.is_open()) {
            if (std::optional<ItemStack> dropped = inventory_hud.update_grid(inventory)) {
                spawn_dropped_item(*dropped);
            }
        }
    }

    if (show_debug_overlay && world) {
        ui::draw_debug_overlay(camera, *world, BREAK_REACH, camera_move_speed, game_tick);
    }

    EndDrawing();
}

void GameEngine::run()
{
    while (!WindowShouldClose() && !quit_requested) {
        float delta_time = GetFrameTime();
        audio.update(delta_time, settings, state == GameState::Playing);
        if (state != GameState::Playing) {
            update_and_draw_menu();
            continue;
        }

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

        // Exactly once per rendered frame, never from inside tick() (which
        // can run several times in one frame after a stall - see
        // MAX_TICKS_PER_FRAME just above): integrates whatever background
        // chunk generation/meshing (World's ChunkWorkerPool) finished since
        // last frame, under its own small per-frame budget. Draining this
        // once per tick instead would let a stall's own catch-up ticks
        // multiply that budget right on top of the stall that just
        // happened - see World::integrate_worker_results()'s own comment.
        if (world) world->integrate_worker_results();

        update(delta_time);
        draw();
    }
}

void GameEngine::update_and_draw_menu()
{
    BeginDrawing();
    ClearBackground(Color{24, 24, 28, 255}); // fallback - covered by one of the two textures below except for one un-drawn edge case (see the comment on the `default` GameState::Playing branch)

    // MainMenu gets the blurred title panorama; every other menu screen
    // (world list/create, settings, the in-game pause menu - none of them
    // have a 3D world of their own to show behind them here) gets the
    // tiled dirt "options background" instead.
    if (state == GameState::MainMenu) {
        ui::title_background();
    } else if (state != GameState::Playing) {
        ui::menu_background();
    }

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
    auto new_world = std::make_unique<World>(config);
    // Inventory belongs to a save, never to the GameEngine session. Without
    // this reset, entering a brand-new world after leaving another one
    // leaked the previous world's stacks into it.
    inventory = default_inventory();

    // Resume exactly where the player left off last time, if they ever
    // have before (see save_player_state()) - bypasses set_world()'s own
    // find_spawn_position() call entirely rather than overriding its
    // result afterward: find_spawn_position() itself calls
    // update_chunk_states() to populate the area it searches, so calling
    // it and then immediately jumping somewhere else meant generating two
    // full batches of chunks (once around a throwaway point near world
    // origin, once around the real position) every time a previously-
    // played world was reopened - measured as roughly doubling load time.
    // A brand new world has no player.json yet, so falls through to
    // set_world()'s normal spawn search unchanged.
    if (std::optional<PlayerSaveState> saved = WorldSave::load_player_state(folder_name)) {
        world = std::move(new_world);
        camera.position = saved->position;
        camera.target = Vector3Add(camera.position, Vector3Scale(saved->forward, 10.0f));
        inventory = saved->inventory;
        spawn_settle_frames = 3; // see its own comment on set_world()
        player_vertical_velocity = 0.0f; // see set_world()'s own comment
        // Blocking: the world needs to actually be there around the
        // player's resumed position by the time Playing starts, not merely
        // dispatched - see World::update_chunk_states_blocking()'s own
        // comment.
        world->update_chunk_states_blocking(camera.position);
    } else {
        set_world(std::move(new_world));
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
    dropped_items.clear();
    particles.clear();
    footstep_particle_distance = 0.0f;
    current_world_folder.clear();
    inventory_hud.close(inventory);
    enter_state(GameState::MainMenu);
}
