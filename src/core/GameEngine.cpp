#include "core/GameEngine.hpp"
#include "core/Block.hpp"
#include "core/TextureManager.hpp"
#include "core/Tick.hpp"
#include "player/Item.hpp"
#include "player/DropTable.hpp"
#include "player/Recipe.hpp"
#include "player/PlayerController.hpp"
#include "ui/FontManager.hpp"
#include "ui/Widgets.hpp"
#include "ui/Localization.hpp"
#include "core/Keybindings.hpp"
#include "core/WorldSave.hpp"
#include "rendering/Skybox.hpp"
#include "ui/DebugOverlay.hpp"
#include "worldgen/Structure.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr float CAMERA_MOVE_SPEED_DEFAULT = 4.0f; // world units per second
    // Only used by the scroll-to-change-speed handling commented out in
    // update() for now - commented out alongside it so they don't sit
    // here unused (and unused-const-variable warned about) in the
    // meantime. Uncomment together.
    // constexpr float CAMERA_MOVE_SPEED_MIN = 2.0f;
    // constexpr float CAMERA_MOVE_SPEED_MAX = 100.0f;
    // constexpr float CAMERA_MOVE_SPEED_SCROLL_STEP = 2.0f; // per wheel notch
    constexpr float CAMERA_MOUSE_SENSITIVITY = 0.08f;

    constexpr float SURVIVAL_REACH = 4.5f;
    constexpr float CREATIVE_REACH = 5.0f;

    // The hold-to-break progress bar, drawn just under the crosshair - see
    // GameEngine::draw() and the is_breaking/breaking_progress fields.
    constexpr float BREAK_BAR_WIDTH    = 60.0f;
    constexpr float BREAK_BAR_HEIGHT   = 6.0f;
    constexpr float BREAK_BAR_OFFSET_Y = 28.0f; // below screen center
    constexpr Color BREAK_BAR_BACKGROUND = {0, 0, 0, 150};
    constexpr Color BREAK_BAR_FILL       = {255, 255, 255, 220};

    // Subtle atmospheric haze over the whole scene - a constant, very low
    // blend toward the sky's own horizon color, independent of the chunk
    // shader's distance fog (which only ramps in near the render-distance
    // edge - see World::draw_opaque). Gives even nearby geometry a faint
    // sense of depth/atmosphere instead of reading perfectly crisp right
    // up against the camera; deliberately subtle (~7%), not real fog.
    constexpr unsigned char CAMERA_HAZE_ALPHA = 18;

    // Q-drop (spawn_dropped_item()): thrown out from just in front of the
    // player - not right at their own position, or it would immediately
    // re-trigger their own pickup radius - forward and slightly up,
    // blocks/tick to match DroppedItem's own velocity unit, the same
    // forward-and-up toss real Minecraft gives a manually dropped item.
    constexpr float DROP_SPAWN_DISTANCE = 0.6f;
    constexpr float DROP_LAUNCH_SPEED   = 0.15f;
    constexpr float DROP_LAUNCH_UP      = 0.05f;

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

    // --- Environmental damage tuning (Survival only - see
    // GameEngine::update_player_damage()) - half-heart units throughout,
    // same as PlayerHealth itself, matching real Minecraft's own damage
    // numbers directly (vanilla's damage points already *are* half-hearts).

    // Real Minecraft's own fall-damage rule: the first 3 blocks are free,
    // then 1 point (half a heart) per additional whole block fallen.
    constexpr int FALL_DAMAGE_SAFE_BLOCKS = 3;

    constexpr int LAVA_DAMAGE = 4; // per contact tick - lava is meant to kill fast
    constexpr float FIRE_DURATION_FROM_LAVA_SECONDS = 15.0f; // vanilla's own lava burn duration
    constexpr int FIRE_DAMAGE = 1;
    constexpr float FIRE_TICK_INTERVAL_SECONDS = 1.0f;

    constexpr float MAX_AIR_SECONDS = 15.0f; // vanilla's own breath meter length
    constexpr int DROWN_DAMAGE = 2;
    constexpr float DROWN_TICK_INTERVAL_SECONDS = 1.0f;

    constexpr int SUFFOCATION_DAMAGE = 1;
    constexpr float SUFFOCATION_TICK_INTERVAL_SECONDS = 1.0f;

    constexpr int CACTUS_DAMAGE = 1; // per-frame attempt - player_health's own invulnerability window throttles this to ~2/second

    // How far below the world's own floor (MIN_WORLD_Y - see Chunk.hpp) a
    // fall counts as "into the void" - a safety net for however a player
    // might end up under the terrain (bedrock should normally prevent it
    // outright), not a feature meant to be reachable in ordinary play.
    constexpr float VOID_DAMAGE_Y = static_cast<float>(MIN_WORLD_Y - 4);
    constexpr int VOID_DAMAGE = 4; // same per-tick rate as lava - falling forever shouldn't take long to end

    constexpr float DEATH_RESPAWN_SECONDS = 2.0f; // real Minecraft's own death-screen delay, just without the screen/button
    constexpr float HURT_FLASH_SECONDS    = 0.3f;

    // Breaking with the wrong tool (or bare hands) still works, just at the
    // bare-hand multiplier (1x) below rather than refusing outright - no
    // hard "needs a pickaxe to drop stone" requirement, since a lost/broken
    // tool can now be recrafted (see Recipe.hpp) but resolve_block_drops()'s
    // own tool gating is still what actually withholds a drop for an ore
    // mined with too weak a tool.
    //
    // Beta 1.7.3's own tick-quantized formula (breaking is simulated at the
    // game's 20 ticks/second, not a continuous real-number countdown):
    //   progress per tick = multiplier / hardness / 30
    //   ticks required     = ceil(1 / progress per tick) = ceil(30 * hardness / multiplier)
    // then converted to seconds at TICKS_PER_SECOND so breaking_progress's
    // own delta_time accumulation still lands on a whole-tick boundary
    // (e.g. Stone + Diamond Pickaxe: 30*1.5/8 = 5.625 -> 6 ticks -> 0.3s).
    float break_seconds_required(BlockType type, const ItemStack& selected) {
        const BlockProperties& block_properties = get_block_properties(type);
        float multiplier = 1.0f;
        if (selected.is_tool()) {
            const ItemProperties& tool_properties = get_item_properties(selected.tool);
            if (tool_properties.tool_kind == block_properties.effective_tool) {
                multiplier = tool_properties.mining_speed_multiplier;
            }
        }
        int ticks = std::max(1, static_cast<int>(std::ceil(30.0f * block_properties.hardness / multiplier)));
        return static_cast<float>(ticks) / static_cast<float>(TICKS_PER_SECOND);
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

    // Leaf decay (see GameEngine::check_leaf_decay_near) - every wood/leaf
    // pair this build has.
    constexpr int LEAF_DECAY_SCAN_RADIUS = 5; // around a just-removed log, how far out to look for leaves that might now be orphaned
    constexpr int LEAF_DECAY_LOG_RADIUS = 4;  // how far a leaf itself may look for a surviving log before it decays - roughly matches real Minecraft's own leaf-decay range

    bool is_log_block(BlockType type) {
        return type == BlockType::OakLog || type == BlockType::SpruceLog || type == BlockType::BirchLog;
    }
    bool is_leaf_block(BlockType type) {
        return type == BlockType::Foliage || type == BlockType::SpruceFoliage || type == BlockType::BirchFoliage;
    }

    // Which way a just-placed directional block (see block_is_directional())
    // should face - its front toward the player, i.e. the opposite of
    // whichever way the player is currently looking (real Minecraft's own
    // furnace/dispenser/pumpkin placement rule), bucketed into the nearest
    // of the 4 cardinal directions and ignoring pitch entirely (a block's
    // facing is horizontal-only).
    HorizontalDirection direction_facing_player(Vector3 camera_forward) {
        float away_x = -camera_forward.x;
        float away_z = -camera_forward.z;
        if (std::fabs(away_x) > std::fabs(away_z)) {
            return away_x > 0.0f ? HorizontalDirection::East : HorizontalDirection::West;
        }
        return away_z > 0.0f ? HorizontalDirection::South : HorizontalDirection::North;
    }
    bool has_nearby_log(World& world, int x, int y, int z) {
        for (int lx = -LEAF_DECAY_LOG_RADIUS; lx <= LEAF_DECAY_LOG_RADIUS; ++lx) {
            for (int ly = -LEAF_DECAY_LOG_RADIUS; ly <= LEAF_DECAY_LOG_RADIUS; ++ly) {
                for (int lz = -LEAF_DECAY_LOG_RADIUS; lz <= LEAF_DECAY_LOG_RADIUS; ++lz) {
                    if (is_log_block(world.get_block(x + lx, y + ly, z + lz))) return true;
                }
            }
        }
        return false;
    }

    // A felled tree's leaves don't vanish in the same frame the log comes
    // down - each one gets its own random delay in this range before it's
    // actually removed, so the canopy visibly thins out over a couple of
    // seconds instead of blinking away all at once.
    constexpr float LEAF_DECAY_MIN_DELAY_SECONDS = 1.0f;
    constexpr float LEAF_DECAY_MAX_DELAY_SECONDS = 4.0f;
}

