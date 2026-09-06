#include "core/Inventory.hpp"

#include <cstdint>

Inventory default_inventory()
{
    Inventory inventory;
    int slot = 0;
    for (uint8_t i = 1; slot < HOTBAR_SIZE && i < static_cast<uint8_t>(BlockType::Count); ++i) {
        inventory.hotbar[slot++] = static_cast<BlockType>(i); // starts at 1 - skips Air (0)
    }
    return inventory;
}

std::vector<BlockType> all_placeable_blocks()
{
    std::vector<BlockType> blocks;
    for (uint8_t i = 1; i < static_cast<uint8_t>(BlockType::Count); ++i) {
        blocks.push_back(static_cast<BlockType>(i)); // starts at 1 - skips Air (0)
    }
    return blocks;
}
