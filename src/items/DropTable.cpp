#include "items/DropTable.hpp"
#include "content/Content.hpp"
#include "items/Item.hpp"

#include "raylib.h"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>

namespace {
    struct DropEntry {
        ItemRef what;
        int count_min = 1;
        int count_max = 1;
        float chance = 1.0f;
    };

    struct BlockDropRule {
        ToolKind requires_tool = ToolKind::None;
        int min_tier = ToolTier::None; // see ItemProperties::tier
        std::vector<DropEntry> drops;
    };

    std::array<std::optional<BlockDropRule>, static_cast<size_t>(BlockType::Count)> drop_table;

    BlockDropRule& rule_for(BlockType block) {
        return *drop_table[static_cast<size_t>(block)];
    }
}

namespace content {

    DropRule when_broken(BlockType block) {
        std::optional<BlockDropRule>& rule = drop_table[static_cast<size_t>(block)];
        if (rule) throw std::runtime_error("drop rule defined twice for block '" + get_block_name(block) + "'");
        rule.emplace();
        return DropRule(block);
    }

    DropRule& DropRule::needs(ToolKind kind, int min_tier) {
        BlockDropRule& rule = rule_for(block_);
        rule.requires_tool = kind;
        rule.min_tier = min_tier;
        return *this;
    }

    DropRule& DropRule::drop(ItemRef what, int count) {
        return drop(what, count, count);
    }

    DropRule& DropRule::drop(ItemRef what, int count_min, int count_max) {
        DropEntry entry{what};
        entry.count_min = std::max(1, count_min);
        entry.count_max = std::max(entry.count_min, count_max);
        rule_for(block_).drops.push_back(entry);
        return *this;
    }

    DropRule& DropRule::chance(float probability) {
        BlockDropRule& rule = rule_for(block_);
        if (rule.drops.empty()) {
            throw std::runtime_error("drop rule for '" + get_block_name(block_) + "': chance() before any drop()");
        }
        rule.drops.back().chance = probability;
        return *this;
    }

} // namespace content

void Load_drop_table()
{
    content::register_drops();
}

bool can_harvest_block(BlockType type, const ItemStack& selected)
{
    const std::optional<BlockDropRule>& rule = drop_table[static_cast<size_t>(type)];
    // No table entry, or no tool requirement at all - always harvestable,
    // matching resolve_block_drops()'s own "no entry -> drops itself"
    // fallback (that fallback never withholds a drop for lacking a tool).
    if (!rule || rule->requires_tool == ToolKind::None) return true;

    bool wielding_right_kind = selected.is_tool() &&
        get_item_properties(selected.tool).tool_kind == rule->requires_tool;
    if (!wielding_right_kind) return false;
    if (rule->min_tier > 0 && get_item_properties(selected.tool).tier < rule->min_tier) return false;
    return true;
}

std::vector<DropRoll> resolve_block_drops(BlockType type, const ItemStack& selected)
{
    const std::optional<BlockDropRule>& rule = drop_table[static_cast<size_t>(type)];

    // No rule at all (see content::when_broken()) - the block drops
    // itself, rather than silently yielding nothing.
    if (!rule) return {{false, type, ItemType::None, 1}};

    if (!can_harvest_block(type, selected)) return {};

    std::vector<DropRoll> result;
    for (const DropEntry& entry : rule->drops) {
        float roll = static_cast<float>(GetRandomValue(0, 100000)) / 100000.0f;
        if (roll > entry.chance) continue;
        int count = entry.count_min == entry.count_max
            ? entry.count_min
            : GetRandomValue(entry.count_min, entry.count_max);
        DropRoll drop;
        drop.is_item = entry.what.is_item();
        drop.block = entry.what.block;
        drop.item = entry.what.item;
        drop.count = count;
        result.push_back(drop);
    }
    return result;
}
