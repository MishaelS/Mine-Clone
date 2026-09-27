#include "core/Block.hpp"
#include "content/Content.hpp"
#include "core/TextureManager.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace {

    std::array<BlockProperties, MAX_BLOCK_TYPES> block_table;
    std::array<std::string, MAX_BLOCK_TYPES> block_names;
    std::vector<BlockType> defined_blocks; // all_block_types()
    std::unordered_map<std::string, BlockType> name_to_type;

    constexpr const char* TERRAIN_TEXTURE_PATH = "sprites/terrain.png";

    // terrain.png is a 16x16 grid of 16px tiles (256x256 pixels total) -
    // every block face's content::Tile is a tile's column/row in that grid.
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
    // density) for a block whose definition doesn't override them, instead
    // of a second, separate classification. Values are deliberately
    // approximate ("wood floats, stone sinks, stone needs a pickaxe") -
    // this project isn't chasing exact vanilla hardness numbers, just
    // plausible relative ones; BlockDef's own .hardness()/.tool()/
    // .density() still win for anything worth tuning individually.
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

    // Range-checked the same way the old JSON loader did - a typo'd tile
    // fails loudly at startup instead of rendering a neighbor's texture.
    Rectangle checked_tile_uv(BlockType type, content::Tile tile) {
        if (tile.x < 0 || tile.x >= GRID_TILES || tile.y < 0 || tile.y >= GRID_TILES) {
            throw std::runtime_error("block '" + block_names[static_cast<size_t>(type)] +
                                     "': terrain tile coordinates must be in range 0..15");
        }
        return tile_uv(tile.x, tile.y);
    }
}

namespace content {

    BlockDef block(BlockType type, const char* name) {
        size_t index = static_cast<size_t>(type);
        if (type == BlockType::Air) {
            throw std::runtime_error(std::string("block '") + name + "': not a definable BlockType");
        }
        if (!block_names[index].empty()) {
            throw std::runtime_error("BlockType defined twice: '" + block_names[index] + "' and '" + name + "'");
        }
        if (!name_to_type.emplace(name, type).second) {
            throw std::runtime_error(std::string("duplicate block name '") + name + "'");
        }
        block_names[index] = name;

        // An ordinary opaque, solid, stone-sounding full cube - BlockDef's
        // setters change only what differs from this.
        BlockProperties& properties = block_table[index];
        properties = BlockProperties{};
        properties.solid            = true;
        properties.transparent      = false;
        properties.selectable       = true;
        properties.replaceable      = false;
        properties.luminance        = 0;
        properties.render_shape     = BlockRenderShape::Cube;
        properties.translucent      = false;
        properties.cutout           = false;
        properties.cull_same_faces  = true;
        properties.has_custom_shape = false;
        properties.side_inset       = 0.0f;
        properties.attach_floor     = false;
        properties.attach_wall      = false;
        properties.attach_ceiling   = false;
        properties.damages_on_touch = false;
        properties.directional = false;
        properties.shape_kind = BlockShapeKind::Cube;
        properties.double_block = BlockType::Air;
        properties.item_sprite_x = -1;
        properties.item_sprite_y = -1;
        for (int face = 0; face < 6; ++face) {
            properties.texture_uvs[face] = tile_uv(0, 0);
            properties.texture_tints[face] = WHITE;
        }

        BlockDef def(type);
        def.sound(BlockSoundGroup::Stone);
        return def;
    }

    BlockDef& BlockDef::sound(BlockSoundGroup group) {
        BlockProperties& properties = block_table[static_cast<size_t>(type_)];
        properties.sound_group = group;
        PhysicalDefaults defaults = physical_defaults_for(group);
        if (!hardness_set_) properties.hardness = defaults.hardness;
        if (!tool_set_) properties.effective_tool = defaults.tool;
        if (!density_set_) properties.density = defaults.density;
        return *this;
    }

    BlockDef& BlockDef::hardness(float seconds) {
        block_table[static_cast<size_t>(type_)].hardness = seconds;
        hardness_set_ = true;
        return *this;
    }

    BlockDef& BlockDef::tool(ToolKind kind) {
        block_table[static_cast<size_t>(type_)].effective_tool = kind;
        tool_set_ = true;
        return *this;
    }

