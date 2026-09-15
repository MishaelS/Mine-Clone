#include "player/DropTable.hpp"
#include "player/Item.hpp"
#include "core/Json.hpp"

#include "raylib.h"

#include <array>
#include <optional>

namespace {
    struct DropEntry {
        bool is_item = false;
        BlockType block = BlockType::Air;
        ItemType item = ItemType::None;
        int count_min = 1;
        int count_max = 1;
        float chance = 1.0f;
    };

    struct BlockDropRule {
        ToolKind requires_tool = ToolKind::None;
        int min_tier = 0; // 0 = no minimum; see ItemProperties::tier
        std::vector<DropEntry> drops;
    };

    std::array<std::optional<BlockDropRule>, static_cast<size_t>(BlockType::Count)> drop_table;

    ToolKind tool_kind_from_name(const std::string& name)
    {
        if (name == "pickaxe") return ToolKind::Pickaxe;
        if (name == "axe") return ToolKind::Axe;
        if (name == "shovel") return ToolKind::Shovel;
        if (name == "sword") return ToolKind::Sword;
        if (name == "hoe") return ToolKind::Hoe;
        return ToolKind::None;
    }

    // Mining-level rank matching ItemProperties::tier (Gold == Wood, both
    // rank 1 - see that field's own comment).
    int tier_from_name(const std::string& name)
    {
        if (name == "wood" || name == "gold") return 1;
        if (name == "stone") return 2;
        if (name == "iron") return 3;
        if (name == "diamond") return 4;
        return 0;
    }
}

void Load_drop_table()
{
    char* fileText = LoadFileText(ASSETS_PATH "drops.json");
    if (fileText == nullptr) {
        throw std::runtime_error("Could not load " ASSETS_PATH "drops.json");
    }
    Json root = Json::parse(fileText);
    UnloadFileText(fileText);

    for (const Json& entry : root["drops"].as_array()) {
        std::optional<BlockType> block_type = block_type_from_name(entry["block"].as_string());
        if (!block_type) continue; // not a block this build has yet - skip (see header comment)

        BlockDropRule rule;
        rule.requires_tool = tool_kind_from_name(entry["requires_tool"].as_string());
        rule.min_tier = tier_from_name(entry["min_tool_tier"].as_string());

        for (const Json& drop : entry["drops"].as_array()) {
            DropEntry parsed;
            std::string name = drop["name"].as_string();
            if (drop["type"].as_string() == "item") {
                std::optional<ItemType> item_type = item_type_from_name(name);
                if (!item_type) continue; // not an item this build has yet - skip
                parsed.is_item = true;
                parsed.item = *item_type;
            } else {
                std::optional<BlockType> drop_block = block_type_from_name(name);
                if (!drop_block) continue;
                parsed.block = *drop_block;
            }
            parsed.count_min = std::max(1, static_cast<int>(drop["count_min"].as_number(1)));
            parsed.count_max = std::max(parsed.count_min, static_cast<int>(drop["count_max"].as_number(parsed.count_min)));
            parsed.chance = static_cast<float>(drop["chance"].as_number(1.0));
            rule.drops.push_back(parsed);
        }

        drop_table[static_cast<size_t>(*block_type)] = std::move(rule);
    }
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

    // No table entry at all - not yet covered by drops.json, or its name
    // didn't resolve - falls back to the old "drops itself" default rather
    // than silently yielding nothing.
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
        drop.is_item = entry.is_item;
        drop.block = entry.block;
        drop.item = entry.item;
        drop.count = count;
        result.push_back(drop);
    }
    return result;
}
