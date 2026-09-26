#include "core/GameEngine.hpp"
#include "core/Block.hpp"
#include "core/BlockShape.hpp"
#include "core/Tick.hpp"
#include "items/Item.hpp"
#include "items/DropTable.hpp"
#include "items/Recipe.hpp"
#include "items/Smelting.hpp"
#include "entities/Player.hpp"
#include "ui/Widgets.hpp"
#include "ui/Localization.hpp"
#include "core/Keybindings.hpp"
#include "core/WorldSave.hpp"
#include "rendering/Skybox.hpp"
#include "rendering/EntityLighting.hpp"
#include "core/DayNightCycle.hpp"
#include "ui/DebugOverlay.hpp"
#include "worldgen/Structure.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <sstream>
#include <unordered_set>

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
    constexpr Color IN_GAME_MENU_OVERLAY = {0, 0, 0, 105};

    constexpr uint64_t SLEEP_ALLOWED_START_TICK = 12542; // roughly 18:30, vanilla's night-sleep threshold
    constexpr float SLEEP_RAMP_SECONDS         = 1.6f;
    constexpr float SLEEP_SLOWDOWN_TICKS       = 1800.0f;
    constexpr float SLEEP_MIN_TICKS_PER_SECOND = 80.0f;
    constexpr float SLEEP_MAX_TICKS_PER_SECOND = 1800.0f;
    constexpr int SLEEP_MAX_TICKS_PER_FRAME = 260;
    constexpr unsigned char SLEEP_OVERLAY_ALPHA = 170;
    constexpr unsigned char SLEEP_PANEL_ALPHA   = 175;

    // How much darker shadow gets at the brightness slider's own minimum
    // (settings.brightness == 10) - see set_chunk_brightness()'s own
    // comment for the full gamma-curve reasoning. Tune to taste; 1.0 at
    // the slider's max is always a no-op regardless of this constant.
    constexpr float BRIGHTNESS_GAMMA_RANGE = 2.0f;

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

    // HP restored per cake slice eaten - there's no hunger/saturation
    // system here (see PlayerHealth's own comment) for a bite to restore
    // the way vanilla's own cake does, so this substitutes a flat direct
    // heal instead. A placeholder balance number, not derived from any
    // existing precedent - adjust freely.
    constexpr int CAKE_HEAL_PER_BITE = 2;
    constexpr uint8_t CAKE_MAX_BITES = 6; // vanilla's own count - the 7th bite consumes the block

    constexpr float MAX_AIR_SECONDS = 15.0f; // vanilla's own breath meter length
    constexpr int DROWN_DAMAGE = 2;
    constexpr float DROWN_TICK_INTERVAL_SECONDS = 1.0f;

    constexpr int SUFFOCATION_DAMAGE = 1;
    constexpr float SUFFOCATION_TICK_INTERVAL_SECONDS = 1.0f;

    constexpr int CACTUS_DAMAGE = 1; // per-frame attempt - the player's health's own invulnerability window throttles this to ~2/second

    // How far below the world's own floor (MIN_WORLD_Y - see Chunk.hpp) a
    // fall counts as "into the void" - a safety net for however a player
    // might end up under the terrain (bedrock should normally prevent it
    // outright), not a feature meant to be reachable in ordinary play.
    constexpr float VOID_DAMAGE_Y = static_cast<float>(MIN_WORLD_Y - 4);
    constexpr int VOID_DAMAGE = 4; // same per-tick rate as lava - falling forever shouldn't take long to end

    constexpr float DEATH_RESPAWN_SECONDS = 2.0f; // real Minecraft's own death-screen delay, just without the screen/button
    constexpr float HURT_FLASH_SECONDS    = 0.3f;

    // Breaking with the wrong tool (or bare hands) still works, just much
    // slower below (the /100 divisor rather than /30 - see the formula's
    // own comment) rather than refusing outright - no hard block on the
    // *attempt*, since a lost/broken tool can now be recrafted (see
    // Recipe.hpp); resolve_block_drops()'s own tool gating (the exact same
    // can_harvest_block() check used here) is what actually withholds a
    // drop at the end of it for an ore mined with too weak a tool.
    //
    // Real Minecraft's own tick-quantized formula (breaking is simulated at
    // the game's 20 ticks/second, not a continuous real-number countdown) -
    // two independent conditions, not one:
    //   speed = (tool's category matches this block's effective_tool) ? that tool's own speed : 1
    //   divisor = can_harvest_block(type, selected) ? 30 : 100
    //   progress per tick = speed / hardness / divisor
    //   ticks required     = ceil(1 / progress per tick) = ceil(divisor * hardness / speed)
    // then converted to seconds at TICKS_PER_SECOND so breaking_progress's
    // own delta_time accumulation still lands on a whole-tick boundary.
    // The two conditions are independent, not the same check: a Log has no
    // requires_tool at all (can_harvest_block() is always true for it, so
    // hand-mining one still uses divisor 30 - no five-Log-lengths-slower
    // penalty), it's specifically Stone/ore's own hard pickaxe requirement
    // that triggers the /100 divisor when unmet. (e.g. Stone hand-mined:
    // speed=1, divisor=100 -> 100*1.5/1 = 150 ticks = 7.5s; Stone + Diamond
    // Pickaxe: speed=8, divisor=30 (a wood pickaxe already satisfies
    // Stone's own min tier) -> 30*1.5/8 = 5.625 -> 6 ticks -> 0.3s).
    float break_seconds_required(BlockType type, const ItemStack& selected) {
        const BlockProperties& block_properties = get_block_properties(type);
        float speed = 1.0f;
        if (selected.is_tool()) {
            const ItemProperties& tool_properties = get_item_properties(selected.tool);
            if (tool_properties.tool_kind == block_properties.effective_tool) {
                speed = tool_properties.mining_speed_multiplier;
            }
        }
        float divisor = can_harvest_block(type, selected) ? 30.0f : 100.0f;
        int ticks = std::max(1, static_cast<int>(std::ceil(divisor * block_properties.hardness / speed)));
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

    HorizontalDirection opposite_direction(HorizontalDirection direction)
    {
        switch (direction) {
            case HorizontalDirection::North: return HorizontalDirection::South;
            case HorizontalDirection::South: return HorizontalDirection::North;
            case HorizontalDirection::East:  return HorizontalDirection::West;
            case HorizontalDirection::West:  return HorizontalDirection::East;
        }
        return HorizontalDirection::South;
    }

    bool slab_click_adds_missing_half(const World& world, const World::RaycastHit& hit)
    {
        uint16_t packed = world.get_block_state(hit.x, hit.y, hit.z);
        bool existing_top_half = (packed & BlockStateBits::TOP_HALF) != 0;
        if (hit.normal.y > 0.5f) return !existing_top_half;
        if (hit.normal.y < -0.5f) return existing_top_half;

        float local_y = hit.hit_point.y - std::floor(hit.hit_point.y);
        return existing_top_half ? local_y < 0.5f : local_y >= 0.5f;
    }

    bool placement_intersects_player(const Player& player, BlockType block, int x, int y, int z,
                                     HorizontalDirection facing)
    {
        if (player.intersects_block(x, y, z)) return true;
        if (block == BlockType::OakDoorLower || block == BlockType::IronDoorLower) {
            return player.intersects_block(x, y + 1, z);
        }
        if (block == BlockType::BedHead) {
            DirectionOffset step = horizontal_direction_offset(facing);
            return player.intersects_block(x + step.dx, y, z + step.dz);
        }
        return false;
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

    float smoothstep01(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // A felled tree's leaves don't vanish in the same frame the log comes
    // down - each one gets its own random delay in this range before it's
    // actually removed, so the canopy visibly thins out over a couple of
    // seconds instead of blinking away all at once. In ticks (20/second),
    // not seconds - see PendingLeafDecay's own comment on why.
    constexpr int LEAF_DECAY_MIN_DELAY_TICKS = 1 * TICKS_PER_SECOND;
    constexpr int LEAF_DECAY_MAX_DELAY_TICKS = 4 * TICKS_PER_SECOND;

    // Halves the overall decay rate: once an eligible leaf's delay above
    // elapses, it only actually decays this fraction of the time (1/2) -
    // the other half, update_leaf_decay() just re-rolls a fresh delay and
    // checks again later instead of removing it. Expected checks before it
    // finally goes is 1 / (1/LEAF_DECAY_CHANCE_DENOMINATOR) = 2, doubling
    // the average total wait without touching the delay range above.
    constexpr int LEAF_DECAY_CHANCE_DENOMINATOR = 2;

    // --- Chat command parsing helpers (see GameEngine::execute_chat_command) ---

    std::vector<std::string> split_whitespace(const std::string& text) {
        std::vector<std::string> tokens;
        std::istringstream stream(text);
        std::string token;
        while (stream >> token) tokens.push_back(token);
        return tokens;
    }

    // Whole-string parse (not just a leading prefix) - "12abc" must fail,
    // not silently read as 12, the same std::stoi/std::stof would let
    // through if the trailing-character check below didn't reject it.
    std::optional<int> parse_int(const std::string& text) {
        try {
            size_t consumed = 0;
            int value = std::stoi(text, &consumed);
            if (consumed != text.size()) return std::nullopt;
            return value;
        } catch (...) { return std::nullopt; }
    }
    std::optional<float> parse_float(const std::string& text) {
        try {
            size_t consumed = 0;
            float value = std::stof(text, &consumed);
            if (consumed != text.size()) return std::nullopt;
            return value;
        } catch (...) { return std::nullopt; }
    }

    // Same cap real Minecraft's own /fill refuses past ("too many blocks in
    // the specified area") - without one, a careless /fill spanning
    // thousands of blocks on a side would stall a frame for a very long
    // time (each cell re-lights/remeshes its whole chunk neighborhood).
    constexpr long long COMMAND_VOLUME_LIMIT = 32768;

    // Commands real Minecraft has that this project deliberately doesn't
    // implement yet - each names the missing underlying system (mobs/
    // entities, enchanting, hunger/XP, gamerules, weather/difficulty,
    // multiplayer accounts) rather than silently no-op-ing or pretending.
    const std::unordered_set<std::string> UNSUPPORTED_COMMANDS = {
        "enchant", "effect", "xp", "gamerule", "weather", "difficulty",
        "summon", "spawnpoint", "kick", "op", "execute",
    };

    // Translation key naming what's missing for an UNSUPPORTED_COMMANDS entry.
    std::string unsupported_command_reason_key(const std::string& command) {
        if (command == "kick" || command == "op") return "command.unsupported.multiplayer";
        if (UNSUPPORTED_COMMANDS.count(command)) return "command.unsupported." + command;
        return "command.unsupported.other";
    }

    // /help output, one translation key per line - also the chat's
    // command suggestions (every line after the title starts with its own
    // "/command").
    const char* CHAT_HELP_KEYS[] = {
        "command.help.title",
        "command.help.tp",
        "command.help.give",
        "command.help.clear",
        "command.help.gamemode",
        "command.help.time",
        "command.help.setworldspawn",
        "command.help.setblock",
        "command.help.fill",
        "command.help.clone",
        "command.help.kill",
        "command.help.say",
    };

    std::vector<std::string> chat_command_suggestion_keys() {
        return std::vector<std::string>(std::begin(CHAT_HELP_KEYS) + 1, std::end(CHAT_HELP_KEYS));
    }
}

GameEngine::GameEngine(const Settings& settings, AudioSystem& audio)
    : settings(settings), audio(audio), camera_move_speed(CAMERA_MOVE_SPEED_DEFAULT)
{
    Load_block_definitions(); // needs a GL context, so only after InitWindow
    Load_item_definitions();
    Load_drop_table(); // needs both name tables above ready to resolve against
    Load_recipes();
    Load_smelting(); // same name tables as recipes
    chat_hud.set_command_suggestions(chat_command_suggestion_keys());
    SetTextureFilter(get_block_atlas_texture(),
                      settings.texture_filter == TextureFilterMode::Bilinear ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    load_chunk_shader(); // same reason

    // position/target are placeholders until set_world() actually has a
    // World to find real ground in - everything else here doesn't depend
    // on one.
    camera.position   = {0.0f, 100.0f, 0.0f};
    camera.target     = {0.0f, 100.0f, -1.0f};
    camera.up         = {0.0f, 1.0f, 0.0f};
    camera.fovy       = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
}

GameEngine::~GameEngine()
{
    // Covers quitting the app outright while a World is loaded (the OS
    // window-close control, or force-quit) - close_world() covers the
    // other exit path (the pause menu), but this one has no earlier hook
    // to call it from.
    if (inventory_hud.is_open()) inventory_hud.close(inventory);
    save_player_state();
    world.reset(); // while the GL context still exists - ~World() frees chunk meshes

    if (IsTextureValid(pause_snapshot)) UnloadTexture(pause_snapshot);
    unload_chunk_fog_shader();
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
        camera.position.y += Player::EYE_HEIGHT;
        // North: -Z in this engine's convention (see Chunk.cpp's
        // CUBE_FACES comment). Level, not angled down - the old downward
        // tilt was there to see a bird's-eye view from high above the
        // world; standing on real ground, a level look is the natural one.
        camera.target = {camera.position.x, camera.position.y, camera.position.z - 10.0f};
        spawn_settle_frames = 3; // see its own comment - 2 measured, +1 margin
        player.reset(camera);
        player.health().reset();
        reset_life_timers();
        camera_view = CameraView::FirstPerson;
        // A brand-new world (or a previous world's leftover value, if this
        // isn't the app's first one this session) starts fresh at dawn -
        // open_world()'s own saved-state branch overrides
        // this with whatever was actually persisted, for a world that's
        // been played before.
        game_tick = 0;
    }
}

void GameEngine::tick()
{
    ++game_tick;
    // Every piece of world simulation lives off this clock now - chunk
    // streaming, fluids, falling blocks, dropped-item physics, leaf decay,
    // random ticks (sapling growth today), and DayNightCycle's own sun/moon
    // angle (see draw()'s call into DayNightCycle::sun_direction(game_tick)) -
    // this is the ONLY place game_tick is ever incremented, exactly once per
    // call, so its rate is entirely governed by how often run()'s fixed-
    // timestep accumulator calls tick() (nominally 20/second - see
    // Tick.hpp), never by delta_time directly. Called unconditionally from
    // run() regardless of what update() is doing this frame (including a UI
    // screen owning input - see update()'s own ui_captured), so none of
    // this ever actually pauses.
    if (world) {
        // Picks up a render/fog distance change made from the pause menu's
        // Settings screen immediately, rather than only the next time a
        // world is started (see World::set_view_distance's own comment) -
        // a no-op most ticks, when neither value actually changed since.
        world->set_view_distance(settings.render_distance_chunks, settings.fog_distance_blocks);
        world->update_chunk_states(camera.position);
        world->update_fluids();
        world->update_falling_blocks();
        world->update_furnaces();
    }
    tick_dropped_items();
    update_leaf_decay();
    update_random_ticks();
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
        Vector3 pickup_point = player.closest_hitbox_point(item->get_position());
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

                int delay_ticks = GetRandomValue(LEAF_DECAY_MIN_DELAY_TICKS, LEAF_DECAY_MAX_DELAY_TICKS);
                pending_leaf_decay.push_back({x, y, z, delay_ticks});
            }
        }
    }
}

