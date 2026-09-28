#pragma once

#include "core/Block.hpp"
#include "items/Inventory.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct StructureDefinition;

// What a block does - the engine's block API. A block type has any number
// of behaviors; the game calls their events (a player placed it, broke it,
// right-clicked it, a random tick landed on it, a neighbor changed...), and
// they act on the world only through BlockApi. Behaviors written in C++
// (src/content/Behaviors.cpp) use it directly; Lua scripts and the model
// editor's behavior graphs get the very same calls.
//
// Everything here runs on the main thread, during the game's tick or a
// player's click - never during background world generation.

struct BlockPos {
    int x = 0, y = 0, z = 0;

    BlockPos offset(int dx, int dy, int dz) const { return {x + dx, y + dy, z + dz}; }
    BlockPos above() const { return offset(0, 1, 0); }
    BlockPos below() const { return offset(0, -1, 0); }
    BlockPos neighbor(BlockFace face) const;
    bool operator==(const BlockPos& other) const { return x == other.x && y == other.y && z == other.z; }
    bool operator!=(const BlockPos& other) const { return !(*this == other); }
};

// A right click on a block: what the player holds, where they clicked.
struct BlockUse {
    ItemStack& held;  // the selected hotbar slot - take from it with BlockApi::take_held()
    BlockFace face;   // the face of the block that was clicked
    bool creative;    // a creative player's stack never runs out
};

// What a behavior can ask of and do to the world.
class BlockApi {
public:
    virtual ~BlockApi() = default;

    // --- Queries ---
    virtual BlockType get_block(BlockPos pos) const = 0;
    // A state property of the block there (BlockStateProperty - a crop's
    // "age"), or nothing when that block has none by that name.
    virtual std::optional<int> get_property(BlockPos pos, const std::string& name) const = 0;
    // 0..15: the light there right now - sky light dimmed by the time of
    // day, or block light (torches), whichever is brighter.
    virtual int get_light(BlockPos pos) const = 0;
    // 0..15, the two channels, sky light as at noon.
    virtual int get_sky_light(BlockPos pos) const = 0;
    virtual int get_block_light(BlockPos pos) const = 0;
    // Whether a `type` block is within `radius` blocks sideways and
    // `height` up or down of `center` (not `center` itself) - water near
    // farmland.
    virtual bool is_block_near(BlockPos center, BlockType type, int radius, int height) const = 0;
    virtual float random() = 0; // 0..1
    virtual uint64_t game_tick() const = 0;

    // --- Actions ---
    // Puts `type` there, whatever was there; its own behaviors hear
    // on_placed(), its neighbors on_neighbor_changed().
    virtual void set_block(BlockPos pos, BlockType type) = 0;
    // Sets a state property of the block there (clamped to 0..its max);
    // nothing if it has no such property. Neighbors are told.
    virtual void set_property(BlockPos pos, const std::string& name, int value) = 0;
    // Breaks it as the player's hand would: particles, sound, and (with
    // `drops`) whatever it drops. Its behaviors hear on_broken().
    virtual void break_block(BlockPos pos, bool drops) = 0;
    // Drops `stack` as an item popping out of the cell.
    virtual void drop_item(BlockPos pos, const ItemStack& stack) = 0;
    // on_scheduled_tick() for the block there, `delay_ticks` game ticks
    // (20 a second) from now - if it's still the same block by then.
    virtual void schedule_tick(BlockPos pos, int delay_ticks) = 0;
    // Grows `structure` (one of its variants at random) with its origin at
    // `origin`, the origin cell counted as empty - false, and nothing
    // placed, when one of its required blocks is in the way.
    virtual bool place_structure(BlockPos origin, const StructureDefinition& structure) = 0;
    // Takes `count` from what the player holds (not in creative).
    virtual void take_held(BlockUse& use, int count) = 0;
};

// One behavior of a block type - override only the events it cares about.
class BlockBehavior {
public:
    virtual ~BlockBehavior() = default;

    // Whether a player may place the block at `pos` (the cell is still
    // empty). Every behavior of the block must agree.
    virtual bool can_place(BlockApi& api, BlockPos pos) { (void)api; (void)pos; return true; }
    // Whether it can stay where it is - asked whenever a neighbor
    // changes; if any behavior says no, it breaks (with its drops).
    virtual bool can_stay(BlockApi& api, BlockPos pos) { (void)api; (void)pos; return true; }
    // It has just appeared at `pos` - placed by a player or set by a behavior.
    virtual void on_placed(BlockApi& api, BlockPos pos) { (void)api; (void)pos; }
    // It was at `pos` and has just been broken - the cell is already empty.
    virtual void on_broken(BlockApi& api, BlockPos pos) { (void)api; (void)pos; }
    // A right click on it. True: the click was used up (nothing gets
    // placed against it, no other behavior hears it).
    virtual bool on_use(BlockApi& api, BlockPos pos, BlockUse& use) { (void)api; (void)pos; (void)use; return false; }
    // Now and then at random (Minecraft's random ticks - about once every
    // 68 seconds per block on average).
    virtual void on_random_tick(BlockApi& api, BlockPos pos) { (void)api; (void)pos; }
    // When a schedule_tick() for it comes due.
    virtual void on_scheduled_tick(BlockApi& api, BlockPos pos) { (void)api; (void)pos; }
    // The block at `from`, next to it, changed.
    virtual void on_neighbor_changed(BlockApi& api, BlockPos pos, BlockPos from) { (void)api; (void)pos; (void)from; }
};

// Every block type's behaviors, set up once at startup (after the blocks
// and structures are loaded - content::register_behaviors()).
namespace block_behaviors {
    void add(BlockType type, std::shared_ptr<BlockBehavior> behavior);
    const std::vector<std::shared_ptr<BlockBehavior>>& of(BlockType type);
    void clear();
}
