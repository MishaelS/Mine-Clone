#version 330

// Chunk mesh fragment shader: raylib's own default fragment shader (same
// texture*vertexColor shading, so tint/face-direction shading baked into
// vertex colors by Chunk::build_mesh keeps working exactly as before) plus
// linear distance fog, day/night + brightness-slider light scaling and
// sun-aware AO (both per-fragment, from their own vertex channels - see
// fragLight/fragAO below - rather than baked into vertexColor, so neither
// needs this mesh rebuilt when the time of day changes) and, only during
// Chunk::draw_water()'s own pass (see isWaterPass), a
// slow scroll of the sampled texel within its own atlas tile - a flowing-
// water look using the single static water tile terrain.png already has,
// no extra animation frames needed. Minecraft's own FogRenderer works the
// same way as the fog below: a linear ramp between a start and end
// distance, in world units, blending the object color toward the sky
// color.

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragWorldPosition;
in vec2 fragLight; // (sky, block) fractions, 0..1 - see chunk.vs's own comment
in float fragAO; // AO_BRIGHTNESS[ao], 0.5..1.0 (1.0 = no occlusion) - see chunk.vs's own comment

out vec4 finalColor;

// Real Minecraft's own "only sky light dims at night" rule, applied here
// instead of ever rebuilding a chunk's mesh when the time of day changes -
// see Chunk.hpp's set_chunk_daylight(), fed DayNightCycle::
// sky_light_factor(game_tick) once per frame by GameEngine::draw(). Block
// light (fragLight.y - torches, lava) is never scaled by this.
uniform float daylightFactor;

// Settings > Graphics' brightness slider - a gamma exponent applied ONLY to
// the sky-light term below, set once per frame via Chunk.hpp's
// set_chunk_brightness(). 1.0 (the slider's own max) is a no-op
// (pow(x, 1) == x); above 1.0 it only darkens shadow - full daylight
// (sky term already 1.0) stays pow(1.0, anything) == 1.0 regardless of the
// slider. Block light (torches, lava) never goes through this at all - see
// the comment where it's combined below for why.
uniform float brightnessGamma;

// True only while begin_dynamic_entity_shader()'s own draws are active -
// see Chunk.hpp's set_chunk_dynamic_entity_pass() for why: a dynamic
// entity has no real fragLight data (nothing binds vertexTexCoord2 for
// it), its light is already fully baked into fragColor instead, so this
// skips the two-channel combine entirely and trusts fragColor as-is.
uniform bool isDynamicEntityPass;

