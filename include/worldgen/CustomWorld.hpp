#pragma once

#include "core/Biome.hpp"
#include "core/Block.hpp"
#include "worldgen/WorldType.hpp"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// WorldType::Custom: a flat world built from a layer recipe the player
// types in ("bedrock, 59 stone, 3 dirt, grass" - bottom to top), plus an
// on/off switch and a chance for every structure and biome.

// Every switchable feature. To add one: a value here, a row in
// CUSTOM_FEATURES below, "custom.feature.<id>" in the translation files,
// and whatever generation code reads it (see custom_world_params()) - the
// custom world screen and world.json pick the row up by themselves.
enum class CustomFeature : uint8_t {
    Trees, Grass, Caves, Ravines, Ores,
    Plains, Forest, Desert, Hills,
    Count,
};

enum class CustomFeatureGroup : uint8_t { Structures, Biomes };

struct CustomFeatureInfo {
    CustomFeature feature;
    const char* id; // world.json key and translation key suffix
    CustomFeatureGroup group;
};

// Display order on the custom world screen.
inline constexpr CustomFeatureInfo CUSTOM_FEATURES[] = {
    {CustomFeature::Trees,   "trees",   CustomFeatureGroup::Structures},
    {CustomFeature::Grass,   "grass",   CustomFeatureGroup::Structures},
    {CustomFeature::Caves,   "caves",   CustomFeatureGroup::Structures},
    {CustomFeature::Ravines, "ravines", CustomFeatureGroup::Structures},
    {CustomFeature::Ores,    "ores",    CustomFeatureGroup::Structures},
    {CustomFeature::Plains,  "plains",  CustomFeatureGroup::Biomes},
    {CustomFeature::Forest,  "forest",  CustomFeatureGroup::Biomes},
    {CustomFeature::Desert,  "desert",  CustomFeatureGroup::Biomes},
    {CustomFeature::Hills,   "hills",   CustomFeatureGroup::Biomes},
};

// Chance is a percentage of how often the feature shows up in a Normal
// world: 100 = the same, 50 = half as often, 200 = twice. For biomes it's
// their share of the land relative to each other.
constexpr int CUSTOM_CHANCE_MIN = 1;
constexpr int CUSTOM_CHANCE_MAX = 200;

struct CustomFeatureSetting {
    bool enabled = true;
    int chance = 100;
};

struct CustomWorld {
    static constexpr const char* DEFAULT_LAYERS = "bedrock, 59 stone, 3 dirt, grass";

    // The layer recipe as saved in world.json, in canonical form (block
    // ids, see parse_custom_layers()).
    std::string layers = DEFAULT_LAYERS;
    std::array<CustomFeatureSetting, static_cast<size_t>(CustomFeature::Count)> features{};

    // `layers` resolved to one block per height, from the world floor up
    // (Air where the recipe asks for a gap) - see resolve_column().
    std::vector<BlockType> column;

    CustomFeatureSetting& operator[](CustomFeature feature) { return features[static_cast<size_t>(feature)]; }
    const CustomFeatureSetting& operator[](CustomFeature feature) const { return features[static_cast<size_t>(feature)]; }

    // 0 when switched off, otherwise chance / 100.
    float amount(CustomFeature feature) const;

    // Fills `column` from `layers`; an invalid recipe (a hand-edited
    // world.json) falls back to DEFAULT_LAYERS.
    void resolve_column();
};

// Result of reading a layer recipe. On failure `error_key` is a
// translation key and `error_arg` its "{0}".
struct LayerParseResult {
    std::vector<BlockType> column; // bottom to top, one entry per block
    std::string canonical;         // e.g. "bedrock, 59 stone, 3 dirt, grass"
    std::string error_key;
    std::string error_arg;
    bool ok() const { return error_key.empty(); }
};

// Maps one typed block name to a block - by default its id (see
// block_type_from_name()); the custom world screen also accepts the name
// as shown in the current language.
using BlockNameLookup = std::function<std::optional<BlockType>(const std::string& name)>;

// Layer recipe syntax, deliberately tiny: entries separated by commas (or
// new lines/semicolons), bottom to top; each is a block with an optional
// count before it - "grass", "3 dirt", "3*dirt". "air" leaves a gap.
LayerParseResult parse_custom_layers(const std::string& text, const BlockNameLookup& lookup = {});

// The highest a recipe may reach, leaving room above for trees.
constexpr int CUSTOM_MAX_LAYERS = 350;

// Cave/ravine/tree/grass/ore rates for a custom world - see WorldTypeParams.
WorldTypeParams custom_world_params(const CustomWorld& custom);

// Land-only biome weights (a custom world is flat - it has no sea),
// scaled by each biome's switch and chance.
BiomeWeights custom_biome_weights(const BiomeWeights& land_weights, const CustomWorld& custom);
