#include "worldgen/CustomWorld.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {
    std::string trim(const std::string& text)
    {
        size_t begin = text.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return {};
        size_t end = text.find_last_not_of(" \t\r\n");
        return text.substr(begin, end - begin + 1);
    }

    // "Oak Planks" -> "oak_planks": the id form of a typed ASCII name.
    std::string as_block_id(const std::string& name)
    {
        std::string id;
        for (char c : name) {
            if (c == ' ') id += '_';
            else id += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return id;
    }

    std::optional<BlockType> find_block(const std::string& name, const BlockNameLookup& lookup)
    {
        if (std::optional<BlockType> type = block_type_from_name(as_block_id(name))) return type;
        if (name == "air" || name == "воздух") return BlockType::Air;
        if (lookup) return lookup(name);
        return std::nullopt;
    }

    int rarity_for(const CustomFeatureSetting& setting, int normal_rarity)
    {
        if (!setting.enabled) return 0;
        return std::max(1, static_cast<int>(std::lround(normal_rarity * 100.0 / setting.chance)));
    }
}

float CustomWorld::amount(CustomFeature feature) const
{
    const CustomFeatureSetting& setting = (*this)[feature];
    return setting.enabled ? setting.chance / 100.0f : 0.0f;
}

void CustomWorld::resolve_column()
{
    LayerParseResult parsed = parse_custom_layers(layers);
    if (!parsed.ok()) parsed = parse_custom_layers(DEFAULT_LAYERS);
    column = std::move(parsed.column);
}

LayerParseResult parse_custom_layers(const std::string& text, const BlockNameLookup& lookup)
{
    LayerParseResult result;
    std::vector<std::pair<BlockType, int>> runs; // merged: "stone, stone" -> 2 stone

    std::string entry;
    auto finish_entry = [&]() -> bool {
        std::string item = trim(entry);
        entry.clear();
        if (item.empty()) return true;

        int count = 1;
        size_t digits = 0;
        while (digits < item.size() && std::isdigit(static_cast<unsigned char>(item[digits]))) ++digits;
        if (digits > 0) {
            if (digits > 4) {
                result.error_key = "custom.error.count";
                result.error_arg = item;
                return false;
            }
            count = std::stoi(item.substr(0, digits));
            if (count <= 0) {
                result.error_key = "custom.error.count";
                result.error_arg = item;
                return false;
            }
            std::string rest = trim(item.substr(digits));
            if (!rest.empty() && rest[0] == '*') rest = trim(rest.substr(1));
            item = rest;
        }
        if (item.empty()) {
            result.error_key = "custom.error.missing_block";
            result.error_arg = std::to_string(count);
            return false;
        }

        std::optional<BlockType> type = find_block(item, lookup);
        if (!type) {
            result.error_key = "custom.error.unknown_block";
            result.error_arg = item;
            return false;
        }
        if (!runs.empty() && runs.back().first == *type) runs.back().second += count;
        else runs.push_back({*type, count});
        return true;
    };

    for (char c : text) {
        if (c == ',' || c == ';' || c == '\n') {
            if (!finish_entry()) return result;
        } else {
            entry += c;
        }
    }
    if (!finish_entry()) return result;

    int total = 0;
    for (const auto& run : runs) total += run.second;
    if (total == 0) {
        result.error_key = "custom.error.empty";
        return result;
    }
    if (total > CUSTOM_MAX_LAYERS) {
        result.error_key = "custom.error.too_tall";
        result.error_arg = std::to_string(CUSTOM_MAX_LAYERS);
        return result;
    }

    for (const auto& [type, count] : runs) {
        result.column.insert(result.column.end(), count, type);
        if (!result.canonical.empty()) result.canonical += ", ";
        if (count > 1) result.canonical += std::to_string(count) + " ";
        result.canonical += type == BlockType::Air ? std::string("air") : get_block_name(type);
    }
    return result;
}

WorldTypeParams custom_world_params(const CustomWorld& custom)
{
    const WorldTypeParams normal = world_type_params(WorldType::Normal);
    WorldTypeParams params = normal;
    params.cave_chunk_rarity = rarity_for(custom[CustomFeature::Caves], normal.cave_chunk_rarity);
    params.ravine_chunk_rarity = rarity_for(custom[CustomFeature::Ravines], normal.ravine_chunk_rarity);
    params.tree_density = custom.amount(CustomFeature::Trees);
    params.grass_density = custom.amount(CustomFeature::Grass);
    params.ore_amount = custom.amount(CustomFeature::Ores);
    return params;
}

BiomeWeights custom_biome_weights(const BiomeWeights& land_weights, const CustomWorld& custom)
{
    BiomeWeights weights{};
    weights.plains = land_weights.plains * custom.amount(CustomFeature::Plains);
    weights.forest = land_weights.forest * custom.amount(CustomFeature::Forest);
    weights.desert = land_weights.desert * custom.amount(CustomFeature::Desert);
    weights.hills  = land_weights.hills  * custom.amount(CustomFeature::Hills);
    float total = weights.plains + weights.forest + weights.desert + weights.hills;

    // This spot would naturally be a biome that's switched off - fall back
    // to the enabled ones in proportion to their chances, so "desert only"
    // really is desert everywhere.
    if (total < 0.0001f) {
        weights.plains = custom.amount(CustomFeature::Plains);
        weights.forest = custom.amount(CustomFeature::Forest);
        weights.desert = custom.amount(CustomFeature::Desert);
        weights.hills  = custom.amount(CustomFeature::Hills);
        total = weights.plains + weights.forest + weights.desert + weights.hills;
    }
    // Every biome switched off: plain, featureless land.
    if (total < 0.0001f) {
        weights = {};
        weights.plains = 1.0f;
        return weights;
    }
    weights.plains /= total;
    weights.forest /= total;
    weights.desert /= total;
    weights.hills  /= total;
    return weights;
}