    BlockDef& BlockDef::density(float relative_to_water) {
        block_table[static_cast<size_t>(type_)].density = relative_to_water;
        density_set_ = true;
        return *this;
    }

    BlockDef& BlockDef::luminance(int level) {
        block_table[static_cast<size_t>(type_)].luminance = std::clamp(level, 0, 15);
        return *this;
    }

    BlockDef& BlockDef::non_solid() {
        BlockProperties& properties = block_table[static_cast<size_t>(type_)];
        properties.solid = false;
        if (!replaceable_set_) properties.replaceable = true;
        return *this;
    }

    BlockDef& BlockDef::not_selectable() {
        block_table[static_cast<size_t>(type_)].selectable = false;
        return *this;
    }

    BlockDef& BlockDef::replaceable(bool value) {
        block_table[static_cast<size_t>(type_)].replaceable = value;
        replaceable_set_ = true;
        return *this;
    }

    BlockDef& BlockDef::transparent() {
        block_table[static_cast<size_t>(type_)].transparent = true;
        return *this;
    }

    BlockDef& BlockDef::translucent() {
        block_table[static_cast<size_t>(type_)].translucent = true;
        return *this;
    }

    BlockDef& BlockDef::cutout() {
        block_table[static_cast<size_t>(type_)].cutout = true;
        return *this;
    }

    BlockDef& BlockDef::keep_same_faces() {
        block_table[static_cast<size_t>(type_)].cull_same_faces = false;
        return *this;
    }

    BlockDef& BlockDef::shape(BlockShapeKind kind) {
        block_table[static_cast<size_t>(type_)].shape_kind = kind;
        return *this;
    }

    BlockDef& BlockDef::double_block(BlockType type) {
        block_table[static_cast<size_t>(type_)].double_block = type;
        return *this;
    }

    BlockDef& BlockDef::item_sprite(Tile tile) {
        block_table[static_cast<size_t>(type_)].item_sprite_x = tile.x;
        block_table[static_cast<size_t>(type_)].item_sprite_y = tile.y;
        return *this;
    }

    BlockDef& BlockDef::state_model(int state, const BlockStateModel& model) {
        block_table[static_cast<size_t>(type_)].state_models[static_cast<size_t>(state)] = model;
        return *this;
    }

    BlockDef& BlockDef::elements(std::vector<BlockElement> parts) {
        block_table[static_cast<size_t>(type_)].elements = std::move(parts);
        return *this;
    }

    BlockDef& BlockDef::directional() {
        block_table[static_cast<size_t>(type_)].directional = true;
        return *this;
    }

    BlockDef& BlockDef::damages_on_touch() {
        block_table[static_cast<size_t>(type_)].damages_on_touch = true;
        return *this;
    }

    BlockDef& BlockDef::side_inset(int pixels) {
        block_table[static_cast<size_t>(type_)].side_inset = static_cast<float>(pixels) / static_cast<float>(TILE_PIXELS);
        return *this;
    }

    BlockDef& BlockDef::cross() {
        block_table[static_cast<size_t>(type_)].render_shape = BlockRenderShape::Cross;
        return *this;
    }

    BlockDef& BlockDef::shaped() {
        block_table[static_cast<size_t>(type_)].render_shape = BlockRenderShape::Shaped;
        return *this;
    }

    BlockDef& BlockDef::custom_shape() {
        block_table[static_cast<size_t>(type_)].has_custom_shape = true;
        return *this;
    }

    BlockDef& BlockDef::attach_floor() {
        block_table[static_cast<size_t>(type_)].attach_floor = true;
        return *this;
    }

    BlockDef& BlockDef::attach_wall() {
        block_table[static_cast<size_t>(type_)].attach_wall = true;
        return *this;
    }

    BlockDef& BlockDef::attach_ceiling() {
        block_table[static_cast<size_t>(type_)].attach_ceiling = true;
        return *this;
    }

    void BlockDef::set_face(int face, Tile tile, Color tint, FacePriority priority) {
        if (priority < face_priority_[face]) return;
        face_priority_[face] = priority;
        BlockProperties& properties = block_table[static_cast<size_t>(type_)];
        properties.texture_uvs[face] = checked_tile_uv(type_, tile);
        properties.texture_tints[face] = tint;
    }

