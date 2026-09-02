#include "core/IsoCamera.hpp"

#include <cmath>

namespace {
    constexpr float DISTANCE = 20.0f;
    // True isometric pitch: atan(1/sqrt(2)) ~= 35.264 degrees down from horizontal.
    constexpr float PITCH        = 35.264f * DEG2RAD;
    constexpr float YAW          = 45.0f * DEG2RAD;
    constexpr float ORTHO_HEIGHT = 10.0f; // visible world units, top to bottom
    constexpr float PAN_SPEED    = 10.0f;    // world units per second

    // Yaw is fixed, so "screen up" and "screen right" always map to these two
    // world-space directions (forward = horizontal part of the view direction,
    // right = perpendicular to it).
    const Vector3 FORWARD = {-sinf(YAW), 0.0f, -cosf(YAW)};
    const Vector3 RIGHT   = { cosf(YAW), 0.0f, -sinf(YAW)};
}

IsoCamera::IsoCamera()
{
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.position = {
        DISTANCE * cosf(PITCH) * sinf(YAW),
        DISTANCE * sinf(PITCH),
        DISTANCE * cosf(PITCH) * cosf(YAW),
    };
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = ORTHO_HEIGHT;
    camera.projection = CAMERA_ORTHOGRAPHIC;
}

void IsoCamera::update(float deltaTime)
{
    Vector3 pan = {0.0f, 0.0f, 0.0f};
    if (IsKeyDown(KEY_W)) { pan.x += FORWARD.x; pan.z += FORWARD.z; }
    if (IsKeyDown(KEY_S)) { pan.x -= FORWARD.x; pan.z -= FORWARD.z; }
    if (IsKeyDown(KEY_D)) { pan.x += RIGHT.x;   pan.z += RIGHT.z;   }
    if (IsKeyDown(KEY_A)) { pan.x -= RIGHT.x;   pan.z -= RIGHT.z;   }

    float length = sqrtf(pan.x * pan.x + pan.z * pan.z);
    if (length > 0.0f) {
        float step = PAN_SPEED * deltaTime / length;
        pan.x *= step;
        pan.z *= step;

        camera.target.x += pan.x;
        camera.target.z += pan.z;
        camera.position.x += pan.x;
        camera.position.z += pan.z;
    }
}
