#include "player/Inventory.hpp"

#include <algorithm>

Inventory default_inventory()
{
    Inventory inventory;
    // No crafting system yet, so a fresh survival inventory starts with
    // one basic tool per kind that actually benefits from mining speed -
    // otherwise there would be no way to ever obtain one. Sword/Hoe are
    // left out: neither does anything yet (no combat, no farming), so
    // starting with one would just be inventory clutter.
    inventory.add_tool(ItemType::WoodenPickaxe);
    inventory.add_tool(ItemType::WoodenShovel);
    inventory.add_tool(ItemType::WoodenAxe);
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
    taken.block = slot.block;
    taken.count = 1;
    if (--slot.count <= 0) slot.clear();
    return taken;
}
