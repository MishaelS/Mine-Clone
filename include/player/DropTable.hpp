#pragma once

#include "core/Block.hpp"
#include "player/Inventory.hpp"

#include <vector>

// One resolved drop from breaking a block - either a block stack (e.g.
// Stone -> Cobblestone) or a material stack (e.g. Coal Ore -> Coal).
struct DropRoll {
    bool is_item = false;
    BlockType block = BlockType::Air;
    ItemType item = ItemType::None;
    int count = 1;
};

// Loads assets/drops.json (Beta 1.7.3-sourced per-block drop table - see
// the file's own "_notes"). Call once, after both Load_block_definitions()
// and Load_item_definitions() (needs both name tables to resolve entries).
// A drop entry naming a block/item this build doesn't have yet (the file
// deliberately documents more of Beta 1.7.3 than the engine can act on -
// saplings, doors, etc.) is silently skipped rather than treated as an
// error, unlike blocks.json's own stricter loader.
void Load_drop_table();

// True if breaking `type` with `selected` would actually yield at least a
// chance at a drop - i.e. `selected` satisfies whatever requires_tool/
// min_tool_tier drops.json demands for this block (a block with neither
// set - Dirt, Log, Leaves, ... - is always harvestable, hand included).
// Doesn't roll anything itself; resolve_block_drops() checks this exact
// same gate before rolling. Exposed separately for GameEngine's own
// break-speed formula, which needs this "can harvest at all" condition
// too - real Minecraft's own break-speed formula uses a tenfold-slower
// divisor specifically when it's false, on top of (not instead of) the
// ordinary tool-category speed bonus.
bool can_harvest_block(BlockType type, const ItemStack& selected);

// What breaking `type` with `selected` (bare hand if empty or not a tool)
// actually yields - rolls every one of that block's possible drops
// independently against its own chance (a block can yield more than one
// stack at once, e.g. Gravel's Flint roll alongside its own Gravel roll).
// If `selected` doesn't meet the block's own requires_tool/min_tool_tier
// (see drops.json), returns empty - same as real Minecraft, breaking with
// the wrong/no tool yields nothing at all, not a partial drop. A BlockType
// with no drop-table entry at all (not yet covered by drops.json, or its
// name didn't resolve) falls back to "drops itself" - the old behavior -
// so nothing regresses to silently dropping nothing.
std::vector<DropRoll> resolve_block_drops(BlockType type, const ItemStack& selected);
