#pragma once

#include "player/Inventory.hpp"

#include "raylib.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Persistent world rule: creative enables flight, instant breaking and an
// unlimited block catalog; survival enables player physics, timed mining,
// finite stacks and block drops. Missing mode in an older save defaults to
// Creative for backward compatibility.
enum class GameMode : uint8_t { Creative, Survival };

struct WorldInfo {
    std::string folder_name;  // sanitized on-disk directory name under saves/
    std::string display_name; // the name as typed, shown in the world list
    uint32_t seed = 0;
    GameMode game_mode = GameMode::Creative;
};

// Where the player was and what their hotbar held, last time they left this
// world ("Выйти и сохранить игру") - kept in its own file (player.json),
// separate from world.json's immutable creation metadata (name/seed/mode),
// since this changes on every exit while that never does.
struct PlayerSaveState {
    Vector3 position = {0.0f, 0.0f,  0.0f};    // camera position (eye height already included)
    Vector3 forward  = {0.0f, 0.0f, -1.0f};    // normalized look direction - camera.target is reconstructed from this on load
    Inventory inventory;
    // Half-heart units, same as PlayerHealth::MAX_HEALTH (kept as a plain
    // int here rather than including player/PlayerHealth.hpp, which this
    // header has no other reason to depend on). Missing in an older save
    // defaults to full health - see WorldSave::load_player_state().
    int health = 20;
};

// A dropped item still sitting on the ground, last time this world was
// exited - enough to reconstruct an equivalent DroppedItem on load,
// including how far into its despawn countdown it already was (see
// DroppedItem::set_age()), so it doesn't get another full 5 minutes just
// for having been saved and reloaded.
struct DroppedItemSaveState {
    Vector3 position = {0.0f, 0.0f, 0.0f};
    ItemStack stack;
    float age = 0.0f;
    std::optional<Color> block_tint; // grass/foliage tint - see DroppedItem's own field
};

// A Chest's own 27-slot storage at a specific block position - see
// World::chest_inventory()/World::all_chest_inventories().
struct ChestSaveState {
    int x = 0, y = 0, z = 0;
    std::array<ItemStack, INVENTORY_STORAGE_SIZE> slots{};
};

namespace WorldSave {
    // Every saves/*/world.json that exists and parses successfully -
    // corrupted entries are skipped rather than crashing the world list.
    std::vector<WorldInfo> list_worlds();

    std::optional<WorldInfo> load_world_info(const std::string& folder_name);

    // Writes saves/<folder_name>/world.json and creates chunks/ alongside
    // it. `info.folder_name` must already be a sanitized, available name -
    // see next_available_folder_name().
    bool create_world(const WorldInfo& info);

    // Deletes saves/<folder_name>/ and everything under it.
    bool delete_world(const std::string& folder_name);

    // saves/<folder_name> (SAVE_DATA_PATH-rooted), as a plain path string -
    // what World's WorldConfig::save_directory gets set to.
    std::string world_directory(const std::string& folder_name);

    // Strips path separators, ':', and control characters from a
    // player-typed world name so it's safe to use as a single path segment;
    // falls back to "world" if that leaves nothing. Cyrillic and other
    // non-ASCII text passes through unchanged - macOS stores filenames as
    // UTF-8 natively.
    std::string sanitize_folder_name(const std::string& world_name);

    // sanitize_folder_name(), then appends " (2)", " (3)", ... until the
    // result doesn't collide with an existing saves/ subdirectory.
    std::string next_available_folder_name(const std::string& world_name);

    // Blank seed_text -> hash(world_name). seed_text that fully parses as
    // an integer (the whole string, not just a prefix) -> that integer
    // directly. Otherwise (arbitrary text, same as real Minecraft allows)
    // -> hash(seed_text). The hash is a small local FNV-1a-style mix (see
    // the .cpp - same spirit as Chunk.cpp's cave_chunk_seed), not
    // std::hash<std::string>, whose output isn't guaranteed stable across
    // runs/implementations and so isn't fit for a seed players might want
    // to share.
    uint32_t derive_seed(const std::string& world_name, const std::string& seed_text);

    // saves/<folder_name>/player.json. Overwrites any previous save for
    // this world - there's only ever one player, no multiple save slots.
    bool save_player_state(const std::string& folder_name, const PlayerSaveState& state);

    // std::nullopt if this world has never been exited before (no
    // player.json yet, e.g. right after creation) or the file is
    // corrupted - callers fall back to World::find_spawn_position()/
    // default_inventory() either way.
    std::optional<PlayerSaveState> load_player_state(const std::string& folder_name);

    // saves/<folder_name>/items.json - every dropped item still on the
    // ground when the player last exited this world. Overwrites any
    // previous save the same way save_player_state() does. An empty list
    // still overwrites the file (with an empty array), same as leaving no
    // items on the ground should mean none get reloaded either.
    bool save_dropped_items(const std::string& folder_name, const std::vector<DroppedItemSaveState>& items);

    // Empty (not nullopt) if the file is missing or corrupted - unlike
    // load_player_state(), there's no meaningfully different fallback
    // behavior for "never saved" vs "nothing was on the ground", so callers
    // don't need to tell them apart.
    std::vector<DroppedItemSaveState> load_dropped_items(const std::string& folder_name);

    // saves/<folder_name>/chests.json - every Chest's own storage (see
    // World::all_chest_inventories()). Same overwrite-on-every-save
    // behavior as save_dropped_items().
    bool save_chests(const std::string& folder_name, const std::vector<ChestSaveState>& chests);

    // Empty if the file is missing/corrupted - same "no fallback needed"
    // reasoning as load_dropped_items().
    std::vector<ChestSaveState> load_chests(const std::string& folder_name);
}
