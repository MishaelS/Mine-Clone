#include "core/Block.hpp"
#include "core/Json.hpp"
#include "core/TextureManager.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace {
    std::array<BlockProperties, static_cast<size_t>(BlockType::Count)> block_table;
    std::array<std::string, static_cast<size_t>(BlockType::Count)> block_names;

    constexpr const char* TERRAIN_TEXTURE_PATH = "sprites/terrain.png";

    // terrain.png is a 16x16 grid of 16px tiles (256x256 pixels total) -
    // every block face's "x"/"y" in blocks.json is a tile's column/row in
    // that grid.
    constexpr int TILE_PIXELS = 16;   // one tile's width/height, in pixels
    constexpr int GRID_TILES  = 16;   // tiles per row/column in terrain.png
    constexpr float ATLAS_PIXELS = static_cast<float>(TILE_PIXELS * GRID_TILES); // 256

    // Set once Load_block_definitions() has loaded terrain.png (needs a GL
    // context, so this can't happen at static-init time) - kept alive
    // afterward so chunks can read get_block_atlas_texture() at any point.
    const Texture2D* block_atlas_texture = nullptr;

    // Turns a tile's (column, row) into the UV rectangle (0..1) raylib/OpenGL
    // expects. The tile's position and size stay whole pixels right up to
    // the last step - the division by ATLAS_PIXELS - which is unavoidable:
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

    // Sound group already sorts every block into a rough material family -
    // reused here as the *default* physical properties (hardness/tool/
    // density) for a block whose blocks.json entry doesn't override them,
    // instead of a second, separate classification. Values are deliberately
    // approximate ("wood floats, stone sinks, stone needs a pickaxe") -
    // this project isn't chasing exact vanilla hardness numbers, just
    // plausible relative ones; blocks.json's own optional "hardness"/
    // "tool"/"density" fields (see below) still win for anything worth
    // tuning individually.
    struct PhysicalDefaults { float hardness; ToolKind tool; float density; };

    PhysicalDefaults physical_defaults_for(BlockSoundGroup group) {
        switch (group) {
            case BlockSoundGroup::Stone:   return {1.5f, ToolKind::Pickaxe, 2.7f};
            case BlockSoundGroup::Metal:   return {3.0f, ToolKind::Pickaxe, 5.0f};
            case BlockSoundGroup::Wood:    return {1.0f, ToolKind::Axe,     0.6f};
            case BlockSoundGroup::Grass:   return {0.5f, ToolKind::Shovel,  1.4f};
            case BlockSoundGroup::Dirt:    return {0.5f, ToolKind::Shovel,  1.4f};
            case BlockSoundGroup::Sand:    return {0.5f, ToolKind::Shovel,  1.5f};
            case BlockSoundGroup::Gravel:  return {0.5f, ToolKind::Shovel,  1.8f};
            case BlockSoundGroup::Snow:    return {0.5f, ToolKind::Shovel,  0.3f};
            case BlockSoundGroup::Glass:   return {0.3f, ToolKind::None,    2.4f};
            case BlockSoundGroup::Cloth:   return {0.3f, ToolKind::None,    0.4f};
            case BlockSoundGroup::Foliage: return {0.3f, ToolKind::None,    0.3f};
            default:                       return {0.3f, ToolKind::None,    1.0f};
        }
    }

    ToolKind tool_kind_from_name(const std::string& name) {
        if (name == "pickaxe") return ToolKind::Pickaxe;
        if (name == "shovel")  return ToolKind::Shovel;
        if (name == "axe")     return ToolKind::Axe;
        if (name == "sword")   return ToolKind::Sword;
        if (name == "hoe")     return ToolKind::Hoe;
        return ToolKind::None;
    }

    // One face's texture: which terrain.png tile, and the tint to multiply
    // into it (WHITE - i.e. no change - unless blocks.json gives a "color").
    // tint.a is this face's opacity for translucent blocks (see
    // BlockProperties::translucent) - 255 (fully opaque) unless blocks.json
    // gives a 4th "color" component; meaningless for an opaque block, which
    // never blends.
    struct FaceTexture {
        Rectangle uv;
        Color tint;
    };

    FaceTexture load_face_texture(const Json& face) {
        int col = static_cast<int>(face["x"].as_number(0.0));
        int row = static_cast<int>(face["y"].as_number(0.0));
        if (col < 0 || col >= GRID_TILES || row < 0 || row >= GRID_TILES) {
            throw std::runtime_error("blocks.json: terrain tile coordinates must be in range 0..15");
        }

        Color tint = WHITE;
        const Json& color = face["color"];
        if (color.get_type() == Json::Type::Array) {
            const std::vector<Json>& components = color.as_array();
            if (components.size() >= 3) {
                tint.r = static_cast<unsigned char>(components[0].as_number(255.0));
                tint.g = static_cast<unsigned char>(components[1].as_number(255.0));
                tint.b = static_cast<unsigned char>(components[2].as_number(255.0));
            }
            if (components.size() >= 4) {
                tint.a = static_cast<unsigned char>(components[3].as_number(255.0));
            }
        }

        return { tile_uv(col, row), tint };
    }

    const std::unordered_map<std::string, BlockType> NAME_TO_TYPE = {
        {"grass"            , BlockType::Grass           },
        {"dirt"             , BlockType::Dirt            },
        {"foliage"          , BlockType::Foliage         },
        {"oak_log"          , BlockType::OakLog          },
        {"oak_planks"       , BlockType::OakPlanks       },
        {"sand"             , BlockType::Sand            },
        {"gravel"           , BlockType::Gravel          },
        {"clay"             , BlockType::Clay            },
        {"stone"            , BlockType::Stone           },
        {"cobblestone"      , BlockType::Cobblestone     },
        {"iron_ore"         , BlockType::IronOre         },
        {"coal_ore"         , BlockType::CoalOre         },
        {"gold_ore"         , BlockType::GoldOre         },
        {"diamond_ore"      , BlockType::DiamondOre      },
        {"redstone_ore"     , BlockType::RedstoneOre     },
        {"bedrock"          , BlockType::Bedrock         },
        {"water"            , BlockType::Water           },
        {"workbench"        , BlockType::Workbench       },
        {"glass"            , BlockType::Glass           },
        {"ice"              , BlockType::Ice             },
        {"double_stone_slab", BlockType::DoubleStoneSlab },
        {"bricks"           , BlockType::Bricks          },
        {"tnt"              , BlockType::Tnt             },
        {"iron_block"       , BlockType::IronBlock       },
        {"gold_block"       , BlockType::GoldBlock       },
        {"diamond_block"    , BlockType::DiamondBlock    },
        {"chest"            , BlockType::Chest           },
        {"bookshelf"        , BlockType::Bookshelf       },
        {"mossy_cobblestone", BlockType::MossyCobblestone},
        {"obsidian"         , BlockType::Obsidian        },
        {"sponge"           , BlockType::Sponge          },
        {"white_wool"       , BlockType::WhiteWool       },
        {"mob_spawner"      , BlockType::MobSpawner      },
        {"snow_block"       , BlockType::SnowBlock       },
        {"snowy_grass"      , BlockType::SnowyGrass      },
        {"cactus"           , BlockType::Cactus          },
        {"note_block"       , BlockType::NoteBlock       },
        {"jukebox"          , BlockType::Jukebox         },
        {"furnace"          , BlockType::Furnace         },
        {"lit_furnace"      , BlockType::LitFurnace      },
        {"dispenser"        , BlockType::Dispenser       },
        {"netherrack"       , BlockType::Netherrack      },
        {"soul_sand"        , BlockType::SoulSand        },
        {"glowstone"        , BlockType::Glowstone       },
        {"piston"           , BlockType::Piston          },
        {"sticky_piston"    , BlockType::StickyPiston    },
        {"spruce_log"       , BlockType::SpruceLog       },
        {"birch_log"        , BlockType::BirchLog        },
        {"pumpkin"          , BlockType::Pumpkin         },
        {"jack_o_lantern"   , BlockType::JackOLantern    },
        {"spruce_foliage"   , BlockType::SpruceFoliage   },
        {"birch_foliage"    , BlockType::BirchFoliage    },
        {"lapis_block"      , BlockType::LapisBlock      },
        {"lapis_ore"        , BlockType::LapisOre        },
        {"sandstone"        , BlockType::Sandstone       },
        {"black_wool"       , BlockType::BlackWool       },
        {"gray_wool"        , BlockType::GrayWool        },
        {"red_wool"         , BlockType::RedWool         },
        {"pink_wool"        , BlockType::PinkWool        },
        {"green_wool"       , BlockType::GreenWool       },
        {"lime_wool"        , BlockType::LimeWool        },
        {"brown_wool"       , BlockType::BrownWool       },
        {"yellow_wool"      , BlockType::YellowWool      },
        {"blue_wool"        , BlockType::BlueWool        },
        {"light_blue_wool"  , BlockType::LightBlueWool   },
        {"purple_wool"      , BlockType::PurpleWool      },
        {"magenta_wool"     , BlockType::MagentaWool     },
        {"cyan_wool"        , BlockType::CyanWool        },
        {"orange_wool"      , BlockType::OrangeWool      },
        {"light_gray_wool"  , BlockType::LightGrayWool   },
        {"lava"             , BlockType::Lava            },
        {"short_grass"      , BlockType::ShortGrass      },
        {"oak_sapling"      , BlockType::OakSapling      },
        {"torch"            , BlockType::Torch           },
        {"redstone_torch"   , BlockType::RedstoneTorch   },
        {"lit_redstone_torch", BlockType::LitRedstoneTorch},
        {"oak_stairs"       , BlockType::OakStairs       },
        {"oak_trapdoor"     , BlockType::OakTrapdoor     },
        {"oak_door_lower"   , BlockType::OakDoorLower    },
        {"oak_door_upper"   , BlockType::OakDoorUpper    },
        {"iron_door_lower"  , BlockType::IronDoorLower   },
        {"iron_door_upper"  , BlockType::IronDoorUpper   },
        {"bed_head"         , BlockType::BedHead         },
        {"bed_foot"         , BlockType::BedFoot         },
        {"cake"             , BlockType::Cake            },
        {"oak_slab"         , BlockType::OakSlab         },
    };

    BlockSoundGroup sound_group_from_name(const std::string& name) {
        if (name == "grass"  ) return BlockSoundGroup::Grass;
        if (name == "dirt"   ) return BlockSoundGroup::Dirt;
        if (name == "gravel" ) return BlockSoundGroup::Gravel;
        if (name == "wood"   ) return BlockSoundGroup::Wood;
        if (name == "sand"   ) return BlockSoundGroup::Sand;
        if (name == "snow"   ) return BlockSoundGroup::Snow;
        if (name == "glass"  ) return BlockSoundGroup::Glass;
        if (name == "cloth"  ) return BlockSoundGroup::Cloth;
        if (name == "foliage") return BlockSoundGroup::Foliage;
        if (name == "metal"  ) return BlockSoundGroup::Metal;
        return
            name == "stone" ? BlockSoundGroup::Stone : BlockSoundGroup::None;
    }
}

