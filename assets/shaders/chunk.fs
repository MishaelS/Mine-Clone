#version 330

// Chunk mesh fragment shader: raylib's own default fragment shader (same
// texture*vertexColor shading, so AO/tint baked into vertex colors by
// Chunk::build_mesh keeps working exactly as before) plus linear distance
// fog and, only during Chunk::draw_water()'s own pass (see isWaterPass), a
// slow scroll of the sampled texel within its own atlas tile - a flowing-
// water look using the single static water tile terrain.png already has,
// no extra animation frames needed. Minecraft's own FogRenderer works the
// same way as the fog below: a linear ramp between a start and end
// distance, in world units, blending the object color toward the sky
// color.

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragWorldPosition;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Set once per frame from World::draw() (see Chunk.hpp's set_chunk_fog) -
// fogColor matches the skybox's own horizon color, so the render-distance
// edge reads as fading into the sky instead of a hard cutoff where chunks
// just stop being drawn. fogStart/fogEnd come from World::LOADED_RADIUS.
uniform vec3 cameraPosition;
uniform vec3 fogColor;
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

    float distance = length(fragWorldPosition - cameraPosition);
    float fogFactor = clamp((distance - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    finalColor.rgb = mix(finalColor.rgb, fogColor, fogFactor);
}
