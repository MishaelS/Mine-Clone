#pragma once

#include "player/Inventory.hpp"

#include <optional>
#include <vector>

// Loads assets/recipes.json (Beta 1.7.3-sourced - see the file's own
// "_notes"). Call once, after both Load_block_definitions() and
// Load_item_definitions() (needs both name tables to resolve entries). A
// recipe naming a block/item this build doesn't have yet (the file
// documents more of Beta 1.7.3 than the engine can act on - doors, saplings,
// etc.) is silently skipped, same spirit as Load_drop_table().
void Load_recipes();

// What crafting `grid` (row-major, `rows` x `cols`, empty cells as empty
// ItemStacks) currently produces, if anything - tries every loaded recipe
// whose own grid_size fits within `rows`/`cols`: a shaped recipe smaller
// than the grid is matched at every valid offset (same as real Minecraft -
// a 2x2 pattern isn't pinned to one corner of a 3x3 workbench grid), a
// shapeless one just needs the right ingredients present anywhere,
// regardless of stack size (a slot holding 5 Coal still only ever supplies
// 1 toward a recipe - see take_crafted_output()). std::nullopt if nothing
// matches.
std::optional<ItemStack> match_recipe(const std::vector<ItemStack>& grid, int rows, int cols);

// Consumes exactly 1 unit from each of `grid`'s occupied ingredient cells
// that match_recipe()'s currently-matched recipe actually used (clearing a
// cell that drops to 0) - called once when the player takes the output
// slot. Safe to call even if nothing matches (does nothing then).
void consume_recipe_ingredients(std::vector<ItemStack>& grid, int rows, int cols);
