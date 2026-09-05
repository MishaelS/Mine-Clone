#include "Skybox.hpp"

#include "rlgl.h"

namespace {
    // Half-extent of the cube; comfortably inside raylib's default far clip
    // plane (1000 units) so it never gets clipped away.
    constexpr float SIZE = 500.0f;

    constexpr Color SKY_COLOR = {135, 190, 235, 255};
    constexpr Color HORIZON_COLOR = {215, 235, 245, 255};

    void Vertex(Vector3 center, float x, float y, float z, Color color) {
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlVertex3f(center.x + x, center.y + y, center.z + z);
    }
}

void draw_skybox(Vector3 camera_position)
{
    rlSetTexture(0);
    rlDisableBackfaceCulling(); // the camera sits inside this cube
    rlDisableDepthTest();       // always render behind everything else

    rlBegin(RL_QUADS);
        // Top
        Vertex(camera_position, -SIZE, SIZE, -SIZE, SKY_COLOR);
        Vertex(camera_position, -SIZE, SIZE,  SIZE, SKY_COLOR);
        Vertex(camera_position,  SIZE, SIZE,  SIZE, SKY_COLOR);
        Vertex(camera_position,  SIZE, SIZE, -SIZE, SKY_COLOR);

        // Bottom
        Vertex(camera_position, -SIZE, -SIZE,  SIZE, HORIZON_COLOR);
        Vertex(camera_position, -SIZE, -SIZE, -SIZE, HORIZON_COLOR);
        Vertex(camera_position,  SIZE, -SIZE, -SIZE, HORIZON_COLOR);
        Vertex(camera_position,  SIZE, -SIZE,  SIZE, HORIZON_COLOR);

        // North (-Z), sky at the top edge fading to horizon at the bottom edge
        Vertex(camera_position, -SIZE,  SIZE, -SIZE, SKY_COLOR);
        Vertex(camera_position,  SIZE,  SIZE, -SIZE, SKY_COLOR);
        Vertex(camera_position,  SIZE, -SIZE, -SIZE, HORIZON_COLOR);
        Vertex(camera_position, -SIZE, -SIZE, -SIZE, HORIZON_COLOR);

        // South (+Z)
        Vertex(camera_position,  SIZE,  SIZE,  SIZE, SKY_COLOR);
        Vertex(camera_position, -SIZE,  SIZE,  SIZE, SKY_COLOR);
        Vertex(camera_position, -SIZE, -SIZE,  SIZE, HORIZON_COLOR);
        Vertex(camera_position,  SIZE, -SIZE,  SIZE, HORIZON_COLOR);

        // East (+X)
        Vertex(camera_position,  SIZE,  SIZE, -SIZE, SKY_COLOR);
        Vertex(camera_position,  SIZE,  SIZE,  SIZE, SKY_COLOR);
        Vertex(camera_position,  SIZE, -SIZE,  SIZE, HORIZON_COLOR);
        Vertex(camera_position,  SIZE, -SIZE, -SIZE, HORIZON_COLOR);

        // West (-X)
        Vertex(camera_position, -SIZE,  SIZE,  SIZE, SKY_COLOR);
        Vertex(camera_position, -SIZE,  SIZE, -SIZE, SKY_COLOR);
        Vertex(camera_position, -SIZE, -SIZE, -SIZE, HORIZON_COLOR);
        Vertex(camera_position, -SIZE, -SIZE,  SIZE, HORIZON_COLOR);
    rlEnd();

    // rlEnableDepthTest/rlEnableBackfaceCulling below flip GL state
    // immediately, but rlEnd() doesn't flush these quads to the GPU by
    // itself — without this, they'd get rasterized later (whenever the
    // batch actually flushes) with depth test and culling back on, and
    // vanish: back-face culling would discard them since the camera sits
    // inside the cube, facing their back side.
    rlDrawRenderBatchActive();

    rlEnableDepthTest();
    rlEnableBackfaceCulling();
}

Color skybox_horizon_color()
{
    return HORIZON_COLOR;
}
