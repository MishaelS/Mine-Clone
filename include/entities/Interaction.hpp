#pragma once

#include "items/Inventory.hpp"
#include "model/EntityModel.hpp"

#include <optional>
#include <string>
#include <vector>

// How the player acts on an entity.
enum class InteractionTrigger : uint8_t {
    Hit, // left click - attacks it
    Use, // right click
};

// One rule of what happens when the player acts on a mob - shearing a
// sheep, milking a cow... Each mob keeps a list of them (Mob::
// add_interaction()); every rule matching the trigger, the item in hand and
// the mob's state is tried, each firing with its own chance. Written
// builder-style:
//
//     on_hit().with_empty_hand().unless("sheared").chance(0.2f)
//         .drops(BlockType::WhiteWool, 1, 3).then_set("sheared");
//     on_use().holding(ItemType::Bucket).gives_in_hand(ItemType::MilkBucket);
struct InteractionRule {
    InteractionTrigger trigger = InteractionTrigger::Hit;

    // What the player must hold.
    enum class Held : uint8_t { Anything, EmptyHand, Item } held = Held::Anything;
    std::optional<ItemRef> held_item; // with Held::Item

    std::string required_state; // the mob must have it ("" = no need)
    std::string blocking_state; // ...and must not have this one
    float probability = 1.0f;   // 0..1, per interaction

    // What it does when it fires.
    std::optional<ItemRef> drop; // dropped next to the mob
    int drop_min = 1, drop_max = 1;
    std::optional<ItemRef> hand_result; // one of the held items turns into this
    std::string state_to_set;
    std::string state_to_clear;

    InteractionRule& with_empty_hand() { held = Held::EmptyHand; return *this; }
    InteractionRule& holding(ItemRef item) { held = Held::Item; held_item = item; return *this; }
    InteractionRule& only_if(std::string state) { required_state = std::move(state); return *this; }
    InteractionRule& unless(std::string state) { blocking_state = std::move(state); return *this; }
    InteractionRule& chance(float value) { probability = value; return *this; }
    InteractionRule& drops(ItemRef item, int min_count = 1, int max_count = 1) { drop = item; drop_min = min_count; drop_max = max_count; return *this; }
    InteractionRule& gives_in_hand(ItemRef item) { hand_result = item; return *this; }
    InteractionRule& then_set(std::string state) { state_to_set = std::move(state); return *this; }
    InteractionRule& then_clear(std::string state) { state_to_clear = std::move(state); return *this; }

    bool holds_right_item(const ItemStack& held_stack) const
    {
        switch (held) {
        case Held::Anything: return true;
        case Held::EmptyHand: return held_stack.empty();
        case Held::Item: return !held_stack.empty() && held_item && held_item->matches(held_stack);
        }
        return false;
    }
};

inline InteractionRule on_hit() { return InteractionRule{}; }
inline InteractionRule on_use() { InteractionRule rule; rule.trigger = InteractionTrigger::Use; return rule; }

// What one interaction came to - for GameEngine to carry out.
struct InteractionResult {
    bool handled = false;            // some rule applied (a right click then does nothing else)
    std::vector<ItemStack> drops;    // dropped next to the mob
    std::optional<ItemStack> in_hand; // one held item becomes this (the rest of the stack stays)
};

// A block or item by the name /give takes ("white_wool", "bucket"...) -
// nothing for a name that's neither.
std::optional<ItemRef> item_ref_from_name(const std::string& name);

// The rule an entity's model file describes (EntityInfo::interactions). A
// rule naming an item that doesn't exist is logged and never fires.
InteractionRule rule_from_info(const EntityInteractionInfo& info);
