#include "worldgen/Structure.hpp"

#include "content/StructureFile.hpp"

#include "raylib.h"

#include <algorithm>
#include <utility>

Structure::Structure(std::vector<StructureBlock> structure_blocks)
    : blocks(std::move(structure_blocks))
{
}

bool structure_can_replace(StructureReplaceRule rule, BlockType existing)
{
    switch (rule) {
        case StructureReplaceRule::AirOnly     : return existing == BlockType::Air;
        case StructureReplaceRule::AirOrFoliage: return existing == BlockType::Air || existing == BlockType::Foliage;
        case StructureReplaceRule::Any         : return true;
    }
    return false;
}

namespace {
    std::vector<StructureDefinition> loaded_structures;

    std::optional<StructureDefinition> to_definition(const structure_file::StructureFile& file)
    {
        auto named = [&](const std::string& name) -> std::optional<BlockType> {
            std::optional<BlockType> type = block_type_from_name(name);
            if (!type) TraceLog(LOG_WARNING, "structure '%s': no block named '%s' - skipped", file.name.c_str(), name.c_str());
            return type;
        };

        StructureDefinition definition;
        definition.name = file.name;
        for (const structure_file::Variant& variant : file.variants) {
            std::vector<StructureBlock> blocks;
            for (const structure_file::Block& block : variant.blocks) {
                const std::optional<BlockType> type = named(block.block);
                if (!type) return std::nullopt;
                blocks.push_back({block.x, block.y, block.z, *type, static_cast<StructureReplaceRule>(block.replace), block.required});
            }
            if (!blocks.empty()) definition.variants.emplace_back(std::move(blocks));
        }
        if (definition.variants.empty()) return std::nullopt;

        for (const std::string& name : file.placed_on) {
            const std::optional<BlockType> type = named(name);
            if (!type) return std::nullopt;
            definition.placed_on.push_back(*type);
        }
        for (size_t b = 0; b < definition.chance.size(); ++b) definition.chance[b] = file.chance[b];
        definition.tree_density = file.tree_density;
        if (!file.grows_from.empty()) {
            definition.grows_from = named(file.grows_from);
            if (!definition.grows_from) return std::nullopt;
        }

        const structure_file::Bounds bounds = structure_file::bounds(file);
        definition.min_x = bounds.min_x;
        definition.max_x = bounds.max_x;
        definition.min_z = bounds.min_z;
        definition.max_z = bounds.max_z;
        return definition;
    }
}

void Load_structures()
{
    loaded_structures.clear();
    for (const structure_file::StructureFile& file : structure_file::load_all()) {
        if (std::optional<StructureDefinition> definition = to_definition(file)) loaded_structures.push_back(std::move(*definition));
    }
}

const std::vector<StructureDefinition>& get_structures()
{
    return loaded_structures;
}

const StructureDefinition* structure_growing_from(BlockType type)
{
    for (const StructureDefinition& definition : loaded_structures) {
        if (definition.grows_from == type) return &definition;
    }
    return nullptr;
}