// Matches Chunk.hpp's own MIN_LIGHT_FRACTION - duplicated here since a
// shader can't include a C++ header; if one changes, so must the other.
const float MIN_LIGHT_FRACTION = 0.2;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Set once per frame from World::draw() (see Chunk.hpp's set_chunk_fog) -
// fogColor/fogSkyColor match the skybox's own horizon/sky gradient (see
// Skybox.cpp), so the render-distance edge reads as fading into the sky
// instead of a hard cutoff where chunks just stop being drawn - blended
// per-fragment between the two below, by view angle, rather than tinting
// every fragment with the flat horizon color alone; that left a mismatch
// visible as a pale silhouette wherever terrain rose above the horizon
// line, fogged a shade too light against the bluer sky actually behind
// it. fogStart/fogEnd come from World::LOADED_RADIUS.
uniform vec3 cameraPosition;
uniform vec3 fogColor;
uniform vec3 fogSkyColor;
uniform float fogStart;
uniform float fogEnd;

// Set once per frame from World::draw() (see Chunk.hpp's set_chunk_water_
// time) - seconds since the shader was loaded, used only for the texel
// scroll below.
uniform float waterTime;

// True only around Chunk::draw_water()'s own draw calls (set_chunk_water_
// pass in Chunk.hpp) - every chunk's opaque and water mesh share this one
// Material/shader, so without this flag the scroll below would just as
// well apply to every other block's texture too.
uniform bool isWaterPass;

// terrain.png's tile grid (see Block.cpp's own TILE_PIXELS/GRID_TILES) -
// duplicated here since a shader can't include a C++ header; if one
// changes, so must the other.
const float TILE_UV_SIZE = 1.0 / 16.0;
const vec3 WATER_DISTANCE_FOG_COLOR = vec3(0.12, 0.36, 0.90);
const float WATER_DISTANCE_FOG_STRENGTH = 0.72;

void main()
{
    vec2 uv = fragTexCoord;
    if (isWaterPass) {
        // Wrap the scroll within the texel's own tile (fract()), so it
        // loops seamlessly forever instead of eventually sampling into a
        // neighboring tile.
        vec2 tileOrigin = floor(uv / TILE_UV_SIZE) * TILE_UV_SIZE;
        vec2 halfTexel = 0.5 / vec2(textureSize(texture0, 0));
        vec2 tileInset = halfTexel / TILE_UV_SIZE;
        vec2 localUV = (uv - tileOrigin) / TILE_UV_SIZE;
        localUV = clamp((localUV - tileInset) / (1.0 - 2.0 * tileInset), 0.0, 1.0);
        localUV = fract(localUV + vec2(waterTime * 0.02, waterTime * 0.015));
        uv = tileOrigin + halfTexel + localUV * (TILE_UV_SIZE - 2.0 * halfTexel);
    }

    vec4 texelColor = texture(texture0, uv);
    // Pixel-art cutouts (currently foliage) need real holes, not black or
    // order-dependent translucent quads. Fully/mostly transparent texels
    // are harmlessly absent for every other block texture too.
    if (texelColor.a < 0.1) discard;
    finalColor = texelColor*colDiffuse*fragColor;

    // Day/night: combine the two light channels here, per fragment, rather
    // than at mesh-build time - block light always shows at full strength,
    // sky light fades toward MIN_NIGHT_SKY_LIGHT_FACTOR-of-full as
    // daylightFactor itself does (see DayNightCycle::sky_light_factor()).
    // Skipped entirely for a dynamic entity (see isDynamicEntityPass's own
    // comment) - its fragColor already *is* the final, fully-lit color.
    if (!isDynamicEntityPass) {
        // Day/night applies to the sky term only (fragLight.x is the raw,
        // always-fully-lit-by-day sky fraction - see chunk.vs's own
        // comment); the brightness slider's own gamma then applies only to
        // *that* sky term too, never to block light (fragLight.y) - a
        // torch's own light stays exactly as strong regardless of the
        // slider, the same way it never dims from AO below either. The
        // MIN_LIGHT_FRACTION floor is applied last, to their max, so it
        // never itself responds to the slider (it's a flat readability
        // guarantee, not "shadow").
        float skyTerm = pow(clamp(fragLight.x * daylightFactor, 0.0, 1.0), brightnessGamma);
        float blockTerm = fragLight.y;
        float lightScale = max(MIN_LIGHT_FRACTION, max(skyTerm, blockTerm));
        finalColor.rgb *= lightScale;

        // Ambient occlusion: suppressed wherever this fragment is actually
        // standing in direct sunlight right now (sky exposure * the
        // current daylight strength both near 1.0) - real Minecraft's own
        // vertex AO darkens a corner purely from nearby geometry, with no
        // regard for whether direct light is hitting it, which reads
        // wrong for a sunlit corner. mix() fades toward "no occlusion"
        // (1.0) as sun exposure rises, and applies fragAO at full strength
        // (its own baked value) in genuine shadow - a night-time or
        // sky-blocked corner darkens exactly as it always has.
        float sunExposure = clamp(fragLight.x * daylightFactor, 0.0, 1.0);
        finalColor.rgb *= mix(fragAO, 1.0, sunExposure);
    }

    float distance = length(fragWorldPosition - cameraPosition);
    float fogFactor = clamp((distance - fogStart) / (fogEnd - fogStart), 0.0, 1.0);

    // Same horizon->sky blend draw_skybox()'s own cube gradient makes by
    // vertical position, approximated here by the view ray's own upward
    // component instead (level = fogColor, straight up = fogSkyColor) -
    // downward view rays clamp to fogColor too, since nothing below the
    // horizon in this skybox gets any bluer.
    vec3 viewDir = normalize(fragWorldPosition - cameraPosition);
    vec3 skyTintedFogColor = mix(fogColor, fogSkyColor, clamp(viewDir.y, 0.0, 1.0));

    vec3 fogTarget = skyTintedFogColor;
    float fogStrength = fogFactor;
    if (isWaterPass) {
        // Water is already translucent over the sky/terrain behind it, so
        // blending it all the way into the bright horizon color makes lakes
        // read as white at medium distance. Keep the render-distance fade,
        // but bias that fade back toward Minecraft-like blue water.
        fogTarget = mix(skyTintedFogColor, WATER_DISTANCE_FOG_COLOR, 0.65);
        fogStrength *= WATER_DISTANCE_FOG_STRENGTH;
    }
    finalColor.rgb = mix(finalColor.rgb, fogTarget, fogStrength);
}
