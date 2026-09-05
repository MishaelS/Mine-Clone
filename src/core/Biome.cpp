#include "core/Biome.hpp"

#include <algorithm>

namespace {
    std::string BIOME_NAMES[] = {
        "Plains",
        "Forest",
        "Desert",
        "Hills",
        "Ocean",
        "Sea",
    };

    // Smooth 0->1 ramp between edge0 and edge1 (Ken Perlin's own smoothstep
    // formula) — used throughout compute_biome_weights() instead of a hard
    // less-than/greater-than comparison, so crossing a biome boundary
    // blends gradually over the ramp's width instead of at a single-point
    // cliff the way the old classify_biome() did.
    float smoothstep(float edge0, float edge1, float x) {
        float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}

BiomeWeights compute_biome_weights(float temperature, float humidity, float continentalness, float coast_roughness)
{
    // Land vs. water at very negative continentalness, ramping down to 0 by
    // the time it turns positive (dry land) — a gently sloping coastline
    // instead of the shoreline being a cliff. Same edges as the original
    // single ocean band, so the overall land/water split stays put; what's
    // new is splitting that water band itself into two.
    float total_water = 1.0f - smoothstep(-0.1f, 0.15f, continentalness);

    // Of that water, the fraction that's deep, "wild" Ocean rather than
    // shallow, calm Sea — from either of two independent causes:
    // continentalness well below the coastline (far out to sea is always
    // deep Ocean, regardless of coast_roughness) or, right at the
    // coastline itself, a "wild" stretch of coast_roughness (some
    // coastlines drop straight to deep water with no calm Sea buffer,
    // others don't — continentalness alone can't distinguish those two
    // kinds of coastline, since both start at the same distance from
    // land). Without this second term, deep Ocean could only ever appear
    // behind a Sea buffer, and a "wild gravel beach" directly against
    // Ocean (as opposed to a sandy Sea beach) could never occur.
    float deep_ocean = 1.0f - smoothstep(-0.35f, -0.05f, continentalness);
    float wild_coast = smoothstep(-0.1f, 0.15f, coast_roughness);
    float ocean_fraction = std::max(deep_ocean, wild_coast);
    float ocean = total_water * ocean_fraction;
    float sea = total_water - ocean;

    // The 4 land biomes — same regions as the old hard thresholds, each
    // now a smooth ramp across a band instead of a single cutoff point.
    float hills  = 1.0f - smoothstep(-0.4f, -0.2f, temperature); // cold -> rocky highlands
    float desert = smoothstep(0.3f, 0.5f, temperature) * (1.0f - smoothstep(-0.2f, 0.0f, humidity)); // hot and dry
    float forest = smoothstep(0.2f, 0.4f, humidity); // wet enough
    float plains = 1.0f; // the default everything else falls back to

    // Land biomes only compete with each other over the land fraction —
    // scale them so together they sum to exactly (1 - total_water), with
    // plains soaking up whatever the others didn't claim.
    plains = std::max(0.0f, plains - hills - desert - forest);
    float land_total = plains + hills + desert + forest;
    float land = 1.0f - total_water;
    float land_scale = (land_total > 0.0f) ? land / land_total : 0.0f;

    return {
        plains * land_scale,
        forest * land_scale,
        desert * land_scale,
        hills * land_scale,
        ocean,
        sea,
    };
}

Biome dominant_biome(const BiomeWeights& weights)
{
    Biome best = Biome::Plains;
    float best_weight = weights.plains;
    if (weights.forest > best_weight) { best = Biome::Forest; best_weight = weights.forest; }
    if (weights.desert > best_weight) { best = Biome::Desert; best_weight = weights.desert; }
    if (weights.hills  > best_weight) { best = Biome::Hills;  best_weight = weights.hills; }
    if (weights.ocean  > best_weight) { best = Biome::Ocean;  best_weight = weights.ocean; }
    if (weights.sea    > best_weight) { best = Biome::Sea;    best_weight = weights.sea; }
    return best;
}

const std::string& get_biome_name(Biome biome)
{
    return BIOME_NAMES[static_cast<uint8_t>(biome)];
}