void Load_block_definitions()
{
    // Air: never drawn, so its texture slots are left unused. Named-field
    // assignment (not a positional aggregate-init brace list) so adding a
    // BlockProperties member later can't silently shift which literal
    // lands in which field.
    {
        BlockProperties& air = block_table[static_cast<uint8_t>(BlockType::Air)];
        air                 = BlockProperties{};
        air.solid           = false;
        air.transparent     = true;
        air.selectable      = false;
        air.replaceable     = true;
        air.render_shape    = BlockRenderShape::Cube;
        air.cull_same_faces = true;
        air.sound_group     = BlockSoundGroup::None;
        air.hardness        = 0.0f;
        air.effective_tool  = ToolKind::None;
        air.density         = 0.0f; // never dropped/simulated - air has no falling/buoyancy meaning
    }
    block_names[static_cast<uint8_t>(BlockType::Air)] = "air";

    block_atlas_texture = &TextureManager::get(TERRAIN_TEXTURE_PATH);
    if (block_atlas_texture->width != TILE_PIXELS * GRID_TILES ||
        block_atlas_texture->height != TILE_PIXELS * GRID_TILES) {
        throw std::runtime_error(
            "sprites/terrain.png must be exactly 256x256 pixels "
            "(16x16 tiles, each tile 16x16 pixels)");
    }

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
        size_t block_index = static_cast<size_t>(it->second);
        if (!block_names[block_index].empty()) {
            throw std::runtime_error("blocks.json: duplicate block name '" + name + "'");
        }

        FaceTexture top    = load_face_texture(entry["top"]);
        FaceTexture bottom = load_face_texture(entry["bottom"]);
        FaceTexture side   = load_face_texture(entry["side"]);
        auto optional_face = [&entry, &side](const char* key) {
            const Json& value = entry[key];
            return value.get_type() == Json::Type::Object ? load_face_texture(value) : side;
        };
        FaceTexture north = optional_face("north");
        FaceTexture south = optional_face("south");
        FaceTexture east  = optional_face("east");
        FaceTexture west  = optional_face("west");

        BlockProperties properties;
        properties.solid           = entry["solid"].as_bool(true);
        properties.transparent     = entry["transparent"].as_bool(false);
        properties.selectable      = entry["selectable"].as_bool(true);
        properties.replaceable     = entry["replaceable"].as_bool(!properties.solid);
        std::string render_shape_name = entry["render_shape"].as_string();
        properties.render_shape = render_shape_name == "cross" ? BlockRenderShape::Cross
            : render_shape_name == "shaped" ? BlockRenderShape::Shaped
            : BlockRenderShape::Cube;
        properties.translucent     = entry["translucent"].as_bool(false);
        properties.cutout          = entry["cutout"].as_bool(false);
        properties.cull_same_faces = entry["cull_same_faces"].as_bool(true);
        properties.sound_group     = sound_group_from_name(entry["sound"].as_string("stone"));
        properties.luminance       = static_cast<int>(entry["luminance"].as_number(0.0));

        PhysicalDefaults physical_defaults = physical_defaults_for(properties.sound_group);
        properties.hardness = static_cast<float>(entry["hardness"].as_number(physical_defaults.hardness));
        properties.effective_tool = entry["tool"].get_type() == Json::Type::String
            ? tool_kind_from_name(entry["tool"].as_string()) : physical_defaults.tool;
        properties.density = static_cast<float>(entry["density"].as_number(physical_defaults.density));
        properties.has_custom_shape = entry["custom_shape"].as_bool(false);
        properties.damages_on_touch = entry["damages_on_touch"].as_bool(false);
        const std::vector<Json>& attach = entry["attach"].as_array();
        for (const Json& face : attach) {
            const std::string name = face.as_string();
            if (name == "floor") properties.attach_floor = true;
            else if (name == "wall") properties.attach_wall = true;
            else if (name == "ceiling") properties.attach_ceiling = true;
            else throw std::runtime_error("blocks.json: unknown \"attach\" value '" + name + "'");
        }
        properties.side_inset = static_cast<float>(entry["side_inset"].as_number(0.0)) / static_cast<float>(TILE_PIXELS);
        // Order: Top, Bottom, North, South, East, West.
        properties.texture_uvs[0] = top.uv;
        properties.texture_uvs[1] = bottom.uv;
        properties.texture_uvs[2] = north.uv;
        properties.texture_uvs[3] = south.uv;
        properties.texture_uvs[4] = east.uv;
        properties.texture_uvs[5] = west.uv;
        properties.texture_tints[0] = top.tint;
        properties.texture_tints[1] = bottom.tint;
        properties.texture_tints[2] = north.tint;
        properties.texture_tints[3] = south.tint;
        properties.texture_tints[4] = east.tint;
        properties.texture_tints[5] = west.tint;
        if (entry["cut"].get_type() == Json::Type::Object) {
            properties.cut_texture_uv = load_face_texture(entry["cut"]).uv;
        }
        if (entry["end"].get_type() == Json::Type::Object) {
            properties.end_texture_uv = load_face_texture(entry["end"]).uv;
        }

        block_table[block_index] = properties;
        block_names[block_index] = name;
    }

    for (size_t i = 1; i < block_names.size(); ++i) {
        if (block_names[i].empty()) {
            throw std::runtime_error("blocks.json: definition missing for BlockType id " + std::to_string(i));
        }
    }
}