GameEngine::GameEngine(int screen_width, int screen_height, const char* title)
    : settings(SettingsIO::load()), camera_move_speed(CAMERA_MOVE_SPEED_DEFAULT)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(settings.window_width > 0 ? settings.window_width : screen_width,
               settings.window_height > 0 ? settings.window_height : screen_height, title);
    SetWindowMinSize(960, 540);

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
        Rectangle source      = {0.0f, 0.0f, static_cast<float>(splash.width), static_cast<float>(splash.height)};
        Rectangle destination = {0.0f, 0.0f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
        DrawTexturePro(splash, source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
        EndDrawing();
    }

    SetTargetFPS(settings.target_fps);

    Load_block_definitions(); // needs a GL context, so only after InitWindow
    Load_item_definitions();
    Load_drop_table(); // needs both name tables above ready to resolve against
    Load_recipes();
    audio.initialize();
    ui::set_language(settings.language);
    ui::set_sound_callback([this](ui::SoundEvent event) {
        if (event == ui::SoundEvent::Click) audio.play_ui_click();
        else if (event == ui::SoundEvent::Hover) audio.play_ui_hover();
    });
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
    ui::set_sound_callback({});
    if (IsTextureValid(pause_snapshot)) UnloadTexture(pause_snapshot);
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
        camera.position.y += PlayerController::EYE_HEIGHT;
        // North: -Z in this engine's convention (see Chunk.cpp's
        // CUBE_FACES comment). Level, not angled down - the old downward
        // tilt was there to see a bird's-eye view from high above the
        // world; standing on real ground, a level look is the natural one.
        camera.target = {camera.position.x, camera.position.y, camera.position.z - 10.0f};
        spawn_settle_frames = 3; // see its own comment - 2 measured, +1 margin
        player_controller.reset();
        player_health.reset();
        reset_life_timers();
        camera_view = CameraView::FirstPerson;
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
        // Picks up a render/fog distance change made from the pause menu's
        // Settings screen immediately, rather than only the next time a
        // world is started (see World::set_view_distance's own comment) -
        // a no-op most ticks, when neither value actually changed since.
        world->set_view_distance(settings.render_distance_chunks, settings.fog_distance_blocks);
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

        // Opening the inventory/a container is just a UI overlay, not a
        // pause - the world keeps living behind it (real Minecraft picks
        // up nearby items, runs mob AI, etc. right through an open
        // inventory screen too), so pickup/magnet-pull run unconditionally
        // here, same as every other line in update().
        Vector3 pickup_point = player_controller.closest_hitbox_point(camera, item->get_position());
        item->update_magnet_pull(delta_time, pickup_point);
        if (item->can_pick_up() && Vector3Distance(item->get_position(), pickup_point) <= ITEM_PICKUP_RADIUS) {
            const ItemStack& stack = item->get_stack();
            // Blocks merge into a matching stack or fill an empty slot
            // (add()); a tool never merges, but put_back() preserves
            // its exact durability instead of add_tool()'s always-
            // fresh one; a material stacks the same way a block does,
            // just keyed by item type instead - put_back() already
            // dispatches on is_material()/is_tool() internally, so it's
            // the one call that's correct for all three cases here.
            bool picked_up = stack.holds_item() ? inventory.put_back(stack) : inventory.add(stack.block, stack.count) == 0;
            if (picked_up) {
                item->set_active(false);
                audio.play_item_pickup();
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

    dropped_items.push_back(std::make_unique<DroppedItem>(
        spawn_position, stack, launch_velocity, DroppedItemOrigin::PlayerThrown));
}

void GameEngine::check_leaf_decay_near(int log_x, int log_y, int log_z)
{
    if (!world) return;

    for (int dx = -LEAF_DECAY_SCAN_RADIUS; dx <= LEAF_DECAY_SCAN_RADIUS; ++dx) {
        for (int dy = -LEAF_DECAY_SCAN_RADIUS; dy <= LEAF_DECAY_SCAN_RADIUS; ++dy) {
            for (int dz = -LEAF_DECAY_SCAN_RADIUS; dz <= LEAF_DECAY_SCAN_RADIUS; ++dz) {
                int x = log_x + dx, y = log_y + dy, z = log_z + dz;
                if (!is_leaf_block(world->get_block(x, y, z))) continue;
                if (has_nearby_log(*world, x, y, z)) continue;

                bool already_queued = false;
                for (const PendingLeafDecay& pending : pending_leaf_decay) {
                    if (pending.x == x && pending.y == y && pending.z == z) { already_queued = true; break; }
                }
                if (already_queued) continue;

                float delay = static_cast<float>(GetRandomValue(
                    static_cast<int>(LEAF_DECAY_MIN_DELAY_SECONDS * 100.0f),
                    static_cast<int>(LEAF_DECAY_MAX_DELAY_SECONDS * 100.0f))) / 100.0f;
                pending_leaf_decay.push_back({x, y, z, delay});
            }
        }
    }
}

void GameEngine::update_leaf_decay(float delta_time)
{
    if (!world) { pending_leaf_decay.clear(); return; }

    for (size_t i = 0; i < pending_leaf_decay.size();) {
        pending_leaf_decay[i].remaining_seconds -= delta_time;
        if (pending_leaf_decay[i].remaining_seconds > 0.0f) { ++i; continue; }

        PendingLeafDecay entry = pending_leaf_decay[i];
        pending_leaf_decay[i] = pending_leaf_decay.back();
        pending_leaf_decay.pop_back();

        // Re-validate - a log could have been placed back nearby, or this
        // leaf could already be gone another way, since it was queued.
        if (!is_leaf_block(world->get_block(entry.x, entry.y, entry.z))) continue;
        if (has_nearby_log(*world, entry.x, entry.y, entry.z)) continue;

        if (std::optional<BlockType> decayed = world->break_block(entry.x, entry.y, entry.z)) {
            Vector3 center = {entry.x + 0.5f, entry.y + 0.5f, entry.z + 0.5f};
            particles.spawn_destroy(*decayed, center);
            audio.play_break(*decayed, center, camera.position);
            // Decay has no tool of its own, same as real Minecraft - it
            // just rolls the same table a bare-handed break would (a
            // sapling/stick/apple chance, most of the time nothing).
            for (const DropRoll& drop : resolve_block_drops(*decayed, ItemStack{})) {
                ItemStack drop_stack;
                if (drop.is_item) drop_stack.tool = drop.item; else drop_stack.block = drop.block;
                drop_stack.count = drop.count;
                dropped_items.push_back(std::make_unique<DroppedItem>(
                    center, drop_stack, Vector3{0.0f, 0.02f, 0.0f}, DroppedItemOrigin::Natural));
            }
        }
        // Not ++i - pop_back() just moved a different element into slot i.
    }
}

namespace {
    constexpr float SAPLING_GROW_MIN_SECONDS = 30.0f;
    constexpr float SAPLING_GROW_MAX_SECONDS = 90.0f;
    // How long a blocked attempt (something built over the trunk's own
    // column since it was planted) waits before trying again - much
    // shorter than the grow delay itself, same "keep polling, cheaply"
    // idea as a blocked vanilla sapling re-rolling every random tick
    // instead of just giving up.
    constexpr float SAPLING_RETRY_SECONDS = 5.0f;
    constexpr int SAPLING_TRUNK_HEIGHT_MIN = 4;
    constexpr int SAPLING_TRUNK_HEIGHT_MAX = 6;
}

void GameEngine::queue_sapling_growth(int x, int y, int z)
{
    float delay = static_cast<float>(GetRandomValue(
        static_cast<int>(SAPLING_GROW_MIN_SECONDS), static_cast<int>(SAPLING_GROW_MAX_SECONDS)));
    pending_sapling_growth.push_back({x, y, z, delay});
}

void GameEngine::update_sapling_growth(float delta_time)
{
    if (!world) { pending_sapling_growth.clear(); return; }

    for (size_t i = 0; i < pending_sapling_growth.size();) {
        pending_sapling_growth[i].remaining_seconds -= delta_time;
        if (pending_sapling_growth[i].remaining_seconds > 0.0f) { ++i; continue; }

        PendingSaplingGrowth entry = pending_sapling_growth[i];
        pending_sapling_growth[i] = pending_sapling_growth.back();
        pending_sapling_growth.pop_back();

        // Re-validate - it could have been broken, or something else
        // placed over it, since it was queued.
        if (world->get_block(entry.x, entry.y, entry.z) != BlockType::OakSapling) continue;

        int trunk_height = GetRandomValue(SAPLING_TRUNK_HEIGHT_MIN, SAPLING_TRUNK_HEIGHT_MAX);
        bool trunk_clear = true;
        for (int y = 1; y <= trunk_height; ++y) {
            BlockType existing = world->get_block(entry.x, entry.y + y, entry.z);
            if (existing != BlockType::Air && existing != BlockType::Foliage) { trunk_clear = false; break; }
        }
        if (!trunk_clear) {
            pending_sapling_growth.push_back({entry.x, entry.y, entry.z, SAPLING_RETRY_SECONDS});
            continue;
        }

        // Clear the sapling itself first (no drop/particles - it's turning
        // into the tree, not being destroyed) so the origin block (the
        // bottom trunk log, landing exactly on the sapling's own position)
        // finds Air like every other block placed below, instead of
        // World::place_structure_block() needing its own OakSapling
        // special case.
        world->break_block(entry.x, entry.y, entry.z);

        // Same template make_oak_tree()/StructureGenerator place at
        // world-generation time, just placed here one world-space block at
        // a time via World::place_structure_block() instead of
        // Chunk::set_block() - this runs at an arbitrary runtime position,
        // not bounded to one already-open Chunk the way generation is.
        Structure tree = make_oak_tree(trunk_height);
        for (const StructureBlock& block : tree.get_blocks()) {
            world->place_structure_block(entry.x + block.x, entry.y + block.y, entry.z + block.z,
                block.type, block.replace_rule == StructureReplaceRule::AirOrFoliage);
        }
        // Not ++i - pop_back() just moved a different element into slot i.
    }
}

void GameEngine::spill_chest_if_any(int x, int y, int z)
{
    if (!world) return;
    std::array<ItemStack, INVENTORY_STORAGE_SIZE> contents = world->take_chest_inventory(x, y, z);
    Vector3 center = {x + 0.5f, y + 0.5f, z + 0.5f};
    for (const ItemStack& stack : contents) {
        if (stack.empty()) continue;
        dropped_items.push_back(std::make_unique<DroppedItem>(
            center, stack, break_launch_velocity({0.0f, 1.0f, 0.0f}), DroppedItemOrigin::Natural));
    }
}

void GameEngine::check_grass_support_above(int x, int y, int z)
{
    if (!world) return;
    if (world->get_block(x, y + 1, z) != BlockType::ShortGrass) return;

    if (std::optional<BlockType> broken = world->break_block(x, y + 1, z)) {
        Vector3 center = {x + 0.5f, y + 1.5f, z + 0.5f};
        particles.spawn_destroy(*broken, center);
        audio.play_break(*broken, center, camera.position);
        for (const DropRoll& drop : resolve_block_drops(*broken, ItemStack{})) {
            ItemStack drop_stack;
            if (drop.is_item) drop_stack.tool = drop.item; else drop_stack.block = drop.block;
            drop_stack.count = drop.count;
            dropped_items.push_back(std::make_unique<DroppedItem>(
                center, drop_stack, Vector3{0.0f, 0.02f, 0.0f}, DroppedItemOrigin::Natural));
        }
    }
}

void GameEngine::apply_damage(int amount, DamageSource source)
{
    if (player_health.damage(amount, source)) {
        hurt_flash_seconds = HURT_FLASH_SECONDS;
    }
}

void GameEngine::update_player_damage(float delta_time)
{
    // Fall damage - PlayerController reports this exactly once, the frame
    // its feet actually land, regardless of whether that lands inside this
    // function's own "already dead" early state below (apply_damage/
    // player_health.damage() themselves no-op once dead, so it's harmless
    // to still consume it here rather than leave it queued for a fall that
    // already happened).
    float landing_fall_distance = player_controller.consume_landing_fall_distance();
    if (landing_fall_distance >= 0.0f) {
        int fall_damage = static_cast<int>(std::floor(landing_fall_distance)) - FALL_DAMAGE_SAFE_BLOCKS;
        if (fall_damage > 0) apply_damage(fall_damage, DamageSource::Fall);
    }

    // Lava: hurts every frame it's touched (player_health's own 0.5s
    // invulnerability window is what actually paces this to real
    // Minecraft's own per-half-second lava tick), and always re-arms the
    // burn timer below to its full duration - a single instant of contact
    // still burns for the whole FIRE_DURATION_FROM_LAVA_SECONDS afterward,
    // same as vanilla.
    if (player_controller.is_in_lava()) {
        apply_damage(LAVA_DAMAGE, DamageSource::Lava);
        fire_seconds_remaining = FIRE_DURATION_FROM_LAVA_SECONDS;
    }

    // Burning: water immediately extinguishes it (real Minecraft too),
    // otherwise it counts down on its own and hurts once per
    // FIRE_TICK_INTERVAL_SECONDS regardless of whether the player is still
    // anywhere near the lava that started it.
    if (player_controller.is_in_water()) fire_seconds_remaining = 0.0f;
    if (fire_seconds_remaining > 0.0f) {
        fire_seconds_remaining = std::max(0.0f, fire_seconds_remaining - delta_time);
        fire_damage_timer += delta_time;
        if (fire_damage_timer >= FIRE_TICK_INTERVAL_SECONDS) {
            fire_damage_timer -= FIRE_TICK_INTERVAL_SECONDS;
            apply_damage(FIRE_DAMAGE, DamageSource::Fire);
        }
    } else {
        fire_damage_timer = 0.0f;
    }

    // Drowning: a breath meter that drains only while the eye position
    // specifically is submerged (see PlayerController::is_head_submerged())
    // and otherwise recovers - once it runs out, one hit every
    // DROWN_TICK_INTERVAL_SECONDS for as long as the head stays under.
    if (player_controller.is_head_submerged()) {
        air_seconds = std::max(0.0f, air_seconds - delta_time);
        if (air_seconds <= 0.0f) {
            drown_damage_timer += delta_time;
            if (drown_damage_timer >= DROWN_TICK_INTERVAL_SECONDS) {
                drown_damage_timer -= DROWN_TICK_INTERVAL_SECONDS;
                apply_damage(DROWN_DAMAGE, DamageSource::Drown);
            }
        }
    } else {
        air_seconds = MAX_AIR_SECONDS;
        drown_damage_timer = 0.0f;
    }

    // Suffocation: a solid, opaque block clipped into the player's own eye
    // position (see PlayerController::is_suffocating()) - typically a
    // block placed where the player is standing.
    if (player_controller.is_suffocating()) {
        suffocation_damage_timer += delta_time;
        if (suffocation_damage_timer >= SUFFOCATION_TICK_INTERVAL_SECONDS) {
            suffocation_damage_timer -= SUFFOCATION_TICK_INTERVAL_SECONDS;
            apply_damage(SUFFOCATION_DAMAGE, DamageSource::Suffocation);
        }
    } else {
        suffocation_damage_timer = 0.0f;
    }

    // Cactus: same per-frame-attempt/invulnerability-throttled shape as
    // lava above.
    if (player_controller.is_touching_cactus()) {
        apply_damage(CACTUS_DAMAGE, DamageSource::Cactus);
    }

    // Void: a safety net for ending up below the world's own floor (see
    // VOID_DAMAGE_Y's own comment) - same throttling idea as lava/cactus.
    if (camera.position.y < VOID_DAMAGE_Y) {
        apply_damage(VOID_DAMAGE, DamageSource::Void);
    }

    player_health.update(delta_time);
    hurt_flash_seconds = std::max(0.0f, hurt_flash_seconds - delta_time);

    // Death/respawn: death_respawn_timer is armed exactly once, the frame
    // health first reaches 0 (was_dead_last_frame catches that edge so a
    // second frame of already being dead doesn't keep resetting the
    // countdown back to full).
    bool dead_now = player_health.is_dead();
    if (dead_now && !was_dead_last_frame) {
        death_respawn_timer = DEATH_RESPAWN_SECONDS;
    }
    was_dead_last_frame = dead_now;
    if (dead_now) {
        death_respawn_timer -= delta_time;
        if (death_respawn_timer <= 0.0f) respawn_player();
    }
}

void GameEngine::respawn_player()
{
    if (!world) return;

    // Same fixed point set_world() itself spawns a brand-new session at -
    // this project has no bed/respawn-anchor system, so death always
    // returns here.
    Vector3 spawn = world->find_spawn_position();
    spawn.y += PlayerController::EYE_HEIGHT;
    Vector3 shift = Vector3Subtract(spawn, camera.position);
    camera.position = spawn;
    camera.target = Vector3Add(camera.target, shift);

    player_controller.reset();
    player_health.reset();
    reset_life_timers();
    spawn_settle_frames = 3; // same rotation-jump guard set_world() itself uses right after a teleport
}

void GameEngine::reset_life_timers()
{
    fire_seconds_remaining = 0.0f;
    fire_damage_timer = 0.0f;
    air_seconds = MAX_AIR_SECONDS;
    drown_damage_timer = 0.0f;
    suffocation_damage_timer = 0.0f;
    hurt_flash_seconds = 0.0f;
    death_respawn_timer = 0.0f;
    was_dead_last_frame = false;
}

Camera3D GameEngine::make_render_camera() const
{
    if (camera_view == CameraView::FirstPerson) return camera;

    Camera3D result = camera;
    Vector3 look = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 direction = camera_view == CameraView::ThirdPersonBack
        ? Vector3Negate(look) : look;
    constexpr float distance = 4.0f;
    float allowed_distance = distance;
    if (world) {
        if (auto hit = world->raycast(camera.position, direction, distance)) {
            allowed_distance = std::max(0.25f, hit->distance - 0.2f);
        }
    }
    result.position = Vector3Add(camera.position, Vector3Scale(direction, allowed_distance));
    Vector3 render_look = camera_view == CameraView::ThirdPersonBack ? look : Vector3Negate(look);
    result.target = Vector3Add(result.position, Vector3Scale(render_look, 10.0f));
    return result;
}

void GameEngine::draw_player_model() const
{
    if (camera_view == CameraView::FirstPerson) return;
    Vector3 feet = player_controller.feet_position(camera);
    Vector3 look = Vector3Subtract(camera.target, camera.position);
    if (world) player_renderer.draw(feet, look, *world);
}

void GameEngine::update(float delta_time)
{
    // Mouse wheel adjusting walking speed - commented out for now (left
    // over from when this was a free-fly camera with no gravity); walking
    // speed stays fixed at CAMERA_MOVE_SPEED_DEFAULT instead. Uncomment to
    // bring it back.
    // float wheel_move = GetMouseWheelMove();
    // if (wheel_move != 0.0f) {
    //     camera_move_speed = Clamp(camera_move_speed + wheel_move * CAMERA_MOVE_SPEED_SCROLL_STEP,
    //                                CAMERA_MOVE_SPEED_MIN, CAMERA_MOVE_SPEED_MAX);
    // }

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
        // Mouse wheel also cycles the selected hotbar slot, same "scroll
        // up/away subtracts" convention as every other scrollable list in
        // this project (WorldListScreen, SettingsScreen's Controls grid,
        // InventoryHud's own creative-page scroll) - and wraps around at
        // either end instead of clamping, same as vanilla's hotbar.
        int wheel_steps = static_cast<int>(std::round(GetMouseWheelMove()));
        if (wheel_steps != 0) {
            inventory.selected_slot = ((inventory.selected_slot - wheel_steps) % HOTBAR_SIZE + HOTBAR_SIZE) % HOTBAR_SIZE;
        }
        // Q: throw one item out of the selected hotbar slot. While the
        // inventory screen is open instead, the equivalent (Q over a
        // hovered slot) is handled inside draw()'s own
        // inventory_hud.update_grid() call - it needs to know which slot
        // the mouse is over, which only that call already tracks.
        if (world && IsKeyPressed(KEY_Q)) {
            ItemStack& selected = inventory.hotbar[inventory.selected_slot];
            if (current_game_mode == GameMode::Creative) {
                ItemStack copy = selected;
                if (!copy.empty() && !IsKeyDown(KEY_LEFT_SHIFT)) copy.count = 1;
                spawn_dropped_item(copy);
            } else if (IsKeyDown(KEY_LEFT_SHIFT)) {
                spawn_dropped_item(selected);
                selected.clear();
            } else {
                spawn_dropped_item(take_one_item(selected));
            }
        }
    }

    if (IsKeyPressed(KEY_F3)) {
        show_debug_overlay = !show_debug_overlay;
    }
    if (IsKeyPressed(KEY_F4)) {
        show_chunk_borders = !show_chunk_borders;
    }
    if (IsKeyPressed(KEY_F5)) {
        camera_view = camera_view == CameraView::FirstPerson ? CameraView::ThirdPersonBack
                    : camera_view == CameraView::ThirdPersonBack ? CameraView::ThirdPersonFront
                    : CameraView::FirstPerson;
    }
    if (IsKeyPressed(KEY_F6)) show_wireframe = !show_wireframe;

    update_dropped_items(delta_time);
    update_leaf_decay(delta_time);
    update_sapling_growth(delta_time);
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
    Vector2 mouse_delta = GetMouseDelta();
    Vector3 rotation = {mouse_delta.x * CAMERA_MOUSE_SENSITIVITY, mouse_delta.y * CAMERA_MOUSE_SENSITIVITY, 0.0f};
    if (spawn_settle_frames > 0) {
        // See spawn_settle_frames's own comment: this delta might still be
        // a spurious startup jump, not real player input.
        rotation = {0.0f, 0.0f, 0.0f};
        --spawn_settle_frames;
    }

    Vector3 previous_camera_position = camera.position;
    const bool was_grounded = player_controller.is_grounded();
    UpdateCameraPro(&camera, {0.0f, 0.0f, 0.0f}, rotation, 0.0f);
    // Dead: same "no longer takes input" freeze real Minecraft's own death
    // screen imposes, just without the screen itself - see
    // update_player_damage()'s automatic respawn_player() a couple seconds
    // later. Physics (gravity, whatever residual velocity was left) still
    // runs so the body doesn't hang frozen mid-air.
    bool alive = !player_health.is_dead();
    if (world) {
        PlayerInput input;
        if (alive) {
            input.forward = (is_action_down(GameAction::MoveForward) ? 1.0f : 0.0f) -
                            (is_action_down(GameAction::MoveBackward) ? 1.0f : 0.0f);
            input.right = (is_action_down(GameAction::MoveRight) ? 1.0f : 0.0f) -
                          (is_action_down(GameAction::MoveLeft) ? 1.0f : 0.0f);
            input.jump = is_action_down(GameAction::Jump);
            input.sneak = is_action_down(GameAction::Sneak);
            input.sprint = is_action_down(GameAction::Sprint);
        }
        player_controller.update(camera, *world, current_game_mode, input, delta_time);
        const Vector3 travelled = Vector3Subtract(camera.position, previous_camera_position);
        audio.update_water(delta_time, player_controller.is_in_water(),
                           Vector3LengthSqr(travelled) > 0.000025f);
        if (current_game_mode == GameMode::Survival) update_player_damage(delta_time);
    }

    // Emit by travelled distance, and only near a solid top surface. This
    // keeps the cadence frame-rate independent and prevents dust in flight.
    if (world) {
        float feet_y = camera.position.y - PlayerController::EYE_HEIGHT;
        int ground_x = static_cast<int>(std::floor(camera.position.x));
        int ground_y = static_cast<int>(std::floor(feet_y - 0.06f));
        int ground_z = static_cast<int>(std::floor(camera.position.z));
        BlockType ground_type = world->get_block(ground_x, ground_y, ground_z);
        bool grounded = player_controller.is_grounded() && get_block_properties(ground_type).solid &&
                        std::fabs(feet_y - (ground_y + 1.0f)) <= 0.22f;
        float dx = camera.position.x - previous_camera_position.x;
        float dz = camera.position.z - previous_camera_position.z;
        float horizontal_distance = std::sqrt(dx * dx + dz * dz);
        constexpr float FOOTSTEP_DISTANCE = 1.2f;
        if (grounded && horizontal_distance > 0.0001f) {
            footstep_particle_distance += horizontal_distance;
            int emitted = 0;
            while (footstep_particle_distance >= FOOTSTEP_DISTANCE && emitted < 2) {
                particles.spawn_footstep(ground_type,
                    Vector3{camera.position.x, ground_y + 1.0f, camera.position.z});
                audio.play_step(ground_type,
                    Vector3{camera.position.x, ground_y + 1.0f, camera.position.z}, camera.position);
                footstep_particle_distance -= FOOTSTEP_DISTANCE;
                ++emitted;
            }
        } else if (!grounded) {
            footstep_particle_distance = 0.0f;
        }

        // Jumping and landing are contact events of their own. They must not
        // depend on horizontal distance, otherwise a straight jump/fall is
        // silent even though the feet leave or strike a real block.
        if (was_grounded && !player_controller.is_grounded()) {
            int old_ground_y = static_cast<int>(std::floor(
                previous_camera_position.y - PlayerController::EYE_HEIGHT - 0.06f));
            BlockType old_ground = world->get_block(
                static_cast<int>(std::floor(previous_camera_position.x)), old_ground_y,
                static_cast<int>(std::floor(previous_camera_position.z)));
            if (get_block_properties(old_ground).solid) {
                audio.play_step(old_ground,
                    {previous_camera_position.x, old_ground_y + 1.0f, previous_camera_position.z},
                    camera.position);
            }
        } else if (!was_grounded && player_controller.is_grounded() &&
                   get_block_properties(ground_type).solid) {
            audio.play_step(ground_type,
                {camera.position.x, ground_y + 1.0f, camera.position.z}, camera.position);
            footstep_particle_distance = 0.0f;
        }
    }

    // camera.target isn't a unit vector (it's an arbitrary point ahead of
    // the camera), so the aim direction needs normalizing before it's used
    // as a ray direction.
    Vector3 aim = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    if (!alive) {
        // Dead: no aiming, no breaking/placing - see the input freeze above.
        targeted_block = std::nullopt;
        is_breaking = false;
        breaking_progress = 0.0f;
    } else {

    // Recomputed every frame (not just on click) so draw() can outline
    // whatever's targeted, out to the longer of the two reaches (place's)
    // so the outline still shows a block that's placeable but too far to
    // break.
    float interaction_reach = current_game_mode == GameMode::Creative ? CREATIVE_REACH : SURVIVAL_REACH;
    targeted_block = world ? world->raycast(camera.position, aim, interaction_reach) : std::nullopt;

    // Left click breaks whatever's aimed at, held down over time rather
    // than instantly - how long depends on the block and, if it's the
    // right kind for the job, the selected tool (break_seconds_required()
    // above). Right click places one block against the face the crosshair
    // is aimed at (the cell just outside the targeted block, in the
    // direction of the hit face's own outward normal), within the longer
    // PLACE_REACH - or opens a container if the targeted block is one.
    bool creative_break = world && current_game_mode == GameMode::Creative &&
        binding_pressed(settings.keybindings[static_cast<size_t>(GameAction::BreakBlock)]);
    if (creative_break) {
        if (auto hit = world->raycast(camera.position, aim, CREATIVE_REACH)) {
            if (std::optional<BlockType> broken = world->break_block(hit->x, hit->y, hit->z)) {
                Vector3 center = {hit->x + 0.5f, hit->y + 0.5f, hit->z + 0.5f};
                particles.spawn_hit(*broken, Vector3Add(center, Vector3Scale(hit->normal, 0.505f)), hit->normal);
                particles.spawn_destroy(*broken, center);
                audio.play_break(*broken, center, camera.position);
                if (is_log_block(*broken)) check_leaf_decay_near(hit->x, hit->y, hit->z);
                check_grass_support_above(hit->x, hit->y, hit->z);
                if (*broken == BlockType::Chest) spill_chest_if_any(hit->x, hit->y, hit->z);
            }
        }
    }

    bool break_held = world && current_game_mode == GameMode::Survival &&
        binding_down(settings.keybindings[static_cast<size_t>(GameAction::BreakBlock)]);
    if (!break_held) {
        is_breaking = false;
        breaking_progress = 0.0f;
    }

    if (break_held) {
        auto hit = world->raycast(camera.position, aim, SURVIVAL_REACH);
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
                std::optional<Color> dropped_block_tint;
                if (target_type == BlockType::Foliage) {
                    dropped_block_tint = world->get_foliage_tint(breaking_x, breaking_z);
                } else if (target_type == BlockType::ShortGrass) {
                    dropped_block_tint = world->get_grass_tint(breaking_x, breaking_z);
                }
                if (std::optional<BlockType> broken = world->break_block(breaking_x, breaking_y, breaking_z)) {
                    Vector3 center = {breaking_x + 0.5f, breaking_y + 0.5f, breaking_z + 0.5f};
                    particles.spawn_hit(*broken, Vector3Add(center, Vector3Scale(hit->normal, 0.505f)), hit->normal);
                    particles.spawn_destroy(*broken, center);
                    audio.play_break(*broken, center, camera.position);

                    // What actually comes off this block - not necessarily
                    // itself (Stone -> Cobblestone), not necessarily
                    // anything at all (wrong/no tool against an ore) - see
                    // resolve_block_drops()/assets/drops.json. A block can
                    // yield more than one stack (Gravel's own Flint roll
                    // alongside Gravel itself), so this can push 0..N
                    // dropped items, not just the old always-exactly-1.
                    for (const DropRoll& drop : resolve_block_drops(*broken, selected)) {
                        ItemStack drop_stack;
                        if (drop.is_item) {
                            drop_stack.tool = drop.item;
                        } else {
                            drop_stack.block = drop.block;
                        }
                        drop_stack.count = drop.count;
                        dropped_items.push_back(std::make_unique<DroppedItem>(
                            center, drop_stack, break_launch_velocity(hit->normal),
                            DroppedItemOrigin::Natural,
                            (!drop.is_item && drop.block == *broken) ? dropped_block_tint : std::nullopt));
                    }

                    // Whatever tool broke it loses 1 durability, whether or
                    // not it was actually the right kind for a speed bonus
                    // - same as real Minecraft.
                    if (selected.is_tool() && --selected.durability <= 0) {
                        selected.clear();
                    }
                    if (is_log_block(*broken)) check_leaf_decay_near(breaking_x, breaking_y, breaking_z);
                    check_grass_support_above(breaking_x, breaking_y, breaking_z);
                    if (*broken == BlockType::Chest) spill_chest_if_any(breaking_x, breaking_y, breaking_z);
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
        // A Chest needs its lid to actually swing open - a solid block
        // sitting directly on top blocks that, same as real Minecraft (it
        // still can't be opened even though right-clicking it doesn't do
        // anything else either, so this just leaves the click a no-op
        // rather than falling through to block placement).
        bool chest_blocked_above = container_kind && *container_kind == InventoryHud::ContainerKind::Chest &&
            targeted_block && get_block_properties(world->get_block(
                targeted_block->x, targeted_block->y + 1, targeted_block->z)).solid;
        if (container_kind && !chest_blocked_above) {
            inventory_hud.open_container(*container_kind, targeted_block->x, targeted_block->y, targeted_block->z);
            EnableCursor();
        } else if (!container_kind) {
            ItemStack& selected = inventory.hotbar[inventory.selected_slot];

            // Eating: right-click with a food item selected (heal_amount >
            // 0 - see ItemProperties/Item.cpp's define_food() calls),
            // Survival only (Creative players have no need for it, same as
            // they never take damage either) and only below full health -
            // there's no hunger bar here to gate this on the way vanilla
            // does (see PlayerHealth's own comment), so full health simply
            // stands in for "not hungry". Doesn't need targeted_block at
            // all - you can eat looking at open sky, same as vanilla.
            const ItemProperties* held = selected.holds_item() ? &get_item_properties(selected.tool) : nullptr;
            if (held && held->heal_amount > 0) {
                if (current_game_mode == GameMode::Survival && player_health.health() < PlayerHealth::MAX_HEALTH) {
                    player_health.heal(held->heal_amount);
                    if (--selected.count <= 0) selected.clear();
                }
            } else if (targeted_block && !selected.empty() && !selected.holds_item()) {
                // Right-click-to-place only ever consumes a block stack -
                // a selected tool has nothing to place (and isn't consumed
                // by right-clicking with it either, same as vanilla: tools
                // have no use-on-block action here yet beyond mining); a
                // selected Material likewise has nothing to place (and
                // critically must stay excluded here - selected.block
                // reads as Air for one, so without this check place_block
                // would be called with Air and silently clear out whatever
                // was targeted).
                BlockType targeted_type = world->get_block(
                    targeted_block->x, targeted_block->y, targeted_block->z);
                bool replace_target = get_block_properties(targeted_type).replaceable;
                int place_x = targeted_block->x + (replace_target ? 0 : static_cast<int>(targeted_block->normal.x));
                int place_y = targeted_block->y + (replace_target ? 0 : static_cast<int>(targeted_block->normal.y));
                int place_z = targeted_block->z + (replace_target ? 0 : static_cast<int>(targeted_block->normal.z));
                if (!player_controller.intersects_block(camera, place_x, place_y, place_z) &&
                    world->place_block(place_x, place_y, place_z, selected.block)) {
                    if (block_is_directional(selected.block)) {
                        world->set_block_orientation(place_x, place_y, place_z, direction_facing_player(aim));
                    }
                    if (selected.block == BlockType::OakSapling) queue_sapling_growth(place_x, place_y, place_z);
                    if (current_game_mode == GameMode::Survival && --selected.count <= 0) selected.clear();
                }
            }
        }
    }

    } // alive

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

    Camera3D render_camera = make_render_camera();
    BeginMode3D(render_camera);
    draw_skybox(render_camera.position);
    if (world) {
        // Wireframe ("skeleton") debug view: draws the exact same chunk
        // meshes, just as GL_LINE edges instead of filled/textured
        // triangles - every block's own face boundaries end up visible,
        // which is what actually reveals block positions/mesh structure,
        // rather than a separate position-label overlay.
        if (show_wireframe) rlEnableWireMode();
        world->draw_opaque(render_camera);
        world->draw_falling_blocks(tick_alpha);
        if (show_chunk_borders) {
            world->draw_chunk_borders();
        }
    }
    if (world) begin_dynamic_entity_shader();
    for (auto& object : objects) {
        if (object->is_active()) {
            object->draw();
        }
    }
    for (const auto& item : dropped_items) {
        if (item->is_active() && world) item->render(tick_alpha, render_camera.position, *world);
    }
    draw_player_model();
    particles.draw(render_camera, world.get());
    if (world) end_dynamic_entity_shader();
    if (world) {
        // Water/glass/ice, drawn only now - after every opaque and solid-
        // entity thing above - so its own alpha blending correctly
        // composites over whatever's actually underwater (a dropped item,
        // say) instead of always rendering in front of it regardless of
        // real depth.
        world->draw_translucent(render_camera);
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

    Color haze_color = skybox_horizon_color();
    haze_color.a = CAMERA_HAZE_ALPHA;
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), haze_color);

    if (pause_snapshot_pending) {
        if (IsTextureValid(pause_snapshot)) UnloadTexture(pause_snapshot);
        pause_snapshot = ui::capture_blurred_background();
        pause_snapshot_pending = false;
    }

    // Hurt flash: a brief red pulse whenever apply_damage() actually lands
    // a hit - the only feedback taking damage gets right now (no hurt
    // sound/hit animation asset exists in this project yet). Faded by
    // update_player_damage() counting hurt_flash_seconds back down to 0.
    if (hurt_flash_seconds > 0.0f) {
        unsigned char alpha = static_cast<unsigned char>(
            90.0f * std::clamp(hurt_flash_seconds / HURT_FLASH_SECONDS, 0.0f, 1.0f));
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{200, 0, 0, alpha});
    }

    bool show_death_screen = world && player_health.is_dead();
    if (!show_death_screen) ui::crosshair();

    if (is_breaking) {
        const float bar_width = ui::scaled(BREAK_BAR_WIDTH);
        const float bar_height = ui::scaled(BREAK_BAR_HEIGHT);
        float bar_x = GetScreenWidth() / 2.0f - bar_width / 2.0f;
        float bar_y = GetScreenHeight() / 2.0f + ui::scaled(BREAK_BAR_OFFSET_Y);
        float fill_width = bar_width * std::clamp(breaking_progress, 0.0f, 1.0f);
        DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                      static_cast<int>(bar_width), static_cast<int>(bar_height), BREAK_BAR_BACKGROUND);
        DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                      static_cast<int>(fill_width), static_cast<int>(bar_height), BREAK_BAR_FILL);
    }

    if (world) {
        inventory_hud.draw_hotbar(inventory);
        // Creative hides its own health/hunger bars in real Minecraft too -
        // Creative players are invulnerable, so there's nothing meaningful
        // to show (player_health simply never leaves full health there).
        if (current_game_mode == GameMode::Survival) {
            inventory_hud.draw_hearts(player_health.health(), PlayerHealth::MAX_HEALTH);
        }
        // Drawn and click-handled together here (not from update()) - the
        // same immediate-mode pattern every menu screen already uses, and
        // simplest since update() already returned early while it's open.
        if (inventory_hud.is_open()) {
            if (std::optional<ItemStack> dropped = inventory_hud.update_grid(inventory, current_game_mode, world.get())) {
                spawn_dropped_item(*dropped);
            }
        }
    }

    if (show_debug_overlay && world) {
        ui::draw_debug_overlay(camera, *world,
            current_game_mode == GameMode::Creative ? CREATIVE_REACH : SURVIVAL_REACH,
            current_game_mode == GameMode::Creative ? camera_move_speed : player_controller.horizontal_speed(), game_tick);
    }

    // Death screen: no click-to-respawn button here (see respawn_player()'s
    // own comment) - just the same dark-red overlay/title real Minecraft
    // shows while its own timer runs out, drawn over everything else
    // (hotbar included) the way its death screen does too.
    if (show_death_screen) {
        ui::panel({0.0f, 0.0f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())},
                  Color{110, 0, 0, 140});
        ui::label({0.0f, GetScreenHeight() * 0.35f, static_cast<float>(GetScreenWidth()), ui::scaled(60.0f)},
                  ui::tr("death.title"), WHITE);
    }

    EndDrawing();
}

