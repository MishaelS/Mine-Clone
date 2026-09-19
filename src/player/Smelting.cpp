#include "player/Smelting.hpp"
#include "core/Json.hpp"

#include "raylib.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    struct SmeltingRecipe {
        ItemStack input;  // count unused - one input item per smelt
        ItemStack output;
    };
    struct Fuel {
        ItemStack item;   // count unused
        int burn_ticks = 0;
    };

    int cook_ticks = 200;
    std::vector<SmeltingRecipe> recipes;
    std::vector<Fuel> fuels;

    // A smelting.json name is either a block (blocks.json) or an item
    // (items.json) - same lookup order recipes.json uses.
    std::optional<ItemStack> stack_from_name(const std::string& name, int count)
    {
        ItemStack stack;
        if (std::optional<BlockType> block = block_type_from_name(name)) {
            stack.block = *block;
        } else if (std::optional<ItemType> item = item_type_from_name(name)) {
            stack.tool = *item;
        } else {
            return std::nullopt;
        }
        stack.count = count;
        return stack;
    }

    bool same_item(const ItemStack& a, const ItemStack& b)
    {
        if (a.empty() || b.empty()) return false;
        if (a.holds_item() || b.holds_item()) return a.tool == b.tool;
        return a.block == b.block;
    }

    int max_stack(const ItemStack& stack)
    {
        return stack.is_tool() ? 1 : MAX_ITEM_STACK;
    }

    // Whether the input can be smelted right now: something smeltable is
    // in, and the output slot can take its result.
    bool can_smelt(const FurnaceState& furnace)
    {
        std::optional<ItemStack> result = smelting_result(furnace.input);
        if (!result) return false;
        if (furnace.output.empty()) return true;
        return same_item(furnace.output, *result) && furnace.output.count + result->count <= max_stack(*result);
    }

    void take_one(ItemStack& stack)
    {
        if (--stack.count <= 0) stack.clear();
    }
}

void Load_smelting()
{
    recipes.clear();
    fuels.clear();

    char* text = LoadFileText(ASSETS_PATH "smelting.json");
    if (text == nullptr) throw std::runtime_error("Could not load " ASSETS_PATH "smelting.json");
    Json root = Json::parse(text);
    UnloadFileText(text);

    cook_ticks = std::max(1, static_cast<int>(root["cook_ticks"].as_number(200)));

    for (const Json& entry : root["recipes"].as_array()) {
        std::optional<ItemStack> input = stack_from_name(entry["input"].as_string(), 1);
        std::optional<ItemStack> output = stack_from_name(entry["output"].as_string(),
            std::max(1, static_cast<int>(entry["count"].as_number(1))));
        // A name this build doesn't have (yet) skips just that recipe, same
        // as recipes.json/drops.json treat theirs.
        if (!input || !output) continue;
        recipes.push_back({*input, *output});
    }

    for (const Json& entry : root["fuels"].as_array()) {
        std::optional<ItemStack> item = stack_from_name(entry["name"].as_string(), 1);
        int burn = static_cast<int>(entry["burn_ticks"].as_number(0));
        if (!item || burn <= 0) continue;
        fuels.push_back({*item, burn});
    }
}

int smelting_cook_ticks()
{
    return cook_ticks;
}

std::optional<ItemStack> smelting_result(const ItemStack& input)
{
    if (input.empty()) return std::nullopt;
    for (const SmeltingRecipe& recipe : recipes) {
        if (same_item(recipe.input, input)) return recipe.output;
    }
    return std::nullopt;
}

int fuel_burn_ticks(const ItemStack& stack)
{
    if (stack.empty()) return 0;
    for (const Fuel& fuel : fuels) {
        if (same_item(fuel.item, stack)) return fuel.burn_ticks;
    }
    return 0;
}

bool tick_furnace(FurnaceState& furnace)
{
    bool changed = false;
    if (furnace.burn_ticks_left > 0) --furnace.burn_ticks_left;

    // Out of fuel: light the next item, but only if it would actually be
    // put to use - an idle furnace never wastes fuel.
    if (furnace.burn_ticks_left == 0 && can_smelt(furnace)) {
        int burn = fuel_burn_ticks(furnace.fuel);
        if (burn > 0) {
            furnace.burn_ticks_left = burn;
            furnace.burn_ticks_total = burn;
            take_one(furnace.fuel);
            changed = true;
        }
    }

    if (furnace.burning() && can_smelt(furnace)) {
        if (++furnace.cook_ticks >= cook_ticks) {
            furnace.cook_ticks = 0;
            ItemStack result = *smelting_result(furnace.input);
            if (furnace.output.empty()) furnace.output = result;
            else furnace.output.count += result.count;
            take_one(furnace.input);
            changed = true;
        }
    } else {
        // Vanilla resets progress the moment smelting stops (fuel out,
        // input removed, output full) rather than pausing it.
        furnace.cook_ticks = 0;
    }
    return changed;
}
