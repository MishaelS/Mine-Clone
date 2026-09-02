#include "core/Block.hpp"
#include "core/Json.hpp"
#include "core/TextureManager.hpp"

#include <array>
#include <stdexcept>
#include <unordered_map>

namespace {
    std::array<BlockProperties, static_cast<size_t>(BlockType::Count)> block_table;

    // The source sprites are 4000x4000 photo-scanned textures. Downscaling
    // only to something like 256 still leaves the smooth gradients that
    // scaling bakes in, which reads as "blurry" next to genuine blocky pixel
    // art — so shrink all the way to Minecraft's actual block resolution.
    constexpr int BLOCK_TEXTURE_SIZE = 16;

    const Texture2D& GetBlockTexture(const std::string& file_name) {
        return TextureManager::get_resized("sprites/" + file_name, BLOCK_TEXTURE_SIZE);
    }

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

        const Texture2D& top    = GetBlockTexture(entry["top"].as_string());
        const Texture2D& bottom = GetBlockTexture(entry["bottom"].as_string());
        const Texture2D& side   = GetBlockTexture(entry["side"].as_string());

        BlockProperties properties;
        properties.solid = entry["solid"].as_bool(true);
        properties.transparent = entry["transparent"].as_bool(false);
        properties.luminance = static_cast<int>(entry["luminance"].as_number(0.0));
        // Order: Top, Bottom, North, South, East, West.
        properties.textures[0] = top;
        properties.textures[1] = bottom;
        properties.textures[2] = side;
        properties.textures[3] = side;
        properties.textures[4] = side;
        properties.textures[5] = side;

        block_table[static_cast<uint8_t>(it->second)] = properties;
    }
}

const BlockProperties& get_block_properties(BlockType type)
{
    return block_table[static_cast<uint8_t>(type)];
}
