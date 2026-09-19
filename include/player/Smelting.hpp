#pragma once

#include "player/Inventory.hpp"

#include <optional>

// Furnace smelting - which input turns into what, what burns as fuel and
// for how long, and the per-tick furnace simulation itself. All data comes
// from assets/smelting.json (Minecraft Beta 1.7.3 rules): one input item
// cooks for smelting_cook_ticks() game ticks into one output item; fuel
// burns one item at a time for its own burn_ticks.

// Everything one furnace holds and is doing - its three slots plus burn/
// cook progress. Owned per furnace position by World (World::
// furnace_state()), shown and edited by InventoryHud, persisted by
// WorldSave.
struct FurnaceState {
    ItemStack input;
    ItemStack fuel;
    ItemStack output;
    int burn_ticks_left = 0;   // of the fuel item currently burning; > 0 = lit
    int burn_ticks_total = 0;  // that fuel item's full burn time (for the flame icon)
    int cook_ticks = 0;        // progress on the current input item, 0..smelting_cook_ticks()

    bool burning() const { return burn_ticks_left > 0; }
    bool empty() const { return input.empty() && fuel.empty() && output.empty() && !burning(); }
};

// Parses assets/smelting.json. Call once at startup, after the block/item
// tables are loaded (names resolve against both).
void Load_smelting();

int smelting_cook_ticks();

// What one of `input` smelts into (count as smelting.json gives it), or
// std::nullopt if it isn't smeltable.
std::optional<ItemStack> smelting_result(const ItemStack& input);

// How many ticks one of `stack` burns for - 0 if it isn't fuel.
int fuel_burn_ticks(const ItemStack& stack);

// Advances one furnace by exactly one game tick, vanilla's own order: the
// current fuel burns down; when it's out and there's something it can
// smelt, the next fuel item is lit; while lit and smelting, cook progress
// advances and every smelting_cook_ticks() one input becomes one output.
// Fuel that's already lit keeps burning out even with nothing to smelt.
// Returns true if any slot's contents changed this tick.
bool tick_furnace(FurnaceState& furnace);
