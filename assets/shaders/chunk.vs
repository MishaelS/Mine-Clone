#version 330

// Chunk mesh vertex shader: identical to raylib's own default vertex shader
// (same attribute/uniform names, so DrawMesh() wires everything up exactly
// the same way) plus fragWorldPosition (so the fragment shader can compute
// each fragment's distance from the camera for fog, see chunk.fs) and a
// small wave that ripples water's own surface vertices.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

// (sky, block) light fractions (each 0..1) - Chunk::append_face()'s own
// per-vertex "smooth lighting" sample, kept separate from vertexColor
// (which only carries the day/night-*independent* AO/face-direction
// shading) so chunk.fs can apply the current daylightFactor uniform to
// just the sky channel. Never bound for a dynamic-entity draw (see chunk.
// fs's own isDynamicEntityPass) - reads as a harmless, unused (0, 0) then.
in vec2 vertexTexCoord2;

// Ambient occlusion strength (AO_BRIGHTNESS[ao], 0.5..1.0 - 1.0 = no
// occlusion), the same value 4 times over (raylib's tangents are XYZW per
// vertex; only .x is read here) - see Chunk::append_face()'s own comment
// on why this travels separately from vertexColor's own baked face-
// direction shading. Never bound for a dynamic-entity draw, same as
// vertexTexCoord2 - reads as a harmless, unused 0 then.
in vec4 vertexTangent;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragWorldPosition;
out vec2 fragLight;
out float fragAO;

uniform mat4 mvp;

// Set automatically by raylib's DrawMesh() every draw call - matModel is
// one of its recognized-by-name uniforms, same as mvp, no extra C++ code
// needed to feed it.
uniform mat4 matModel;

// Set once per frame from World::draw() (see Chunk.hpp's set_chunk_water_
// time/set_chunk_water_pass) - same uniforms chunk.fs's texel scroll uses,
// shared here since a linked GLSL program's uniforms are one shared set
// regardless of which stage(s) declare them.
uniform float waterTime;
uniform bool isWaterPass;

void main()
{
    vec3 localPosition = vertexPosition;

    if (isWaterPass) {
        // Ripple the water surface and side top edges, whatever flowing
        // level produced their height. Bottom corners stay at -0.5.
        if (localPosition.y > -0.49) {
            vec3 worldPosition = (matModel * vec4(localPosition, 1.0)).xyz;
            float wave = sin(worldPosition.x * 0.6 + worldPosition.z * 0.4 + waterTime * 1.6) * 0.04
                       + sin(worldPosition.x * 0.25 - worldPosition.z * 0.35 + waterTime * 1.1) * 0.03;
            localPosition.y += wave;
        }
    }

    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragWorldPosition = (matModel * vec4(localPosition, 1.0)).xyz;
    fragLight = vertexTexCoord2;
    fragAO = vertexTangent.x;
    gl_Position = mvp*vec4(localPosition, 1.0);
}
