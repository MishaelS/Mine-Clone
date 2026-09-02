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

    // Same as get(), but on a cache miss the image is downscaled to
    // target_size x target_size before it's uploaded — for callers that need
    // a specific resolution regardless of the source image's size (e.g. block
    // textures). Ignored on a cache hit, since the cached texture keeps
    // whatever size it was first loaded at.
    const Texture2D& get_resized(const std::string& path, int target_size);

    // Frees every cached texture. Call once before CloseWindow.
    void unload_all();
}
