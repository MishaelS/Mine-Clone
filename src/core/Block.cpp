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

    // terrain.png is a 16x16 grid of 16px tiles (256x256 pixels total) —
    // every block face's "x"/"y" in blocks.json is a tile's column/row in
    // that grid.
    constexpr int TILE_PIXELS = 16;   // one tile's width/height, in pixels
    constexpr int GRID_TILES  = 16;   // tiles per row/column in terrain.png
    constexpr float ATLAS_PIXELS = static_cast<float>(TILE_PIXELS * GRID_TILES); // 256

    // Set once Load_block_definitions() has loaded terrain.png (needs a GL
    // context, so this can't happen at static-init time) — kept alive
    // afterward so chunks can read get_block_atlas_texture() at any point.
    const Texture2D* block_atlas_texture = nullptr;

    // Turns a tile's (column, row) into the UV rectangle (0..1) raylib/OpenGL
    // expects. The tile's position and size stay whole pixels right up to
    // the last step — the division by ATLAS_PIXELS — which is unavoidable:
    // the GPU only understands texture coordinates normalized to 0..1,
    // whatever the texture's actual pixel size.
    Rectangle tile_uv(int col, int row) {
        int x = col * TILE_PIXELS;
        int y = row * TILE_PIXELS;
        return {
            x / ATLAS_PIXELS,
            y / ATLAS_PIXELS,
            TILE_PIXELS / ATLAS_PIXELS,
            TILE_PIXELS / ATLAS_PIXELS,
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
        {"water"       , BlockType::Water      },
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