void GameEngine::update_leaf_decay()
{
    if (!world) { pending_leaf_decay.clear(); return; }

    for (size_t i = 0; i < pending_leaf_decay.size();) {
        --pending_leaf_decay[i].remaining_ticks;
        if (pending_leaf_decay[i].remaining_ticks > 0) { ++i; continue; }

        PendingLeafDecay entry = pending_leaf_decay[i];
        pending_leaf_decay[i] = pending_leaf_decay.back();
        pending_leaf_decay.pop_back();

        // Re-validate - a log could have been placed back nearby, or this
        // leaf could already be gone another way, since it was queued.
        if (!is_leaf_block(world->get_block(entry.x, entry.y, entry.z))) continue;
        if (has_nearby_log(*world, entry.x, entry.y, entry.z)) continue;

        // Half the time, this eligible leaf doesn't decay just yet - it
        // re-rolls a fresh delay and gets re-checked later instead (see
        // LEAF_DECAY_CHANCE_DENOMINATOR's own comment), roughly doubling
        // the average time a disconnected canopy takes to fully clear.
        if (GetRandomValue(0, LEAF_DECAY_CHANCE_DENOMINATOR - 1) != 0) {
            int delay_ticks = GetRandomValue(LEAF_DECAY_MIN_DELAY_TICKS, LEAF_DECAY_MAX_DELAY_TICKS);
            pending_leaf_decay.push_back({entry.x, entry.y, entry.z, delay_ticks});
            continue;
        }

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
    // Real Minecraft's own random-tick rate: this many random block
    // positions get checked per loaded chunk, per game tick - not every
    // block every tick (a chunk holds tens of thousands of them), which is
    // exactly why a lone sapling can sit for minutes before the dispatcher
    // happens to land on it.
    constexpr int RANDOM_TICK_SPEED = 3;

    // 1-in-7 chance a sapling turns into a tree the random tick that
    // actually lands on it - real Minecraft's own sapling growth odds.
    constexpr int SAPLING_GROWTH_CHANCE_DENOMINATOR = 7;

    constexpr int SAPLING_TRUNK_HEIGHT_MIN = 4;
    constexpr int SAPLING_TRUNK_HEIGHT_MAX = 6;
}

void GameEngine::update_random_ticks()
{
    if (!world) return;

    for (const auto& [chunk_x, chunk_z] : world->loaded_chunk_coordinates()) {
        for (int i = 0; i < RANDOM_TICK_SPEED; ++i) {
            int local_x = GetRandomValue(0, CHUNK_SIZE - 1);
            int local_y = GetRandomValue(0, CHUNK_HEIGHT - 1);
            int local_z = GetRandomValue(0, CHUNK_SIZE - 1);
            int world_x = chunk_x * CHUNK_SIZE + local_x;
            int world_y = MIN_WORLD_Y + local_y;
            int world_z = chunk_z * CHUNK_SIZE + local_z;

            // Dispatch on whatever block actually happens to be at this
            // random position right now - a future random-tick block
            // (a crop, grass spread, ...) would add its own case here the
            // same way, rather than each growing its own separate scan.
            switch (world->get_block(world_x, world_y, world_z)) {
                case BlockType::OakSapling:
                    update_sapling_growth(world_x, world_y, world_z);
                    break;
                default:
                    break;
            }
        }
    }
}

void GameEngine::update_sapling_growth(int x, int y, int z)
{
    if (GetRandomValue(0, SAPLING_GROWTH_CHANCE_DENOMINATOR - 1) != 0) return;

    int trunk_height = GetRandomValue(SAPLING_TRUNK_HEIGHT_MIN, SAPLING_TRUNK_HEIGHT_MAX);
    for (int dy = 1; dy <= trunk_height; ++dy) {
        BlockType existing = world->get_block(x, y + dy, z);
        // Blocked (something built over the trunk's own column since it
        // was planted) - just give up silently. Unlike the old per-
        // sapling queue, there's no retry to schedule: this exact sapling
        // simply gets another independent 1-in-7 roll on some future
        // random tick for free, same as a blocked vanilla sapling does.
        if (existing != BlockType::Air && existing != BlockType::Foliage) return;
    }

    // Clear the sapling itself first (no drop/particles - it's turning
    // into the tree, not being destroyed) so the origin block (the bottom
    // trunk log, landing exactly on the sapling's own position) finds Air
    // like every other block placed below, instead of World::
    // place_structure_block() needing its own OakSapling special case.
    world->break_block(x, y, z);

    // Same template make_oak_tree()/StructureGenerator place at world-
    // generation time, just placed here one world-space block at a time
    // via World::place_structure_block() instead of Chunk::set_block() -
    // this runs at an arbitrary runtime position, not bounded to one
    // already-open Chunk the way generation is.
    Structure tree = make_oak_tree(trunk_height);
    for (const StructureBlock& block : tree.get_blocks()) {
        world->place_structure_block(x + block.x, y + block.y, z + block.z,
            block.type, block.replace_rule == StructureReplaceRule::AirOrFoliage);
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

void GameEngine::spill_furnace_if_any(int x, int y, int z)
{
    if (!world) return;
    FurnaceState furnace = world->take_furnace_state(x, y, z);
    Vector3 center = {x + 0.5f, y + 0.5f, z + 0.5f};
    for (const ItemStack& stack : {furnace.input, furnace.fuel, furnace.output}) {
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

void GameEngine::check_attachment_support_near(int x, int y, int z)
{
    if (!world) return;

    constexpr BlockFace FACES[6] = {
        BlockFace::Top, BlockFace::Bottom, BlockFace::North,
        BlockFace::South, BlockFace::East, BlockFace::West,
    };
    for (BlockFace face : FACES) {
        const FaceOffset step = block_face_offset(face);
        int tx = x + step.dx;
        int ty = y + step.dy;
        int tz = z + step.dz;
        if (!block_is_attachable(world->get_block(tx, ty, tz))) continue;
        if (world->attachment_has_support(tx, ty, tz)) continue;

        if (std::optional<BlockType> broken = world->break_block(tx, ty, tz)) {
            Vector3 center = {tx + 0.5f, ty + 0.5f, tz + 0.5f};
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
}

void GameEngine::apply_damage(int amount, DamageSource source)
{
    if (player.health().damage(amount, source)) {
        hurt_flash_seconds = HURT_FLASH_SECONDS;
    }
}

void GameEngine::update_player_damage(float delta_time)
{
    // Fall damage - Player reports this exactly once, the frame
    // its feet actually land, regardless of whether that lands inside this
    // function's own "already dead" early state below (apply_damage/
    // player.health().damage() themselves no-op once dead, so it's harmless
    // to still consume it here rather than leave it queued for a fall that
    // already happened).
    float landing_fall_distance = player.consume_landing_fall_distance();
    if (landing_fall_distance >= 0.0f) {
        int fall_damage = static_cast<int>(std::floor(landing_fall_distance)) - FALL_DAMAGE_SAFE_BLOCKS;
        if (fall_damage > 0) apply_damage(fall_damage, DamageSource::Fall);
    }

    // Lava: hurts every frame it's touched (the player's health's own 0.5s
    // invulnerability window is what actually paces this to real
    // Minecraft's own per-half-second lava tick), and always re-arms the
    // burn timer below to its full duration - a single instant of contact
    // still burns for the whole FIRE_DURATION_FROM_LAVA_SECONDS afterward,
    // same as vanilla.
    if (player.is_in_lava()) {
        apply_damage(LAVA_DAMAGE, DamageSource::Lava);
        fire_seconds_remaining = FIRE_DURATION_FROM_LAVA_SECONDS;
    }

    // Burning: water immediately extinguishes it (real Minecraft too),
    // otherwise it counts down on its own and hurts once per
    // FIRE_TICK_INTERVAL_SECONDS regardless of whether the player is still
    // anywhere near the lava that started it.
    if (player.is_in_water()) fire_seconds_remaining = 0.0f;
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
    // specifically is submerged (see Player::is_head_submerged())
    // and otherwise recovers - once it runs out, one hit every
    // DROWN_TICK_INTERVAL_SECONDS for as long as the head stays under.
    if (player.is_head_submerged()) {
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
    // position (see Player::is_suffocating()) - typically a
    // block placed where the player is standing.
    if (player.is_suffocating()) {
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
    if (player.is_touching_cactus()) {
        apply_damage(CACTUS_DAMAGE, DamageSource::Cactus);
    }

    // Void: a safety net for ending up below the world's own floor (see
    // VOID_DAMAGE_Y's own comment) - same throttling idea as lava/cactus.
    if (camera.position.y < VOID_DAMAGE_Y) {
        apply_damage(VOID_DAMAGE, DamageSource::Void);
    }

    player.health().update(delta_time);
    hurt_flash_seconds = std::max(0.0f, hurt_flash_seconds - delta_time);

    // Death/respawn: death_respawn_timer is armed exactly once, the frame
    // health first reaches 0 (was_dead_last_frame catches that edge so a
    // second frame of already being dead doesn't keep resetting the
    // countdown back to full).
    bool dead_now = player.health().is_dead();
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

    // Same fixed point set_world() itself spawns a brand-new session at,
    // unless "/setworldspawn" overrode it for this session - this project
    // has no bed/respawn-anchor system, so death always returns to one of
    // these two.
    Vector3 spawn = world_spawn_override.value_or(world->find_spawn_position());
    spawn.y += Player::EYE_HEIGHT;
    Vector3 shift = Vector3Subtract(spawn, camera.position);
    camera.position = spawn;
    camera.target = Vector3Add(camera.target, shift);

    player.reset(camera);
    player.health().reset();
    reset_life_timers();
    spawn_settle_frames = 3; // same rotation-jump guard set_world() itself uses right after a teleport
}

void GameEngine::start_sleeping(const World::RaycastHit& bed_hit)
{
    if (!world || sleeping || player.health().is_dead()) return;

    uint64_t tick_of_day = game_tick % DayNightCycle::DAY_LENGTH_TICKS;
    if (tick_of_day < SLEEP_ALLOWED_START_TICK) {
        chat_hud.push_message(ui::tr("sleep.only_night"));
        return;
    }

    sleeping = true;
    sleep_target_tick = (game_tick / DayNightCycle::DAY_LENGTH_TICKS + 1) * DayNightCycle::DAY_LENGTH_TICKS;
    sleep_elapsed_seconds = 0.0f;
    sleep_tick_rate = SLEEP_MIN_TICKS_PER_SECOND;
    sleep_tick_budget = 0.0f;
    sleep_return_position = camera.position;
    sleep_return_target = camera.target;
    sleep_return_up = camera.up;
    sleep_start_forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    int head_x = bed_hit.x;
    int head_y = bed_hit.y;
    int head_z = bed_hit.z;
    HorizontalDirection bed_facing = world->get_block_orientation(bed_hit.x, bed_hit.y, bed_hit.z);
    DirectionOffset head_step = horizontal_direction_offset(bed_facing);
    if (world->get_block(bed_hit.x, bed_hit.y, bed_hit.z) == BlockType::BedFoot) {
        head_x += head_step.dx;
        head_z += head_step.dz;
    }

    sleep_pose_position = {
        static_cast<float>(head_x) + 0.5f - static_cast<float>(head_step.dx) * 0.12f,
        static_cast<float>(head_y) + 0.72f,
        static_cast<float>(head_z) + 0.5f - static_cast<float>(head_step.dz) * 0.12f,
    };
    Vector3 head_forward = sleep_start_forward;
    head_forward.y = 0.0f;
    if (Vector3LengthSqr(head_forward) < 0.0001f) {
        head_forward = {
            static_cast<float>(head_step.dx),
            0.0f,
            static_cast<float>(head_step.dz),
        };
    }
    head_forward = Vector3Normalize(head_forward);
    sleep_pose_forward = Vector3Normalize(Vector3Add(
        Vector3Scale({0.0f, 1.0f, 0.0f}, 0.92f),
        Vector3Scale(head_forward, 0.24f)));
    sleep_pose_up = Vector3Normalize(Vector3Scale(head_forward, -1.0f));

    is_breaking = false;
    breaking_progress = 0.0f;
    targeted_block = std::nullopt;
    inventory_hud.close(inventory);
    chat_hud.close();
    EnableCursor();
}

void GameEngine::update_sleep_fast_forward(float delta_time)
{
    const float target_overlay = sleeping ? 1.0f : 0.0f;
    sleep_overlay += (target_overlay - sleep_overlay) * std::min(1.0f, delta_time * 3.0f);
    if (!sleeping) {
        if (sleep_overlay < 0.01f) sleep_overlay = 0.0f;
        return;
    }

    if (game_tick >= sleep_target_tick) {
        finish_sleeping();
        return;
    }

    sleep_elapsed_seconds += delta_time;
    float pose_t = smoothstep01(sleep_elapsed_seconds / SLEEP_RAMP_SECONDS);
    camera.position = Vector3Lerp(sleep_return_position, sleep_pose_position, pose_t);
    camera.target = Vector3Add(camera.position, Vector3Normalize(Vector3Lerp(sleep_start_forward, sleep_pose_forward, pose_t)));
    camera.up = Vector3Normalize(Vector3Lerp(sleep_return_up, sleep_pose_up, pose_t));

    uint64_t remaining_ticks = sleep_target_tick - game_tick;
    float ramp_in = smoothstep01(sleep_elapsed_seconds / SLEEP_RAMP_SECONDS);
    float ramp_out = smoothstep01(static_cast<float>(remaining_ticks) / SLEEP_SLOWDOWN_TICKS);
    float speed_factor = std::min(ramp_in, ramp_out);
    float target_rate = SLEEP_MIN_TICKS_PER_SECOND +
        (SLEEP_MAX_TICKS_PER_SECOND - SLEEP_MIN_TICKS_PER_SECOND) * speed_factor;
    sleep_tick_rate += (target_rate - sleep_tick_rate) * std::min(1.0f, delta_time * 4.0f);

    sleep_tick_budget += sleep_tick_rate * delta_time;
    int ticks_to_run = std::min(SLEEP_MAX_TICKS_PER_FRAME, static_cast<int>(sleep_tick_budget));
    ticks_to_run = std::min<int>(ticks_to_run, static_cast<int>(std::min<uint64_t>(remaining_ticks, SLEEP_MAX_TICKS_PER_FRAME)));
    if (ticks_to_run <= 0) return;

    sleep_tick_budget -= static_cast<float>(ticks_to_run);
    for (int i = 0; i < ticks_to_run; ++i) tick();
    if (game_tick >= sleep_target_tick) finish_sleeping();
}

void GameEngine::finish_sleeping()
{
    sleeping = false;
    sleep_target_tick = 0;
    sleep_elapsed_seconds = 0.0f;
    sleep_tick_rate = 0.0f;
    sleep_tick_budget = 0.0f;
    camera.position = sleep_return_position;
    camera.target = sleep_return_target;
    camera.up = sleep_return_up;
    DisableCursor();
    spawn_settle_frames = 2;
}

void GameEngine::leave_bed()
{
    finish_sleeping();
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
    sleeping = false;
    sleep_target_tick = 0;
    sleep_elapsed_seconds = 0.0f;
    sleep_tick_rate = 0.0f;
    sleep_tick_budget = 0.0f;
    sleep_overlay = 0.0f;
    sleep_return_position = {0.0f, 0.0f, 0.0f};
    sleep_return_target = {0.0f, 0.0f, -1.0f};
    sleep_return_up = {0.0f, 1.0f, 0.0f};
    sleep_start_forward = {0.0f, 0.0f, -1.0f};
    sleep_pose_position = {0.0f, 0.0f, 0.0f};
    sleep_pose_forward = {0.0f, 0.0f, -1.0f};
    sleep_pose_up = {0.0f, 1.0f, 0.0f};
}

void GameEngine::handle_chat_submit(const std::string& text)
{
    if (text.empty()) return;
    if (text[0] == '/') {
        execute_chat_command(text.substr(1));
    } else {
        // No other player exists yet to actually send this to - see
        // ChatHud's own comment on the local-echo/future-multiplayer split.
        chat_hud.push_message(ui::tr_format("chat.player_message", {ui::tr("chat.player_name"), text}));
    }
}

void GameEngine::execute_chat_command(const std::string& command)
{
    std::vector<std::string> tokens = split_whitespace(command);
    if (tokens.empty()) {
        chat_hud.push_message(ui::tr("command.empty"));
        return;
    }

    if (!current_world_allows_commands) {
        chat_hud.push_message(ui::tr("command.disabled"));
        return;
    }

    std::string name = tokens[0];
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());

    // The raw text after the first token, exactly as typed (not tokens
    // rejoined with single spaces) - only /say wants this.
    std::string rest;
    if (size_t space = command.find_first_of(" \t"); space != std::string::npos) {
        size_t start = command.find_first_not_of(" \t", space);
        if (start != std::string::npos) rest = command.substr(start);
    }

    // Every reply is a translation key, plus the values its "{0}", "{1}",
    // ... placeholders take - see ui::tr_format().
    auto push = [this](std::string_view key, std::initializer_list<std::string> args = {}) {
        chat_hud.push_message(ui::tr_format(key, args));
    };

    if (UNSUPPORTED_COMMANDS.count(name)) {
        push("command.unsupported", {name, ui::tr(unsupported_command_reason_key(name))});
        return;
    }

    if (name == "help" || name == "?") {
        for (const char* key : CHAT_HELP_KEYS) push(key);
        return;
    }

    if (name == "say") {
        if (rest.empty()) { push("command.say.usage"); return; }
        push("chat.server_message", {rest});
        return;
    }

    // Every command below actually touches the world/player - none of it
    // means anything without one loaded (chat can't even open without
    // `world` either, but a belt-and-suspenders check costs nothing).
    if (!world) {
        push("command.no_world");
        return;
    }

    if (name == "tp") {
        // Vanilla allows a leading target-selector token before the
        // coordinates ("/tp <player> <x> <y> <z>") - accepted and ignored
        // here (there's only ever the one player to move).
        size_t coord_index = args.size() == 4 ? 1 : 0;
        if (args.size() != 3 && args.size() != 4) {
            push("command.tp.usage");
            return;
        }
        std::optional<float> x = parse_float(args[coord_index]);
        std::optional<float> y = parse_float(args[coord_index + 1]);
        std::optional<float> z = parse_float(args[coord_index + 2]);
        if (!x || !y || !z) {
            push("command.error.coordinates");
            return;
        }
        Vector3 target = {*x, *y + Player::EYE_HEIGHT, *z};
        Vector3 shift = Vector3Subtract(target, camera.position);
        camera.position = target;
        camera.target = Vector3Add(camera.target, shift);
        player.reset(camera);
        push("command.tp.done");
        return;
    }

    if (name == "give") {
        if (args.empty()) { push("command.give.usage"); return; }
        int count = 1;
        if (args.size() >= 2) {
            std::optional<int> parsed = parse_int(args[1]);
            if (!parsed || *parsed <= 0) { push("command.give.bad_count"); return; }
            count = *parsed;
        }
        if (std::optional<BlockType> block = block_type_from_name(args[0])) {
            int leftover = inventory.add(*block, count);
            push(leftover > 0 ? "command.give.done_partial" : "command.give.done",
                 {ui::block_display_name(*block), std::to_string(count - leftover), std::to_string(leftover)});
        } else if (std::optional<ItemType> item = item_type_from_name(args[0])) {
            const ItemProperties& properties = get_item_properties(*item);
            if (properties.category == ItemCategory::Tool) {
                int given = 0;
                for (int i = 0; i < count; ++i) {
                    if (!inventory.add_tool(*item)) break;
                    ++given;
                }
                push(given < count ? "command.give.done_inventory_full" : "command.give.done",
                     {ui::item_display_name(*item), std::to_string(given)});
            } else {
                int leftover = inventory.add_item(*item, count);
                push(leftover > 0 ? "command.give.done_partial" : "command.give.done",
                     {ui::item_display_name(*item), std::to_string(count - leftover), std::to_string(leftover)});
            }
        } else {
            push("command.give.unknown", {args[0]});
        }
        return;
    }

    if (name == "clear") {
        for (ItemStack& stack : inventory.hotbar) stack.clear();
        for (ItemStack& stack : inventory.storage) stack.clear();
        push("command.clear.done");
        return;
    }

    if (name == "gamemode") {
        if (args.empty()) { push("command.gamemode.usage"); return; }
        if (args[0] == "survival") {
            current_game_mode = GameMode::Survival;
            current_world_info.game_mode = current_game_mode;
            WorldSave::save_world_info(current_world_info);
            push("command.gamemode.survival");
        } else if (args[0] == "creative") {
            current_game_mode = GameMode::Creative;
            current_world_info.game_mode = current_game_mode;
            WorldSave::save_world_info(current_world_info);
            push("command.gamemode.creative");
        } else if (args[0] == "adventure" || args[0] == "spectator") {
            push("command.gamemode.unsupported", {args[0]});
        } else {
            push("command.gamemode.unknown", {args[0]});
        }
        return;
    }

    if (name == "time") {
        if (args.size() < 2 || args[0] != "set") {
            push("command.time.usage");
            return;
        }
        uint64_t target_tick;
        if (args[1] == "day") target_tick = 1000;
        else if (args[1] == "noon") target_tick = 6000;
        else if (args[1] == "night") target_tick = 13000;
        else if (args[1] == "midnight") target_tick = 18000;
        else {
            std::optional<int> parsed = parse_int(args[1]);
            if (!parsed || *parsed < 0) { push("command.time.unknown", {args[1]}); return; }
            target_tick = static_cast<uint64_t>(*parsed) % DayNightCycle::DAY_LENGTH_TICKS;
        }
        uint64_t day = game_tick / DayNightCycle::DAY_LENGTH_TICKS;
        game_tick = day * DayNightCycle::DAY_LENGTH_TICKS + target_tick;
        push("command.time.done");
        return;
    }

    if (name == "setworldspawn") {
        Vector3 spawn;
        if (args.size() >= 3) {
            std::optional<float> x = parse_float(args[0]);
            std::optional<float> y = parse_float(args[1]);
            std::optional<float> z = parse_float(args[2]);
            if (!x || !y || !z) { push("command.error.coordinates"); return; }
            spawn = {*x, *y, *z};
        } else {
            spawn = player.feet_position();
        }
        world_spawn_override = spawn;
        push("command.setworldspawn.done");
        return;
    }

    if (name == "setblock") {
        if (args.size() < 4) { push("command.setblock.usage"); return; }
        std::optional<int> x = parse_int(args[0]);
        std::optional<int> y = parse_int(args[1]);
        std::optional<int> z = parse_int(args[2]);
        if (!x || !y || !z) { push("command.error.integer_coordinates"); return; }
        std::optional<BlockType> block = block_type_from_name(args[3]);
        if (!block) { push("command.error.unknown_block", {args[3]}); return; }
        if (world->command_fill_region(*x, *y, *z, *x, *y, *z, *block) > 0) push("command.setblock.done");
        else push("command.setblock.failed");
        return;
    }

    if (name == "fill") {
        if (args.size() < 7) { push("command.fill.usage"); return; }
        std::optional<int> coords[6];
        for (int i = 0; i < 6; ++i) coords[i] = parse_int(args[i]);
        if (std::any_of(std::begin(coords), std::end(coords), [](auto& c) { return !c.has_value(); })) {
            push("command.error.integer_coordinates");
            return;
        }
        std::optional<BlockType> block = block_type_from_name(args[6]);
        if (!block) { push("command.error.unknown_block", {args[6]}); return; }
        int min_x = std::min(*coords[0], *coords[3]), max_x = std::max(*coords[0], *coords[3]);
        int min_y = std::min(*coords[1], *coords[4]), max_y = std::max(*coords[1], *coords[4]);
        int min_z = std::min(*coords[2], *coords[5]), max_z = std::max(*coords[2], *coords[5]);
        long long volume = static_cast<long long>(max_x - min_x + 1) *
                            static_cast<long long>(max_y - min_y + 1) *
                            static_cast<long long>(max_z - min_z + 1);
        if (volume > COMMAND_VOLUME_LIMIT) {
            push("command.error.too_large", {std::to_string(volume), std::to_string(COMMAND_VOLUME_LIMIT)});
            return;
        }
        int placed = world->command_fill_region(min_x, min_y, min_z, max_x, max_y, max_z, *block);
        push("command.fill.done", {std::to_string(placed)});
        return;
    }

    if (name == "clone") {
        if (args.size() < 9) { push("command.clone.usage"); return; }
        std::optional<int> coords[9];
        for (int i = 0; i < 9; ++i) coords[i] = parse_int(args[i]);
        if (std::any_of(std::begin(coords), std::end(coords), [](auto& c) { return !c.has_value(); })) {
            push("command.error.integer_coordinates");
            return;
        }
        int min_x = std::min(*coords[0], *coords[3]), max_x = std::max(*coords[0], *coords[3]);
        int min_y = std::min(*coords[1], *coords[4]), max_y = std::max(*coords[1], *coords[4]);
        int min_z = std::min(*coords[2], *coords[5]), max_z = std::max(*coords[2], *coords[5]);
        long long volume = static_cast<long long>(max_x - min_x + 1) *
                            static_cast<long long>(max_y - min_y + 1) *
                            static_cast<long long>(max_z - min_z + 1);
        if (volume > COMMAND_VOLUME_LIMIT) {
            push("command.error.too_large", {std::to_string(volume), std::to_string(COMMAND_VOLUME_LIMIT)});
            return;
        }
        int dest_x = *coords[6], dest_y = *coords[7], dest_z = *coords[8];
        int placed = world->command_clone_region(min_x, min_y, min_z, max_x, max_y, max_z, dest_x, dest_y, dest_z);
        push("command.clone.done", {std::to_string(placed)});
        return;
    }

    if (name == "kill") {
        if (current_game_mode != GameMode::Survival) {
            push("command.kill.survival_only");
            return;
        }
        player.health().kill();
        push("command.kill.done");
        return;
    }

    push("command.unknown", {name});
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
    Vector3 feet = player.feet_position();
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

    // Inventory: toggles the storage panel open/closed (default E -
    // GameAction::ToggleInventory, rebindable in Settings > Controls),
    // freeing/recapturing the cursor to match. Chat owns the keyboard while
    // it's open (typing this key there shouldn't also toggle the
    // inventory), so this whole block is skipped then, same as it already
    // skips while the grid itself is open.
    bool chat_open = chat_hud.is_open();
    if (world && !chat_open && binding_pressed(settings.keybindings[static_cast<size_t>(GameAction::ToggleInventory)])) {
        inventory_hud.toggle(inventory);
        if (inventory_hud.is_open()) EnableCursor(); else DisableCursor();
    }
    // Chat: default T (GameAction::OpenChat, rebindable) opens it empty;
    // "/" (fixed, not rebindable - it's a punctuation shortcut, not really
    // its own action) opens it pre-filled - only when nothing else is
    // already claiming keyboard input, same "one modal input surface at a
    // time" rule the inventory/pause menu already follow. Escape takes
    // priority over everything else below: closing chat first, same as
    // vanilla, rather than falling through to the pause menu underneath it.
    if (chat_open && IsKeyPressed(KEY_ESCAPE)) {
        chat_hud.close();
    } else if (world && !chat_open && !inventory_hud.is_open() &&
               binding_pressed(settings.keybindings[static_cast<size_t>(GameAction::OpenChat)])) {
        chat_hud.open_chat();
    } else if (world && !chat_open && !inventory_hud.is_open() && IsKeyPressed(KEY_SLASH)) {
        chat_hud.open_command();
    } else if (inventory_hud.is_open() && IsKeyPressed(KEY_ESCAPE)) {
        inventory_hud.close(inventory);
        DisableCursor();
    } else if (world && !chat_open && IsKeyPressed(KEY_ESCAPE)) {
        pause_requested = true;
        return;
    }
    chat_open = chat_hud.is_open(); // may have just changed above
    // Number keys pick a hotbar slot directly - unlike Q/wheel-scroll
    // below, this works even while the inventory grid is open (including
    // mid-drag, with a stack already picked up onto the cursor): it only
    // ever touches inventory.selected_slot, never InventoryHud's own
    // carried_stack, so there's nothing for the grid to steal this from.
    // Still excluded while chat owns the keyboard - typing a digit there
    // must not also swap the selected slot underneath it.
    if (!chat_open) {
        for (int slot = 0; slot < HOTBAR_SIZE; ++slot) {
            if (IsKeyPressed(KEY_ONE + slot)) inventory.selected_slot = slot;
        }
    }
    if (!inventory_hud.is_open() && !chat_open) {
        // Mouse wheel also cycles the selected hotbar slot, same "scroll
        // up/away subtracts" convention as every other scrollable list in
        // this project (WorldListScreen, SettingsScreen's Controls grid,
        // InventoryHud's own creative-page scroll) - and wraps around at
        // either end instead of clamping, same as vanilla's hotbar. Stays
        // gated to the closed grid, unlike the number keys above - open,
        // the wheel already belongs to the creative page scroll instead.
        int wheel_steps = static_cast<int>(std::round(GetMouseWheelMove()));
        if (wheel_steps != 0) {
            inventory.selected_slot = ((inventory.selected_slot - wheel_steps) % HOTBAR_SIZE + HOTBAR_SIZE) % HOTBAR_SIZE;
        }
        // Drop (default Q, GameAction::DropItem - rebindable): throw one
        // item out of the selected hotbar slot. While the inventory screen
        // is open instead, the equivalent (drop over a hovered slot) is
        // handled inside draw()'s own inventory_hud.update_grid() call
        // (passed the same binding) - it needs to know which slot the
        // mouse is over, which only that call already tracks.
        if (world && binding_pressed(settings.keybindings[static_cast<size_t>(GameAction::DropItem)])) {
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

    // update_leaf_decay()/update_random_ticks() (sapling growth) live in
    // tick() - they're world simulation (see PendingLeafDecay's own
    // comment), not per-frame visual polish, so they run on Minecraft's
    // fixed 20/second clock instead of this variable frame rate one, the
    // same as dropped-item physics (tick_dropped_items(), also tick()) vs.
    // just its magnet-pull tracking staying here in update_dropped_items().
    update_dropped_items(delta_time);
    particles.update(delta_time, world.get());

    // Inventory and chat both steal the mouse/keyboard from the camera and
    // block interaction below - but, unlike the old early-return this
    // replaced, everything else (gravity, drowning, a planted sapling
    // growing) keeps simulating right through either being open, the same
    // way tick()'s own world-simulation clock never gated on this at all.
    // Only look (camera rotation) and interaction (raycasting needs the
    // crosshair, which a UI screen doesn't move) actually need suppressing.
    bool ui_captured = inventory_hud.is_open() || chat_open || sleeping;

    // Free-look camera: rebindable keys (Settings) to move, mouse to look.
    // Today's defaults are still W/A/S/D + Space to jump - see
    // default_keybindings() - just no longer hardcoded here.
    auto is_action_down = [this](GameAction action) {
        return binding_down(settings.keybindings[static_cast<size_t>(action)]);
    };
    Vector2 mouse_delta = GetMouseDelta();
    Vector3 rotation = {mouse_delta.x * CAMERA_MOUSE_SENSITIVITY, mouse_delta.y * CAMERA_MOUSE_SENSITIVITY, 0.0f};
    if (spawn_settle_frames > 0 || ui_captured) {
        // See spawn_settle_frames's own comment: this delta might still be
        // a spurious startup jump, not real player input - and while a UI
        // screen owns the mouse, its own delta means "moving the cursor
        // over a button", never "look around".
        rotation = {0.0f, 0.0f, 0.0f};
        if (spawn_settle_frames > 0) --spawn_settle_frames;
    }

    Vector3 previous_camera_position = camera.position;
    const bool was_grounded = player.is_grounded();
    UpdateCameraPro(&camera, {0.0f, 0.0f, 0.0f}, rotation, 0.0f);
    // Dead: same "no longer takes input" freeze real Minecraft's own death
    // screen imposes, just without the screen itself - see
    // update_player_damage()'s automatic respawn_player() a couple seconds
    // later. Physics (gravity, whatever residual velocity was left) still
    // runs so the body doesn't hang frozen mid-air.
    bool alive = !player.health().is_dead();
    // Movement input specifically also stops while a UI screen is open -
    // WASD types into chat instead of walking, same as vanilla - but
    // player.update_movement() below still runs every frame regardless,
    // zero-input, so gravity/buoyancy/damage keep applying.
    bool accepts_movement_input = alive && !ui_captured;
    if (world) {
        PlayerInput input;
        if (accepts_movement_input) {
            input.forward = (is_action_down(GameAction::MoveForward) ? 1.0f : 0.0f) -
                            (is_action_down(GameAction::MoveBackward) ? 1.0f : 0.0f);
            input.right = (is_action_down(GameAction::MoveRight) ? 1.0f : 0.0f) -
                          (is_action_down(GameAction::MoveLeft) ? 1.0f : 0.0f);
            input.jump = is_action_down(GameAction::Jump);
            input.sneak = is_action_down(GameAction::Sneak);
            input.sprint = is_action_down(GameAction::Sprint);
        }
        player.update_movement(camera, *world, current_game_mode, input, delta_time);
        const Vector3 travelled = Vector3Subtract(camera.position, previous_camera_position);
        audio.update_water(delta_time, player.is_in_water(),
                           Vector3LengthSqr(travelled) > 0.000025f);
        if (current_game_mode == GameMode::Survival) update_player_damage(delta_time);
    }

    // Emit by travelled distance, and only near a solid top surface. This
    // keeps the cadence frame-rate independent and prevents dust in flight.
    if (world) {
        float feet_y = camera.position.y - Player::EYE_HEIGHT;
        int ground_x = static_cast<int>(std::floor(camera.position.x));
        int ground_y = static_cast<int>(std::floor(feet_y - 0.06f));
        int ground_z = static_cast<int>(std::floor(camera.position.z));
        BlockType ground_type = world->get_block(ground_x, ground_y, ground_z);
        float ground_surface_y = static_cast<float>(ground_y) + 1.0f;
        bool supported_by_shape = false;
        BlockShapeBoxes ground_shape = world->collision_boxes_at(ground_x, ground_y, ground_z);
        for (int i = 0; i < ground_shape.count; ++i) {
            const BoundingBox& box = ground_shape.boxes[i];
            if (camera.position.x >= box.min.x && camera.position.x <= box.max.x &&
                camera.position.z >= box.min.z && camera.position.z <= box.max.z &&
                std::fabs(feet_y - box.max.y) <= 0.22f) {
                ground_surface_y = box.max.y;
                supported_by_shape = true;
                break;
            }
        }
        bool grounded = player.is_grounded() && supported_by_shape;
        float dx = camera.position.x - previous_camera_position.x;
        float dz = camera.position.z - previous_camera_position.z;
        float horizontal_distance = std::sqrt(dx * dx + dz * dz);
        constexpr float FOOTSTEP_DISTANCE = 1.2f;
        if (grounded && horizontal_distance > 0.0001f) {
            footstep_particle_distance += horizontal_distance;
            int emitted = 0;
            while (footstep_particle_distance >= FOOTSTEP_DISTANCE && emitted < 2) {
                particles.spawn_footstep(ground_type,
                    Vector3{camera.position.x, ground_surface_y, camera.position.z});
                audio.play_step(ground_type,
                    Vector3{camera.position.x, ground_surface_y, camera.position.z}, camera.position);
                footstep_particle_distance -= FOOTSTEP_DISTANCE;
                ++emitted;
            }
        } else if (!grounded) {
            footstep_particle_distance = 0.0f;
        }

        // Jumping and landing are contact events of their own. They must not
        // depend on horizontal distance, otherwise a straight jump/fall is
        // silent even though the feet leave or strike a real block.
        if (was_grounded && !player.is_grounded()) {
            float old_feet_y = previous_camera_position.y - Player::EYE_HEIGHT;
            int old_ground_y = static_cast<int>(std::floor(
                old_feet_y - 0.06f));
            int old_ground_x = static_cast<int>(std::floor(previous_camera_position.x));
            int old_ground_z = static_cast<int>(std::floor(previous_camera_position.z));
            BlockType old_ground = world->get_block(
                old_ground_x, old_ground_y, old_ground_z);
            BlockShapeBoxes old_ground_shape = world->collision_boxes_at(old_ground_x, old_ground_y, old_ground_z);
            for (int i = 0; i < old_ground_shape.count; ++i) {
                const BoundingBox& box = old_ground_shape.boxes[i];
                if (previous_camera_position.x >= box.min.x && previous_camera_position.x <= box.max.x &&
                    previous_camera_position.z >= box.min.z && previous_camera_position.z <= box.max.z &&
                    std::fabs(old_feet_y - box.max.y) <= 0.22f) {
                    audio.play_step(old_ground,
                        {previous_camera_position.x, box.max.y, previous_camera_position.z},
                        camera.position);
                    break;
                }
            }
        } else if (!was_grounded && player.is_grounded() &&
                   supported_by_shape) {
            audio.play_step(ground_type,
                {camera.position.x, ground_surface_y, camera.position.z}, camera.position);
            footstep_particle_distance = 0.0f;
        }
    }

    // camera.target isn't a unit vector (it's an arbitrary point ahead of
    // the camera), so the aim direction needs normalizing before it's used
    // as a ray direction.
    Vector3 aim = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    if (!alive || ui_captured) {
        // Dead, or a UI screen owns the mouse: no aiming, no breaking/
        // placing - see the input freeze above.
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
                check_attachment_support_near(hit->x, hit->y, hit->z);
                if (*broken == BlockType::Chest) spill_chest_if_any(hit->x, hit->y, hit->z);
                if (*broken == BlockType::Furnace || *broken == BlockType::LitFurnace) spill_furnace_if_any(hit->x, hit->y, hit->z);
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
                    // resolve_block_drops()/src/content/Drops.cpp. A block can
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
                    check_attachment_support_near(breaking_x, breaking_y, breaking_z);
                    if (*broken == BlockType::Chest) spill_chest_if_any(breaking_x, breaking_y, breaking_z);
                    if (*broken == BlockType::Furnace || *broken == BlockType::LitFurnace) {
                        spill_furnace_if_any(breaking_x, breaking_y, breaking_z);
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
            ? resolve_container_kind(*world, targeted_block->x, targeted_block->y, targeted_block->z)
            : std::nullopt;
        // A Chest needs its lid to actually swing open - a solid block
        // sitting directly on top blocks that, same as real Minecraft (it
        // still can't be opened even though right-clicking it doesn't do
        // anything else either, so this just leaves the click a no-op
        // rather than falling through to block placement).
        bool chest_blocked_above = container_kind &&
            (*container_kind == InventoryHud::ContainerKind::Chest || *container_kind == InventoryHud::ContainerKind::LargeChest) &&
            targeted_block && get_block_properties(world->get_block(
                targeted_block->x, targeted_block->y + 1, targeted_block->z)).solid;
        BlockType targeted_type = targeted_block
            ? world->get_block(targeted_block->x, targeted_block->y, targeted_block->z)
            : BlockType::Air;
        bool targeted_is_door = targeted_type == BlockType::OakDoorLower || targeted_type == BlockType::OakDoorUpper ||
                                 targeted_type == BlockType::IronDoorLower || targeted_type == BlockType::IronDoorUpper;
        bool targeted_is_bed = targeted_type == BlockType::BedHead || targeted_type == BlockType::BedFoot;

        if (container_kind && !chest_blocked_above) {
            inventory_hud.open_container(*container_kind, targeted_block->x, targeted_block->y, targeted_block->z);
            EnableCursor();
        } else if (!container_kind && targeted_block && targeted_is_bed) {
            start_sleeping(*targeted_block);
        } else if (!container_kind && targeted_block && (targeted_is_door || targeted_type == BlockType::OakTrapdoor)) {
            // Right-click toggles open/closed instead of placing - same
            // priority tier as the chest-container-open branch above. A
            // door's two halves stay in sync: whichever half was clicked,
            // flip both (matches iron doors too - no redstone system
            // exists here to open them any other way, so they open by hand
            // like wood doors rather than being permanently unusable).
            int x = targeted_block->x, y = targeted_block->y, z = targeted_block->z;
            uint16_t packed = world->get_block_state(x, y, z) ^ BlockStateBits::OPEN;
            world->set_block_state(x, y, z, packed);
            if (targeted_is_door) {
                bool is_lower = targeted_type == BlockType::OakDoorLower || targeted_type == BlockType::IronDoorLower;
                int partner_y = is_lower ? y + 1 : y - 1;
                uint16_t partner_packed = world->get_block_state(x, partner_y, z);
                partner_packed = static_cast<uint16_t>((partner_packed & ~BlockStateBits::OPEN) | (packed & BlockStateBits::OPEN));
                world->set_block_state(x, partner_y, z, partner_packed);
            }
        } else if (!container_kind && targeted_block && targeted_type == BlockType::Cake &&
                   current_game_mode == GameMode::Survival && player.health().current() < PlayerHealth::MAX_HEALTH) {
            // Eating restores HP directly (see CAKE_HEAL_PER_BITE's own
            // comment) instead of vanilla's hunger/saturation restore. No
            // inventory consumption - the cake block itself is what's
            // being consumed down to nothing.
            int x = targeted_block->x, y = targeted_block->y, z = targeted_block->z;
            uint16_t packed = world->get_block_state(x, y, z);
            uint8_t bite_count = static_cast<uint8_t>((packed & BlockStateBits::BITE_COUNT_MASK) >> BlockStateBits::BITE_COUNT_SHIFT);
            player.health().heal(CAKE_HEAL_PER_BITE);
            if (bite_count >= CAKE_MAX_BITES) {
                world->break_block(x, y, z);
            } else {
                // The cut direction is fixed on the first bite (this
                // block's own facing), from whichever way the player was
                // looking - a deliberate simplification vs. vanilla's own
                // fixed world direction, not a functional gap.
                if (bite_count == 0) world->set_block_orientation(x, y, z, direction_facing_player(aim));
                uint16_t new_packed = static_cast<uint16_t>((packed & ~BlockStateBits::BITE_COUNT_MASK) |
                    (static_cast<uint16_t>(bite_count + 1) << BlockStateBits::BITE_COUNT_SHIFT));
                world->set_block_state(x, y, z, new_packed);
            }
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
                if (current_game_mode == GameMode::Survival && player.health().current() < PlayerHealth::MAX_HEALTH) {
                    player.health().heal(held->heal_amount);
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
                bool placed = false;
                HorizontalDirection player_facing = direction_facing_player(aim);
                HorizontalDirection facing = selected.block == BlockType::OakStairs
                    ? opposite_direction(player_facing)
                    : player_facing;

                if (selected.block == BlockType::OakSlab && targeted_type == BlockType::OakSlab &&
                    slab_click_adds_missing_half(*world, *targeted_block)) {
                    if (!player.intersects_block(targeted_block->x, targeted_block->y, targeted_block->z)) {
                        placed = world->combine_oak_slab(targeted_block->x, targeted_block->y, targeted_block->z);
                    }
                } else {
                    bool replace_target = get_block_properties(targeted_type).replaceable;
                    int place_x = targeted_block->x + (replace_target ? 0 : static_cast<int>(targeted_block->normal.x));
                    int place_y = targeted_block->y + (replace_target ? 0 : static_cast<int>(targeted_block->normal.y));
                    int place_z = targeted_block->z + (replace_target ? 0 : static_cast<int>(targeted_block->normal.z));
                    if (!placement_intersects_player(player, selected.block, place_x, place_y, place_z, facing)) {
                        // Door/bed are placed as one atomic pair (World::
                        // place_door()/place_bed()) rather than through the
                        // generic single-cell path below - see their own
                        // comments for why (support check, rollback on a
                        // blocked second half).
                        if (selected.block == BlockType::OakDoorLower || selected.block == BlockType::IronDoorLower) {
                            placed = world->place_door(place_x, place_y, place_z, selected.block, facing);
                        } else if (selected.block == BlockType::BedHead) {
                            placed = world->place_bed(place_x, place_y, place_z, facing);
                        } else if (selected.block == BlockType::Chest) {
                            // Merges into a large/double chest with a matching
                            // neighbor when possible - see World::place_chest()'s
                            // own comment for the diagonal-conflict rule.
                            placed = world->place_chest(place_x, place_y, place_z, facing);
                        } else if (block_is_attachable(selected.block)) {
                            // Mounts onto whatever face was clicked - wall,
                            // floor or ceiling, per its own block definition
                            // "attach" list (World::place_attached_block()).
                            placed = world->place_attached_block(place_x, place_y, place_z, selected.block,
                                                                 targeted_block->normal);
                        } else if (world->place_block(place_x, place_y, place_z, selected.block)) {
                            placed = true;
                            if (block_needs_facing(selected.block)) {
                                world->set_block_orientation(place_x, place_y, place_z, facing);
                            }
                            if (selected.block == BlockType::OakTrapdoor || selected.block == BlockType::OakSlab) {
                                // Upper or lower half, vanilla's rule, from the
                                // original raycast hit (the face actually
                                // clicked), not the new cell - see RaycastHit::
                                // hit_point's own comment: the underside of a
                                // block -> top half, the top of a block ->
                                // bottom half, a side face -> whichever half of
                                // it was clicked.
                                bool top_half = targeted_block->normal.y != 0.0f
                                    ? targeted_block->normal.y < 0.0f
                                    : (targeted_block->hit_point.y - std::floor(targeted_block->hit_point.y)) >= 0.5f;
                                if (top_half) world->set_block_state(place_x, place_y, place_z, BlockStateBits::TOP_HALF);
                            }
                        }
                    }
                }
                // No explicit queueing needed for a freshly placed
                // sapling any more - it's just a regular block in a
                // loaded chunk now, so update_random_ticks() will find
                // it on its own on some future random tick.
                if (placed && current_game_mode == GameMode::Survival && --selected.count <= 0) selected.clear();
            }
        }
    }

    } // alive && !ui_captured

    for (auto& object : objects) {
        if (object->is_active()) {
            object->update(delta_time, world.get());
        }
    }

}

void GameEngine::draw()
{
    ClearBackground(RAYWHITE);

    // Inventory screens (hotbar grid, chest/furnace/workbench) keep the
    // world drawing live behind them - it keeps simulating anyway (see
    // update()'s own ui_captured handling) - just blurred: rendered at half
    // resolution into an off-screen target and drawn back through a small
    // GPU blur (ui::begin_blurred_background()), cheap enough every frame.
    // The Esc pause menu still uses a one-time snapshot instead (see
    // pause_requested below) - Application draws it behind its own menu, not
    // this one.
    const bool blur_world = inventory_hud.is_open();
    if (blur_world) ui::begin_blurred_background();
    {
        // How far the current frame already is into the *next* tick (0
        // right after one lands, approaching 1 right before the next does)
        // - every tick-simulated entity (dropped items, falling blocks)
        // interpolates its last two tick positions by this instead of
        // snapping between them, so 20Hz physics still reads as smooth
        // motion at render rate.
        float tick_alpha = std::clamp(tick_accumulator / TICK_DURATION, 0.0f, 1.0f);

        // game_tick has no meaningful value before a world exists (see
        // GameEngine::tick()) - harmless either way here (DayNightCycle::
        // sun_direction(0) is just a valid dawn-position vector, not a
        // crash), but draw_celestial_bodies() below still keeps its own
        // `world` guard since drawing the sun/moon quads at all before a
        // world exists would be pointless.
        Vector3 sun_dir = DayNightCycle::sun_direction(game_tick);
        float celestial_angle = DayNightCycle::celestial_angle(game_tick);

        Camera3D render_camera = make_render_camera();
        BeginMode3D(render_camera);
        draw_skybox(render_camera.position, celestial_angle);
        if (world) {
            draw_seeded_stars(render_camera.position, world->seed(), celestial_angle);

            // Sun/moon - drawn right after the sky's own gradient, still
            // well before any real terrain.
            draw_celestial_bodies(render_camera.position, sun_dir);
            draw_seeded_clouds(render_camera.position, world->seed(), game_tick, celestial_angle,
                               settings.render_distance_chunks * CHUNK_SIZE, settings.cloud_volume);

            // Day/night sky-light dimming - one shared value (block light
            // never changes with time, only how much of a cell's sky light
            // actually shows) fed to both the chunk mesh's own shader
            // uniform and dynamic entities' CPU-computed tint, so terrain,
            // dropped items, particles and the player's own model all
            // darken at night in lockstep instead of chunk faces alone
            // shifting while everything else stays lit as if at noon.
            float sky_factor = DayNightCycle::sky_light_factor(game_tick);
            set_chunk_daylight(sky_factor);
            set_entity_daylight_factor(sky_factor);

            // Settings > Graphics' brightness slider - a gamma exponent on
            // the already-combined light strength (1.0 at the slider's own
            // max, a no-op; higher only darkens shadow - see
            // set_chunk_brightness()'s own comment for why direct sunlight
            // never dims from this). Never touches the sky/sun/moon/fog,
            // which aren't lit by a block light level at all.
            float brightness_gamma = 1.0f + (100.0f - settings.brightness) / 100.0f * BRIGHTNESS_GAMMA_RANGE;
            set_chunk_brightness(brightness_gamma);
            set_entity_brightness_factor(brightness_gamma);

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
            BlockShapeBoxes target_shape = world->outline_boxes_at(
                targeted_block->x, targeted_block->y, targeted_block->z);
            ui::block_outline(target_shape);
            if (is_breaking && targeted_block->x == breaking_x && targeted_block->y == breaking_y &&
                targeted_block->z == breaking_z) {
                ui::block_breaking_overlay(target_shape, breaking_progress);
            }
        }
        EndMode3D();

        Color haze_color = skybox_horizon_color();
        haze_color.a = CAMERA_HAZE_ALPHA;
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), haze_color);
    }
    if (blur_world) ui::end_blurred_background();
    if (inventory_hud.is_open()) {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), IN_GAME_MENU_OVERLAY);
    }

    // Esc was pressed this frame: keep a blurred copy of the world (no HUD
    // yet) for the pause menu's background - see take_pause_snapshot().
    if (pause_requested) {
        if (IsTextureValid(pause_snapshot)) UnloadTexture(pause_snapshot);
        pause_snapshot = ui::capture_blurred_background();
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

    bool show_death_screen = world && player.health().is_dead();
    if (!show_death_screen && !inventory_hud.is_open() && sleep_overlay <= 0.0f) ui::crosshair();

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
        // to show (player.health() simply never leaves full health there).
        if (current_game_mode == GameMode::Survival) {
            inventory_hud.draw_hearts(player.health().current(), PlayerHealth::MAX_HEALTH);
        }
        // Drawn and click-handled together here (not from update()) - the
        // same immediate-mode pattern every menu screen already uses.
        if (inventory_hud.is_open()) {
            const Binding& drop_binding = settings.keybindings[static_cast<size_t>(GameAction::DropItem)];
            if (std::optional<ItemStack> dropped = inventory_hud.update_grid(inventory, current_game_mode, world.get(), drop_binding)) {
                spawn_dropped_item(*dropped);
            }
        }

        // Same immediate-mode draw+handle pattern as the inventory grid
        // above - chat_hud reports a submitted line (already closed) the
        // frame Enter/click-away confirms it.
        if (std::optional<std::string> submitted = chat_hud.update_and_draw()) {
            handle_chat_submit(*submitted);
        }
    }

    if (show_debug_overlay && world) {
        ui::draw_debug_overlay(camera, *world,
            current_game_mode == GameMode::Creative ? CREATIVE_REACH : SURVIVAL_REACH,
            current_game_mode == GameMode::Creative ? camera_move_speed : player.horizontal_speed(), game_tick);
    }

    if (sleep_overlay > 0.0f) {
        float fade = std::clamp(sleep_overlay, 0.0f, 1.0f);
        unsigned char alpha = static_cast<unsigned char>(static_cast<float>(SLEEP_OVERLAY_ALPHA) * fade);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, alpha});

        if (sleeping) {
            const float width = ui::scaled(420.0f);
            const float height = ui::scaled(ui::BUTTON_HEIGHT);
            const float gap = ui::scaled(ui::BUTTON_GAP);
            const float half = (width - gap) * 0.5f;
            const float x = (GetScreenWidth() - width) * 0.5f;
            const float y = GetScreenHeight() - ui::scaled(92.0f);
            ui::panel({x - ui::scaled(10.0f), y - ui::scaled(10.0f),
                       width + ui::scaled(20.0f), height + ui::scaled(20.0f)},
                      Color{0, 0, 0, static_cast<unsigned char>(SLEEP_PANEL_ALPHA * fade)});
            if (ui::button({x, y, half, height}, ui::tr("sleep.leave_bed"))) {
                leave_bed();
            }
            if (ui::button({x + half + gap, y, half, height}, ui::tr("sleep.open_menu"))) {
                pause_requested = true;
            }
        }
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
}

void GameEngine::update_frame(float delta_time)
{
    if (sleeping) {
        tick_accumulator = 0.0f;
        update_sleep_fast_forward(delta_time);
    } else {
        update_sleep_fast_forward(delta_time);

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
}

bool GameEngine::open_world(const std::string& folder_name, const GameLoadProgress& progress)
{
    std::optional<WorldInfo> info = WorldSave::load_world_info(folder_name);
    if (!info) return false;

    WorldConfig config;
    config.seed = info->seed;
    config.world_type = info->world_type;
    if (info->world_type == WorldType::Custom) config.custom = std::make_shared<CustomWorld>(info->custom);
    config.save_directory = WorldSave::world_directory(folder_name);
    config.loaded_radius_chunks = settings.render_distance_chunks;
    config.fog_distance_blocks = settings.fog_distance_blocks;
    // active_radius_chunks keeps WorldConfig's own default - simulation
    // distance isn't a Settings field (see Settings.hpp), only render
    // distance and fog distance are user-configurable.

    current_world_folder = folder_name;
    current_world_info = *info;
    current_game_mode = current_world_info.game_mode;
    current_world_allows_commands = current_world_info.allow_commands;

    // A world with no player.json yet has never been played - it's being
    // generated from scratch, not loaded back.
    std::optional<PlayerSaveState> saved = WorldSave::load_player_state(folder_name);
    const bool generating = !saved;
    progress(generating, WorldLoadStage::Terrain, 0.0f); // up before the World is even built

    auto new_world = std::make_unique<World>(config);
    new_world->set_load_progress_callback([&progress, generating](WorldLoadStage stage, float fraction) {
        progress(generating, stage, fraction);
    });
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
    if (saved) {
        world = std::move(new_world);
        camera.position = saved->position;
        camera.target = Vector3Add(camera.position, Vector3Scale(saved->forward, 10.0f));
        inventory = saved->inventory;
        spawn_settle_frames = 3; // see its own comment on set_world()
        player.reset(camera);
        player.health().reset();
        player.health().set_health(saved->health);
        reset_life_timers();
        camera_view = CameraView::FirstPerson;
        // Resume the day/night cycle (and every random-tick roll) exactly
        // where it was, instead of set_world()'s own fresh-dawn default -
        // see PlayerSaveState::game_tick's own comment.
        game_tick = saved->game_tick;
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
        // Same for every furnace - burn/cook progress included, so a lit
        // furnace keeps smelting where it left off.
        for (const FurnaceSaveState& furnace : WorldSave::load_furnaces(folder_name)) {
            world->furnace_state(furnace.x, furnace.y, furnace.z) = furnace.state;
        }
    } else {
        set_world(std::move(new_world));
    }
    // Loading's done - no more loading-screen frames from here on (the
    // callback refers to `progress`, which only lives for this call).
    if (world) world->set_load_progress_callback({});
    pause_requested = false;
    return true;
}

void GameEngine::save_player_state()
{
    if (!world) return;

    current_world_info.game_mode = current_game_mode;
    WorldSave::save_world_info(current_world_info);

    PlayerSaveState state;
    state.position = camera.position;
    state.forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    state.inventory = inventory;
    state.health = player.health().current();
    state.game_tick = game_tick;
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

    std::vector<FurnaceSaveState> saved_furnaces;
    for (const World::FurnaceSnapshot& furnace : world->all_furnace_states()) {
        saved_furnaces.push_back({furnace.x, furnace.y, furnace.z, furnace.state});
    }
    WorldSave::save_furnaces(current_world_folder, saved_furnaces);
}

void GameEngine::close_world()
{
    save_player_state();
    world.reset(); // ~World() flushes any modified chunks still resident - same guarantee quitting the app outright already relies on
    dropped_items.clear();
    particles.clear();
    footstep_particle_distance = 0.0f;
    current_world_folder.clear();
    current_world_info = {};
    current_world_allows_commands = true;
    inventory_hud.close(inventory);
    chat_hud.close(); // otherwise its is_open() would leak into the next world's very first frame
    world_spawn_override.reset(); // session-only override - see its own comment
    pause_requested = false;
}

bool GameEngine::take_pause_request()
{
    bool requested = pause_requested;
    pause_requested = false;
    return requested;
}

Texture2D GameEngine::take_pause_snapshot()
{
    Texture2D snapshot = pause_snapshot;
    pause_snapshot = {};
    return snapshot;
}
