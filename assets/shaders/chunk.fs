#version 330

// Chunk mesh fragment shader: raylib's own default fragment shader (same
// texture*vertexColor shading, so AO/tint baked into vertex colors by
// Chunk::build_mesh keeps working exactly as before) plus linear distance
// fog — the one new thing this shader adds. Minecraft's own FogRenderer
// works the same way: a linear ramp between a start and end distance, in
// world units, blending the object color toward the sky color.

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragWorldPosition;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Set once per frame from World::draw() (see Chunk.hpp's set_chunk_fog) —
// fogColor matches the skybox's own horizon color, so the render-distance
// edge reads as fading into the sky instead of a hard cutoff where chunks
// just stop being drawn. fogStart/fogEnd come from World::LOADED_RADIUS.
uniform vec3 cameraPosition;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;

void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);
    finalColor = texelColor*colDiffuse*fragColor;

    float distance = length(fragWorldPosition - cameraPosition);
    float fogFactor = clamp((distance - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    finalColor.rgb = mix(finalColor.rgb, fogColor, fogFactor);
}
