#include "core/Block.hpp"
#include "core/Json.hpp"
#include "core/TextureAtlas.hpp"

#include <array>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace {
    std::array<BlockProperties, static_cast<size_t>(BlockType::Count)> block_table;

    // The source sprites are 4000x4000 photo-scanned textures. Downscaling
    // only to something like 256 still leaves the smooth gradients that
    // scaling bakes in, which reads as "blurry" next to genuine blocky pixel
    // art — so shrink all the way to Minecraft's actual block resolution.
    constexpr int BLOCK_TEXTURE_SIZE = 16;

    // 8x8 tiles is far more than the ~20 sprites blocks.json currently
    // references, leaving headroom for new blocks without resizing the atlas.
    constexpr int ATLAS_GRID_SIZE = 8;

    // Constructed inside Load_block_definitions() rather than at static-init
    // time, since uploading it needs a GL context that only exists once
    // InitWindow() has run — kept alive afterward so chunks can read
    // get_block_atlas_texture() at any point.
    std::unique_ptr<TextureAtlas> block_atlas;

    const std::unordered_map<std::string, BlockType> NAME_TO_TYPE = {
        {"grass",       BlockType::Grass},
        {"dirt",        BlockType::Dirt},
        {"oak_planks",  BlockType::OakPlanks},
        {"oak_log",     BlockType::OakLog},
        {"sand",        BlockType::Sand},
        {"gravel",      BlockType::Gravel},
        {"stone",       BlockType::Stone},
        {"andesite",    BlockType::Andesite},
        {"diorite",     BlockType::Diorite},
        {"granite",     BlockType::Granite},
        {"cobblestone", BlockType::Cobblestone},
        {"coal_ore",    BlockType::CoalOre},
        {"copper_ore",  BlockType::CopperOre},
        {"diamond_ore", BlockType::DiamondOre},
        {"emerald_ore", BlockType::EmeraldOre},
        {"gold_ore",    BlockType::GoldOre},
        {"iron_ore",    BlockType::IronOre},
        {"lapis_ore",   BlockType::LapisOre},
        {"bedrock",     BlockType::Bedrock},
    };
}

void Load_block_definitions()
{
    // Air: never drawn, so its texture slots are left unused.
    block_table[static_cast<uint8_t>(BlockType::Air)] = {false, true, 0, {}};

    block_atlas = std::make_unique<TextureAtlas>(BLOCK_TEXTURE_SIZE, ATLAS_GRID_SIZE);

    char* fileText = LoadFileText(ASSETS_PATH "blocks.json");
    if (fileText == nullptr) {
        throw std::runtime_error("Could not load " ASSETS_PATH "blocks.json");
    }
    Json root = Json::parse(fileText);
    UnloadFileText(fileText);

    for (const Json& entry : root["blocks"].as_array()) {
        std::string name = entry["name"].as_string();
        auto it = NAME_TO_TYPE.find(name);
        if (it == NAME_TO_TYPE.end()) {
            throw std::runtime_error("blocks.json: unknown block name '" + name + "'");
        }

        Rectangle top    = block_atlas->get_uv(entry["top"].as_string());
        Rectangle bottom = block_atlas->get_uv(entry["bottom"].as_string());
        Rectangle side   = block_atlas->get_uv(entry["side"].as_string());

        BlockProperties properties;
        properties.solid = entry["solid"].as_bool(true);
        properties.transparent = entry["transparent"].as_bool(false);
        properties.luminance = static_cast<int>(entry["luminance"].as_number(0.0));
        // Order: Top, Bottom, North, South, East, West.
        properties.texture_uvs[0] = top;
        properties.texture_uvs[1] = bottom;
        properties.texture_uvs[2] = side;
        properties.texture_uvs[3] = side;
        properties.texture_uvs[4] = side;
        properties.texture_uvs[5] = side;

        block_table[static_cast<uint8_t>(it->second)] = properties;
    }

    block_atlas->upload();
}

const BlockProperties& get_block_properties(BlockType type)
{
    return block_table[static_cast<uint8_t>(type)];
}

const Texture2D& get_block_atlas_texture()
{
    return block_atlas->texture();
}
