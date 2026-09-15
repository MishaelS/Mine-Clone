#include "player/Inventory.hpp"

#include <algorithm>

Inventory default_inventory()
{
    // No starting tools any more (removed by request) - a fresh survival
    // inventory is just empty; everything from here on has to come from a
    // world drop (or, once a crafting system exists, crafting).
    return Inventory{};
}

std::vector<BlockType> all_placeable_blocks()
{
    std::vector<BlockType> result;
    for (uint8_t id = 1; id < static_cast<uint8_t>(BlockType::Count); ++id) {
        BlockType type = static_cast<BlockType>(id);
        if (type != BlockType::Water) result.push_back(type);
    }
    return result;
}

Inventory creative_inventory()
{
    Inventory inventory;
    std::vector<BlockType> blocks = all_placeable_blocks();
    for (size_t i = 0; i < inventory.hotbar.size() && i < blocks.size(); ++i) {
        inventory.hotbar[i].block = blocks[i];
        inventory.hotbar[i].count = MAX_ITEM_STACK;
    }
    return inventory;
}

int Inventory::add(BlockType type, int count)
{
    if (type == BlockType::Air || count <= 0) return count;

    auto fill = [&](auto& slots, bool matching_only) {
        for (ItemStack& slot : slots) {
            if (matching_only) {
                if (slot.empty() || slot.block != type || slot.count >= MAX_ITEM_STACK) continue;
            } else if (!slot.empty()) {
                continue;
            }

            if (slot.empty()) {
                slot.block = type;
                slot.tool = ItemType::None;
                slot.count = 0;
            }
            int moved = std::min(count, MAX_ITEM_STACK - slot.count);
            slot.count += moved;
            count -= moved;
            if (count == 0) return;
        }
    };

    fill(hotbar, true);
    fill(storage, true);
    fill(hotbar, false);
    fill(storage, false);
    return count;
}

int Inventory::add_item(ItemType type, int count)
{
    if (type == ItemType::None || count <= 0) return count;

    auto fill = [&](auto& slots, bool matching_only) {
        for (ItemStack& slot : slots) {
            if (matching_only) {
                if (slot.empty() || slot.tool != type || slot.count >= MAX_ITEM_STACK) continue;
            } else if (!slot.empty()) {
                continue;
            }

            if (slot.empty()) {
                slot.block = BlockType::Air;
                slot.tool = type;
                slot.count = 0;
            }
            int moved = std::min(count, MAX_ITEM_STACK - slot.count);
            slot.count += moved;
            count -= moved;
            if (count == 0) return;
        }
    };

    fill(hotbar, true);
    fill(storage, true);
    fill(hotbar, false);
    fill(storage, false);
    return count;
}

bool Inventory::add_tool(ItemType type)
{
    const ItemProperties& properties = get_item_properties(type);

    auto try_fill = [&](auto& slots) {
        for (ItemStack& slot : slots) {
            if (!slot.empty()) continue;
            slot.block = BlockType::Air;
            slot.tool = type;
            slot.count = 1;
            slot.durability = properties.max_durability;
            return true;
        }
        return false;
    };

    return try_fill(hotbar) || try_fill(storage);
}

bool Inventory::put_back(const ItemStack& stack)
{
    if (stack.empty()) return false;
    if (stack.is_material()) return add_item(stack.tool, stack.count) == 0;
    if (!stack.is_tool()) return add(stack.block, stack.count) == 0;

    auto try_fill = [&](auto& slots) {
        for (ItemStack& slot : slots) {
            if (!slot.empty()) continue;
            slot = stack;
            return true;
        }
        return false;
    };

    return try_fill(hotbar) || try_fill(storage);
}

ItemStack take_one_item(ItemStack& slot)
{
    if (slot.empty()) return ItemStack{};

    if (slot.is_tool()) {
        ItemStack taken = slot;
        slot.clear();
        return taken;
    }

    ItemStack taken;
    if (slot.is_material()) {
        taken.tool = slot.tool;
    } else {
        taken.block = slot.block;
    }
    taken.count = 1;
    if (--slot.count <= 0) slot.clear();
    return taken;
}
