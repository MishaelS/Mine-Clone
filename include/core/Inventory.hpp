#pragma once

#include "core/Block.hpp"

#include <array>
#include <vector>

// A hotbar slot per number key (1-9), each holding exactly one BlockType -
// Creative-style unlimited access, no stack counts, no survival collecting
// (GameMode::Survival has no mechanics yet - see core/WorldSave). Not
// persisted: a fresh Inventory (default_inventory()) is handed to
// GameEngine each session.
constexpr int HOTBAR_SIZE = 9;

struct Inventory {
    std::array<BlockType, HOTBAR_SIZE> hotbar;
    int selected_slot = 0;
};

// The first HOTBAR_SIZE non-Air BlockTypes in enum order - just needs to be
// immediately usable on a fresh session, not a curated starter set.
Inventory default_inventory();

// Every BlockType blocks.json actually defines, in enum order (skips Air,
// which has no blocks.json entry or texture of its own) - what
// ui::InventoryHud's picker grid lists.
std::vector<BlockType> all_placeable_blocks();