const BlockProperties& get_block_properties(BlockType type)
{
    return block_table[static_cast<uint8_t>(type)];
}

bool block_is_directional(BlockType type)
{
    return type == BlockType::Chest     || type == BlockType::Furnace || type == BlockType::LitFurnace ||
           type == BlockType::Workbench || type == BlockType::Dispenser ||
           type == BlockType::Pumpkin || type == BlockType::JackOLantern;
}

bool block_needs_facing(BlockType type)
{
    return block_is_directional(type) || type == BlockType::OakStairs || type == BlockType::OakTrapdoor;
}

FaceOffset block_face_offset(BlockFace face)
{
    switch (face) {
        case BlockFace::Top:    return {0, 1, 0};
        case BlockFace::Bottom: return {0, -1, 0};
        case BlockFace::North:  return {0, 0, -1};
        case BlockFace::South:  return {0, 0, 1};
        case BlockFace::East:   return {1, 0, 0};
        case BlockFace::West:   return {-1, 0, 0};
    }
    return {0, -1, 0};
}

bool block_is_attachable(BlockType type)
{
    const BlockProperties& properties = get_block_properties(type);
    return properties.attach_floor || properties.attach_wall || properties.attach_ceiling;
}

DirectionOffset horizontal_direction_offset(HorizontalDirection direction)
{
    switch (direction) {
        case HorizontalDirection::North: return {0, -1};
        case HorizontalDirection::South: return {0, 1};
        case HorizontalDirection::East:  return {1, 0};
        case HorizontalDirection::West:  return {-1, 0};
    }
    return {0, 1};
}

