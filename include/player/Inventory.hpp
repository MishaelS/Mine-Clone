#pragma once

#include "core/Block.hpp"
#include "player/Item.hpp"

#include <array>

constexpr int HOTBAR_SIZE = 9;
constexpr int INVENTORY_STORAGE_SIZE = 27;
constexpr int MAX_ITEM_STACK = 64;

// Either a block stack (`block` != Air, `tool` == ItemType::None) or a
// single tool (`tool` != None, `block` unused/Air) - never both. Tools
// never stack (count is always 0 or 1, matching real Minecraft: two fresh
// tools of the same kind still sit in separate slots, since they can carry
// independent durability).
struct ItemStack {
    BlockType block = BlockType::Air;
    ItemType tool = ItemType::None;
    int count = 0;
    int durability = 0; // current remaining durability - only meaningful while is_tool()

    bool is_tool() const { return tool != ItemType::None; }
    bool empty() const { return count <= 0 || (block == BlockType::Air && tool == ItemType::None); }
    void clear() { block = BlockType::Air; tool = ItemType::None; count = 0; durability = 0; }
};

struct Inventory {
    std::array<ItemStack, HOTBAR_SIZE> hotbar{};
    std::array<ItemStack, INVENTORY_STORAGE_SIZE> storage{};
    int selected_slot = 0;

    // Adds as much as possible, filling matching stacks before empty slots.
    // Returns the number of items that did not fit.
    int add(BlockType type, int count = 1);

    // Places one fresh, full-durability tool into the first empty hotbar
    // slot, then the first empty storage slot. Returns false only if both
    // are completely full. Tools never merge into an existing stack (see
    // ItemStack's own comment), so unlike add() there's no matching pass.
    bool add_tool(ItemType type);

    // Places `stack` verbatim - preserving a tool's current durability,
    // unlike add_tool() which always starts one at full - into the first
    // empty hotbar slot, then the first empty storage slot. Returns
    // whether it fit; both InventoryHud's own use (a fallback for
    // returning a carried stack if its original slot became unavailable
    // while it was held - shouldn't normally happen) and a picked-up
    // dropped tool (GameEngine.cpp) tolerate ignoring a false the same way
    // add()'s own leftover-count return already goes ignored for blocks.
    bool put_back(const ItemStack& stack);
};

// A survival-style inventory: empty except for the handful of starting
// tools below. Blocks otherwise enter it only through world drops - and
// with no crafting system yet, a fresh tool has no other way in either, so
// default_inventory() is currently the only source of one. Revisit once
// crafting exists and remove these.
Inventory default_inventory();

// Splits exactly one unit off `slot` and returns it, leaving the remainder
// in place (clearing `slot` entirely if that was the last unit) - a block
// stack loses exactly 1 from its count; a tool (always count 1, no partial
// stacks) comes out whole. Returns an empty ItemStack (does nothing to
// `slot`) if it was already empty. Shared by every "drop one item" path -
// Q on the selected hotbar slot, Q over a hovered inventory slot
// (GameEngine.cpp/InventoryHud.cpp) - so both split a stack exactly the
// same way.
ItemStack take_one_item(ItemStack& slot);
