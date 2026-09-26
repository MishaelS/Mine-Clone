#include "player/Smelting.hpp"
#include "content/Content.hpp"

#include <algorithm>
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

    // Vanilla: every recipe takes 10 seconds (20 ticks per second) - one
    // coal (1600) smelts 8 items, one plank/log (300) 1.5 items.
    constexpr int COOK_TICKS = 200;
    std::vector<SmeltingRecipe> recipes;
    std::vector<Fuel> fuels;

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

namespace content {

void smelt(ItemRef input, ItemRef output, int count)
{
    recipes.push_back({input.stack(1), output.stack(std::max(1, count))});
}

void fuel(ItemRef what, int burn_ticks)
{
    if (burn_ticks > 0) fuels.push_back({what.stack(1), burn_ticks});
}

} // namespace content

void Load_smelting()
{
    recipes.clear();
    fuels.clear();
    content::register_smelting();
}

int smelting_cook_ticks()
{
    return COOK_TICKS;
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
        if (++furnace.cook_ticks >= COOK_TICKS) {
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
