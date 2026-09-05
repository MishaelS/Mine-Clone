#include "core/Block.hpp"
#include "core/Json.hpp"
#include "core/TextureManager.hpp"

#include <array>
#include <stdexcept>
#include <unordered_map>

namespace {
    std::array<BlockProperties, static_cast<size_t>(BlockType::Count)> block_table;
    std::array<std::string, static_cast<size_t>(BlockType::Count)> block_names;

    constexpr const char* TERRAIN_TEXTURE_PATH = "sprites/terrain.png";

    // terrain.png is a fixed 16x16 grid of 16px tiles (256x256 total) — every
    // block face's "x"/"y" in blocks.json is a (column, row) index into it.
    constexpr int TERRAIN_GRID_SIZE = 16;
    constexpr float TERRAIN_ATLAS_PIXELS = 256.0f;

    // Tiles sit edge-to-edge with no padding between them, unlike a packed
    // atlas. Nearest-filtering can sample a neighboring tile's edge texel
    // when a UV lands exactly on 0 or 1 due to floating-point rounding, so
    // each tile's UV rect is inset by half a texel on every side to keep
    // sampling safely inside the intended tile.
    constexpr float HALF_TEXEL = 0.5f / TERRAIN_ATLAS_PIXELS;

    // Set once Load_block_definitions() has loaded terrain.png (needs a GL
    // context, so this can't happen at static-init time) — kept alive
    // afterward so chunks can read get_block_atlas_texture() at any point.
    const Texture2D* block_atlas_texture = nullptr;

    Rectangle tile_uv(int col, int row) {
        float tile = 1.0f / TERRAIN_GRID_SIZE;
        return {
            col * tile + HALF_TEXEL,
            row * tile + HALF_TEXEL,
            tile - 2.0f * HALF_TEXEL,
            tile - 2.0f * HALF_TEXEL,
        };
    }

    // One face's texture: which terrain.png tile, and the tint to multiply
    // into it (WHITE — i.e. no change — unless blocks.json gives a "color").
    struct FaceTexture {
        Rectangle uv;
        Color tint;
    };

    FaceTexture load_face_texture(const Json& face) {
        int col = static_cast<int>(face["x"].as_number(0.0));
        int row = static_cast<int>(face["y"].as_number(0.0));

        Color tint = WHITE;
        const Json& color = face["color"];
        if (color.get_type() == Json::Type::Array) {
            const std::vector<Json>& components = color.as_array();
            if (components.size() >= 3) {
                tint.r = static_cast<unsigned char>(components[0].as_number(255.0));
                tint.g = static_cast<unsigned char>(components[1].as_number(255.0));
                tint.b = static_cast<unsigned char>(components[2].as_number(255.0));
            }
        }

        return { tile_uv(col, row), tint };
    }

    const std::unordered_map<std::string, BlockType> NAME_TO_TYPE = {
        {"grass"       , BlockType::Grass      },
        {"dirt"        , BlockType::Dirt       },
        {"oak_log"     , BlockType::OakLog     },
        {"oak_planks"  , BlockType::OakPlanks  },
        {"sand"        , BlockType::Sand       },
        {"gravel"      , BlockType::Gravel     },
        {"stone"       , BlockType::Stone      },
        {"cobblestone" , BlockType::Cobblestone},
        {"iron_ore"    , BlockType::IronOre    },
        {"coal_ore"    , BlockType::CoalOre    },
        {"gold_ore"    , BlockType::GoldOre    },
        {"diamond_ore" , BlockType::DiamondOre },
        {"redstone_ore", BlockType::RedstoneOre},
        {"bedrock"     , BlockType::Bedrock    },
    };
}

void Load_block_definitions()
{
    // Air: never drawn, so its texture slots are left unused.
    block_table[static_cast<uint8_t>(BlockType::Air)] = {false, true, 0, {}, {}};
    block_names[static_cast<uint8_t>(BlockType::Air)] = "air";

    block_atlas_texture = &TextureManager::get(TERRAIN_TEXTURE_PATH);

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

        FaceTexture top    = load_face_texture(entry["top"]);
        FaceTexture bottom = load_face_texture(entry["bottom"]);
        FaceTexture side   = load_face_texture(entry["side"]);

        BlockProperties properties;
        properties.solid = entry["solid"].as_bool(true);
        properties.transparent = entry["transparent"].as_bool(false);
        properties.luminance = static_cast<int>(entry["luminance"].as_number(0.0));
        // Order: Top, Bottom, North, South, East, West.
        properties.texture_uvs[0] = top.uv;
        properties.texture_uvs[1] = bottom.uv;
        properties.texture_uvs[2] = side.uv;
        properties.texture_uvs[3] = side.uv;
        properties.texture_uvs[4] = side.uv;
        properties.texture_uvs[5] = side.uv;
        properties.texture_tints[0] = top.tint;
        properties.texture_tints[1] = bottom.tint;
        properties.texture_tints[2] = side.tint;
        properties.texture_tints[3] = side.tint;
        properties.texture_tints[4] = side.tint;
        properties.texture_tints[5] = side.tint;

        block_table[static_cast<uint8_t>(it->second)] = properties;
        block_names[static_cast<uint8_t>(it->second)] = name;
    }
}

const BlockProperties& get_block_properties(BlockType type)
{
    return block_table[static_cast<uint8_t>(type)];
}

const std::string& get_block_name(BlockType type)
{
    return block_names[static_cast<uint8_t>(type)];
}

const Texture2D& get_block_atlas_texture()
{
    return *block_atlas_texture;
}
