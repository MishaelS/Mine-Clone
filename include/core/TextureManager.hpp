#pragma once

#include "raylib.h"

#include <string>

// Central GPU texture cache, shared by every system that needs image assets
// (blocks today; entities/UI later). Textures are keyed by their path
// relative to ASSETS_PATH, so no matter how many callers request the same
// file, it's read from disk and uploaded to the GPU only once.
namespace TextureManager {
    // Loads (if not already cached) and returns the texture at `path`
    // (relative to ASSETS_PATH, e.g. "sprites/dirt.png"). Needs a GL context
    // (call after InitWindow).
    const Texture2D& get(const std::string& path);

    // Frees every cached texture. Call once before CloseWindow.
    void unload_all();
}