void GameEngine::run()
{
    while (!WindowShouldClose() && !quit_requested) {
        float delta_time = GetFrameTime();
        if (IsWindowResized()) {
            settings.window_width = GetScreenWidth();
            settings.window_height = GetScreenHeight();
            SettingsIO::save(settings);
        }
        audio.update(delta_time, settings, state == GameState::Playing);
        ui::set_scale_level(settings.ui_scale);
        ui::set_language(settings.language);
        ui::begin_frame();
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

    // Pause and its settings share the frozen world; pre-game screens use
    // the dark dirt pattern.
    const bool over_world = state == GameState::Paused ||
        (state == GameState::Settings && settings_return_state == GameState::Paused);
    if (over_world && IsTextureValid(pause_snapshot)) {
        DrawTexturePro(pause_snapshot,
            {0.0f, 0.0f, static_cast<float>(pause_snapshot.width), static_cast<float>(pause_snapshot.height)},
            {0.0f, 0.0f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())},
            {0.0f, 0.0f}, 0.0f, WHITE);
        if (state == GameState::Settings)
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 105});
    } else if (!over_world && state != GameState::Playing) {
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
    GameState previous_state = state;
    state = new_state;
    if (state == GameState::Settings) settings_screen.enter();
    if (state == GameState::Paused && previous_state == GameState::Playing) pause_snapshot_pending = true;
    if (state == GameState::Playing) {
        if (IsTextureValid(pause_snapshot)) {
            rlDrawRenderBatchActive();
            UnloadTexture(pause_snapshot);
            pause_snapshot = {};
        }
        pause_snapshot_pending = false;
        DisableCursor(); // mouse-look needs the cursor captured
    } else {
        if (state == GameState::MainMenu && IsTextureValid(pause_snapshot)) {
            rlDrawRenderBatchActive();
            UnloadTexture(pause_snapshot);
            pause_snapshot = {};
        }
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
    current_game_mode = info->game_mode;
    auto new_world = std::make_unique<World>(config);
    // Inventory belongs to a save, never to the GameEngine session. Without
    // this reset, entering a brand-new world after leaving another one
    // leaked the previous world's stacks into it.
    inventory = current_game_mode == GameMode::Creative ? creative_inventory() : default_inventory();

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
        player_controller.reset();
        player_health.reset();
        player_health.set_health(saved->health);
        reset_life_timers();
        camera_view = CameraView::FirstPerson;
        // Blocking: the world needs to actually be there around the
        // player's resumed position by the time Playing starts, not merely
        // dispatched - see World::update_chunk_states_blocking()'s own
        // comment.
        world->update_chunk_states_blocking(camera.position);

        // Whatever was still on the ground when this world was last saved
        // (see GameEngine::save_player_state()) - restored with its
        // despawn countdown already wherever it had gotten to, not a fresh
        // one, via DroppedItem::set_age().
        for (const DroppedItemSaveState& item : WorldSave::load_dropped_items(folder_name)) {
            auto dropped = std::make_unique<DroppedItem>(
                item.position, item.stack, Vector3{0.0f, 0.0f, 0.0f}, DroppedItemOrigin::Natural, item.block_tint);
            dropped->set_age(item.age);
            dropped_items.push_back(std::move(dropped));
        }

        // Whatever every chest had in it, last time this world was saved
        // (see GameEngine::save_player_state()) - chest_inventory() creates
        // the entry on first touch, so just writing straight into the
        // reference it returns is enough to restore it.
        for (const ChestSaveState& chest : WorldSave::load_chests(folder_name)) {
            world->chest_inventory(chest.x, chest.y, chest.z) = chest.slots;
        }
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
    state.health = player_health.health();
    WorldSave::save_player_state(current_world_folder, state);

    // Every item still on the ground, so it's there (and keeps counting
    // down toward despawn from where it left off - see DroppedItem::
    // get_age()/set_age()) next time this world is opened, instead of the
    // whole dropped_items list just being lost on exit.
    std::vector<DroppedItemSaveState> saved_items;
    saved_items.reserve(dropped_items.size());
    for (const auto& item : dropped_items) {
        if (!item->is_active()) continue;
        DroppedItemSaveState saved;
        saved.position = item->get_position();
        saved.stack = item->get_stack();
        saved.age = item->get_age();
        saved.block_tint = item->get_block_tint();
        saved_items.push_back(saved);
    }
    WorldSave::save_dropped_items(current_world_folder, saved_items);

    // Every chest's own storage (see World::all_chest_inventories()) -
    // save_chests() itself skips any chest with nothing actually in it, so
    // this doesn't need to filter those out first.
    std::vector<ChestSaveState> saved_chests;
    for (const World::ChestSnapshot& chest : world->all_chest_inventories()) {
        saved_chests.push_back({chest.x, chest.y, chest.z, chest.slots});
    }
    WorldSave::save_chests(current_world_folder, saved_chests);
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