HorizontalDirection horizontal_direction_right_of(HorizontalDirection direction)
{
    switch (direction) {
        case HorizontalDirection::North: return HorizontalDirection::East;
        case HorizontalDirection::East:  return HorizontalDirection::South;
        case HorizontalDirection::South: return HorizontalDirection::West;
        case HorizontalDirection::West:  return HorizontalDirection::North;
    }
    return HorizontalDirection::East;
}

const std::string& get_block_name(BlockType type)
{
    return block_names[static_cast<uint8_t>(type)];
}

std::optional<BlockType> block_type_from_name(const std::string& name)
{
    auto it = NAME_TO_TYPE.find(name);
    if (it == NAME_TO_TYPE.end()) return std::nullopt;
    return it->second;
}

const Texture2D& get_block_atlas_texture()
{
    return *block_atlas_texture;
}

Rectangle get_sample_safe_block_uv(Rectangle uv)
{
    const Texture2D& atlas = get_block_atlas_texture();
    // Keep the coordinates just inside their tile so floating-point
    // rounding can never select the neighbouring tile.  The old half-texel
    // inset mapped the centres of texels 0..15 onto the complete face.  With
    // point sampling that makes the first/last source pixels half as wide as
    // the other fourteen, which is why the 16x16 artwork looked uneven on a
    // one-block face.  A tiny sub-texel inset preserves sixteen equal pixel
    // columns/rows while still keeping the shared atlas boundary exclusive.
    constexpr float SUB_TEXEL_INSET = 1.0f / 1024.0f;
    float inset_u = SUB_TEXEL_INSET / static_cast<float>(atlas.width);
    float inset_v = SUB_TEXEL_INSET / static_cast<float>(atlas.height);
    // Sign-aware: a mirrored rect (negative width/height - see
    // shaped_face_uv()'s door faces) is inset toward its own middle too.
    return {uv.x + std::copysign(inset_u, uv.width), uv.y + std::copysign(inset_v, uv.height),
            std::copysign(std::max(0.0f, std::fabs(uv.width) - inset_u * 2.0f), uv.width),
            std::copysign(std::max(0.0f, std::fabs(uv.height) - inset_v * 2.0f), uv.height)};
}

Rectangle block_atlas_tile_uv(int column, int row)
{
    return tile_uv(column, row);
}
