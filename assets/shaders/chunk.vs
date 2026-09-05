#version 330

// Chunk mesh vertex shader: identical to raylib's own default vertex shader
// (same attribute/uniform names, so DrawMesh() wires everything up exactly
// the same way) plus one addition — fragWorldPosition, so the fragment
// shader can compute each fragment's distance from the camera for fog
// (see chunk.fs) without needing per-vertex distance baked into the mesh.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragWorldPosition;

uniform mat4 mvp;

// Set automatically by raylib's DrawMesh() every draw call — matModel is
// one of its recognized-by-name uniforms, same as mvp, no extra C++ code
// needed to feed it.
uniform mat4 matModel;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragWorldPosition = (matModel * vec4(vertexPosition, 1.0)).xyz;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
