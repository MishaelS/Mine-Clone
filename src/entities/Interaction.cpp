#include "entities/Interaction.hpp"

#include "raylib.h"

std::optional<ItemRef> item_ref_from_name(const std::string& name)
{
    if (name.empty()) return std::nullopt;
    if (std::optional<ItemType> item = item_type_from_name(name)) return ItemRef(*item);
    if (std::optional<BlockType> block = block_type_from_name(name)) return ItemRef(*block);
    return std::nullopt;
}

namespace {
    // An item a rule names - warns once per bad name instead of every click.
    std::optional<ItemRef> named_item(const std::string& name, bool& broken) {
        if (name.empty()) return std::nullopt;
        std::optional<ItemRef> item = item_ref_from_name(name);
        if (!item) {
            TraceLog(LOG_WARNING, "entity interaction: no block or item named '%s'", name.c_str());
            broken = true;
        }
        return item;
    }
}

InteractionRule rule_from_info(const EntityInteractionInfo& info)
{
    InteractionRule rule = info.trigger == EntityTrigger::Hit ? on_hit() : on_use();
    bool broken = false;
    if (info.held == EntityHeld::EmptyHand) rule.with_empty_hand();
    if (info.held == EntityHeld::Item) {
        if (std::optional<ItemRef> held = named_item(info.held_item, broken)) rule.holding(*held);
        else broken = true;
    }
    rule.only_if(info.required_state).unless(info.blocking_state).chance(info.chance);
    if (std::optional<ItemRef> drop = named_item(info.drop_item, broken)) rule.drops(*drop, info.drop_min, info.drop_max);
    if (std::optional<ItemRef> result = named_item(info.hand_result, broken)) rule.gives_in_hand(*result);
    rule.then_set(info.set_state).then_clear(info.clear_state);
    if (broken) rule.chance(0.0f); // never fires - but still takes the click, so nothing odd happens instead
    return rule;
}
