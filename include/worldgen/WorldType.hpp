#pragma once

#include <cstdint>
#include <string>

// Which generator preset a world was created with - picked on the world
// creation screen, saved in world.json ("world_type") and fixed for the
// world's whole life, since every chunk not yet generated must keep
// matching the ones that already were.
enum class WorldType : uint8_t {
    Normal,      // the default terrain
    Mountains,   // much taller, rockier land with snowy peaks
    Underground, // normal surface over far more (and bigger) caves and deeper ravines
    Sky,         // floating islands around cloud height over an open void
    Count,
};

// Everything that differs between presets for the passes shared by all of
// them (caves, ravines, ores) - the terrain *shape* itself is chosen in
// Chunk::generate_terrain(), per preset.
struct WorldTypeParams {
    // Terrain
    float mountain_height = 0.0f;   // extra blocks a full mountain mask adds on top of the normal land height

    // Caves (see Chunk::carve_caves) - 0 rarity disables that feature.
    int cave_chunk_rarity = 6;      // 1 in N chunks originates a cave system
    int cave_chunk_radius = 4;      // how far (in chunks) a tunnel may reach from its origin
    int cavern_chance = 4;          // 1 in N systems is one fat cavern instead of branching tunnels
    float cavern_max_scale = 7.0f;  // radius multiplier range of such a cavern: 1..max
    int tunnel_extra_length = 0;    // added to every tunnel's random length

    int ravine_chunk_rarity = 30;   // 1 in N chunks originates a ravine
    int ravine_min_length = 40;
    int ravine_extra_length = 40;   // random 0..N added to ravine_min_length
    float ravine_depth_scale = 1.0f; // multiplies a ravine's height (not its width)

    // Ores: shifts every vein band up by this many blocks - floating islands
    // have no stone down at the normal ore depths.
    int ore_y_offset = 0;
};

inline WorldTypeParams world_type_params(WorldType type)
{
    WorldTypeParams params;
    switch (type) {
        case WorldType::Mountains:
            params.mountain_height = 130.0f;
            break;
        case WorldType::Underground:
            params.cave_chunk_rarity = 2;
            params.cave_chunk_radius = 5;
            params.cavern_chance = 2;
            params.cavern_max_scale = 11.0f;
            params.tunnel_extra_length = 25;
            params.ravine_chunk_rarity = 8;
            params.ravine_min_length = 60;
            params.ravine_extra_length = 60;
            params.ravine_depth_scale = 2.2f;
            break;
        case WorldType::Sky:
            params.cave_chunk_rarity = 0;
            params.ravine_chunk_rarity = 0;
            params.ore_y_offset = 150;
            break;
        case WorldType::Normal:
        case WorldType::Count:
            break;
    }
    return params;
}

// Stable id saved in world.json and used for translation keys
// ("world_type.<id>" and "world_type.<id>.description").
inline const char* world_type_id(WorldType type)
{
    switch (type) {
        case WorldType::Mountains:   return "mountains";
        case WorldType::Underground: return "underground";
        case WorldType::Sky:         return "sky";
        default:                     return "normal";
    }
}

// Unknown or missing (a world saved before presets existed) -> Normal.
inline WorldType world_type_from_id(const std::string& id)
{
    for (int i = 0; i < static_cast<int>(WorldType::Count); ++i) {
        WorldType type = static_cast<WorldType>(i);
        if (id == world_type_id(type)) return type;
    }
    return WorldType::Normal;
}

inline WorldType next_world_type(WorldType type)
{
    return static_cast<WorldType>((static_cast<int>(type) + 1) % static_cast<int>(WorldType::Count));
}
