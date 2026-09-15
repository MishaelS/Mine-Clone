#pragma once

#include "core/Block.hpp"
#include "player/Item.hpp"

#include <array>
#include <vector>

constexpr int HOTBAR_SIZE = 9;
constexpr int INVENTORY_STORAGE_SIZE = 27;
constexpr int MAX_ITEM_STACK = 64;

// Either a block stack (`block` != Air, `tool` == ItemType::None), a single
// tool (`tool` != None and ItemCategory::Tool, `block` unused/Air), or a
// material stack (`tool` != None and ItemCategory::Material, `block`
// unused/Air) - never more than one of the three. Only a real Tool never
// stacks (count is always 0 or 1, matching real Minecraft: two fresh tools
// of the same kind still sit in separate slots, since they can carry
// independent durability) - a Material stacks up to MAX_ITEM_STACK same as
// a block does.
struct ItemStack {
    BlockType block = BlockType::Air;
    ItemType tool = ItemType::None; // holds a Tool OR a Material item - see is_tool()/is_material()
    int count = 0;
    int durability = 0; // current remaining durability - only meaningful while is_tool()

    bool is_tool() const { return tool != ItemType::None && get_item_properties(tool).category == ItemCategory::Tool; }
    bool is_material() const { return tool != ItemType::None && get_item_properties(tool).category == ItemCategory::Material; }
    // True for either a Tool or a Material - i.e. this slot holds an
    // ItemType at all rather than a BlockType. Callers that only care
    // "which field do I read, .tool or .block" (icon lookup, tooltip text,
    // save/load) should check this, not is_tool() - is_tool() answers a
    // narrower question (durability/mining-speed semantics apply).
    bool holds_item() const { return tool != ItemType::None; }
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

    // Same as add(BlockType, count) above, for a Material item instead of a
    // block (see ItemCategory::Material) - a crafting/drop ingredient like
    // Stick or Coal, which stacks the same way a block does rather than
    // occupying its own single-item slot like a real Tool. `type` must be a
    // Material; a Tool passed here would silently misbehave (durability -
    // use add_tool()/put_back() for those instead).
    int add_item(ItemType type, int count = 1);

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

// A survival-style inventory: starts completely empty (no starting tools -
// removed by request). Blocks/tools only ever enter it through world drops
// (and, once a crafting system exists, crafting) from here on.
Inventory default_inventory();

// Unlimited creative hotbar seed (the first nine registered blocks) and
// the complete picker catalog used only by the creative inventory screen.
Inventory creative_inventory();
std::vector<BlockType> all_placeable_blocks();

// Splits exactly one unit off `slot` and returns it, leaving the remainder
// in place (clearing `slot` entirely if that was the last unit) - a block
// stack loses exactly 1 from its count; a tool (always count 1, no partial
// stacks) comes out whole. Returns an empty ItemStack (does nothing to
// `slot`) if it was already empty. Shared by every "drop one item" path -
// Q on the selected hotbar slot, Q over a hovered inventory slot
// (GameEngine.cpp/InventoryHud.cpp) - so both split a stack exactly the
// same way.
ItemStack take_one_item(ItemStack& slot);
