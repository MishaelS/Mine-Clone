#pragma once

#include "raylib.h"

// Fixed-angle orthographic camera: the classic isometric look (true isometric
// pitch, 45-degree yaw). WASD pans it; no rotation or zoom controls yet.
class IsoCamera {
public:
    IsoCamera();

    void update(float deltaTime);
    const Camera3D& get_camera() const { return camera; }

private:
    Camera3D camera;
};
