#pragma once

#include "raylib.h"

// Draws a giant cube centered on the camera, gradient-colored (sky blue up
// top, lighter near the horizon) and depth-tested off so it always reads as
// infinitely far away. No texture/cubemap — just vertex-colored geometry.
void DrawSkybox(Vector3 cameraPosition);