    // Face indices follow BlockFace: Top, Bottom, North, South, East, West.
    BlockDef& BlockDef::all(Tile tile, Color tint) {
        for (int face = 0; face < 6; ++face) set_face(face, tile, tint, All);
        return *this;
    }

    BlockDef& BlockDef::side(Tile tile, Color tint) {
        for (int face = 2; face < 6; ++face) set_face(face, tile, tint, Side);
        return *this;
    }

    BlockDef& BlockDef::top(Tile tile, Color tint)    { set_face(0, tile, tint, Exact); return *this; }
    BlockDef& BlockDef::bottom(Tile tile, Color tint) { set_face(1, tile, tint, Exact); return *this; }
    BlockDef& BlockDef::north(Tile tile, Color tint)  { set_face(2, tile, tint, Exact); return *this; }
    BlockDef& BlockDef::south(Tile tile, Color tint)  { set_face(3, tile, tint, Exact); return *this; }
    BlockDef& BlockDef::east(Tile tile, Color tint)   { set_face(4, tile, tint, Exact); return *this; }
    BlockDef& BlockDef::west(Tile tile, Color tint)   { set_face(5, tile, tint, Exact); return *this; }

    BlockDef& BlockDef::cut(Tile tile) {
        block_table[static_cast<size_t>(type_)].cut_texture_uv = checked_tile_uv(type_, tile);
        return *this;
    }

    BlockDef& BlockDef::end(Tile tile) {
        block_table[static_cast<size_t>(type_)].end_texture_uv = checked_tile_uv(type_, tile);
        return *this;
    }

} // namespace content

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
        air.item_sprite_x   = -1;
        air.item_sprite_y   = -1;
    }
    block_names[static_cast<uint8_t>(BlockType::Air)] = "air";

    block_atlas_texture = &TextureManager::get(TERRAIN_TEXTURE_PATH);
    if (block_atlas_texture->width != TILE_PIXELS * GRID_TILES ||
        block_atlas_texture->height != TILE_PIXELS * GRID_TILES) {
        throw std::runtime_error(
            "sprites/terrain.png must be exactly 256x256 pixels "
            "(16x16 tiles, each tile 16x16 pixels)");
    }

    content::register_blocks();

    // Every named BlockType must be defined - in code or by a block file.
    for (size_t i = 1; i < static_cast<size_t>(BlockType::Count); ++i) {
        if (block_names[i].empty()) {
            throw std::runtime_error("no definition for BlockType id " + std::to_string(i) +
                                     " - neither in src/content/Blocks.cpp nor in assets/blocks/");
        }
    }
    defined_blocks.clear();
    for (size_t i = 1; i < block_names.size(); ++i) {
        if (!block_names[i].empty()) defined_blocks.push_back(static_cast<BlockType>(i));
    }
}

const std::vector<BlockType>& all_block_types()
{
    return defined_blocks;
}

const BlockProperties& get_block_properties(BlockType type)
{
    return block_table[static_cast<uint8_t>(type)];
}

bool block_is_directional(BlockType type)
{
    return get_block_properties(type).directional;
}

bool block_needs_facing(BlockType type)
{
    const BlockShapeKind kind = get_block_properties(type).shape_kind;
    return block_is_directional(type) || kind == BlockShapeKind::Stairs || kind == BlockShapeKind::Trapdoor;
}

FaceOffset block_face_offset(BlockFace face)
{
    switch (face) {
        case BlockFace::Top:    return { 0,  1,  0};
        case BlockFace::Bottom: return { 0, -1,  0};
        case BlockFace::North:  return { 0,  0, -1};
        case BlockFace::South:  return { 0,  0,  1};
        case BlockFace::East:   return { 1,  0,  0};
        case BlockFace::West:   return {-1,  0,  0};
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
        case HorizontalDirection::North: return { 0, -1};
        case HorizontalDirection::South: return { 0,  1};
        case HorizontalDirection::East:  return { 1,  0};
        case HorizontalDirection::West:  return {-1,  0};
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
    auto it = name_to_type.find(name);
    if (it == name_to_type.end()) return std::nullopt;
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
