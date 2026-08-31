#include "Block.hpp"
#include "BlockTextures.hpp"
#include "Json.hpp"

#include <array>
#include <stdexcept>
#include <unordered_map>

namespace {
    std::array<BlockProperties, static_cast<size_t>(BlockType::Count)> blockTable;

    const std::unordered_map<std::string, BlockType> NAME_TO_TYPE = {
        {"grass",       BlockType::Grass},
        {"dirt",        BlockType::Dirt},
        {"gravel",      BlockType::Gravel},
        {"stone",       BlockType::Stone},
        {"andesite",    BlockType::Andesite},
        {"diorite",     BlockType::Diorite},
        {"granite",     BlockType::Granite},
        {"cobblestone", BlockType::Cobblestone},
        {"bedrock",     BlockType::Bedrock},
    };
}

void LoadBlockDefinitions() {
    // Air: never drawn, so its texture slots are left unused.
    blockTable[static_cast<uint8_t>(BlockType::Air)] = {false, true, 0, {}};

    char* fileText = LoadFileText(ASSETS_PATH "blocks.json");
    if (fileText == nullptr) {
        throw std::runtime_error("Could not load " ASSETS_PATH "blocks.json");
    }
    Json root = Json::Parse(fileText);
    UnloadFileText(fileText);

    for (const Json& entry : root["blocks"].AsArray()) {
        std::string name = entry["name"].AsString();
        auto it = NAME_TO_TYPE.find(name);
        if (it == NAME_TO_TYPE.end()) {
            throw std::runtime_error("blocks.json: unknown block name '" + name + "'");
        }

        const Texture2D& top    = GetBlockTexture(entry["top"].AsString());
        const Texture2D& bottom = GetBlockTexture(entry["bottom"].AsString());
        const Texture2D& side   = GetBlockTexture(entry["side"].AsString());

        BlockProperties properties;
        properties.solid = entry["solid"].AsBool(true);
        properties.transparent = entry["transparent"].AsBool(false);
        properties.luminance = static_cast<int>(entry["luminance"].AsNumber(0.0));
        // Order: Top, Bottom, North, South, East, West.
        properties.textures[0] = top;
        properties.textures[1] = bottom;
        properties.textures[2] = side;
        properties.textures[3] = side;
        properties.textures[4] = side;
        properties.textures[5] = side;

        blockTable[static_cast<uint8_t>(it->second)] = properties;
    }
}

const BlockProperties& GetBlockProperties(BlockType type) {
    return blockTable[static_cast<uint8_t>(type)];
}
