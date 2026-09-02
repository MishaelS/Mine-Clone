#pragma once

#include "raylib.h"

#include <string>
#include <unordered_map>

// Packs same-size sprites into one GPU texture, so a mesh that references
// many different source images (like a chunk's many block faces) can still
// be drawn with a single bound texture — one material, one draw call.
class TextureAtlas {
public:
    // tile_size: width/height (in pixels) every packed sprite is resized to.
    // grid_size: the atlas is a grid_size x grid_size grid of tiles; packing
    // more distinct sprites than that throws.
    TextureAtlas(int tile_size, int grid_size);

    // Packs `file_name` (relative to assets/sprites/) if not already packed,
    // and returns its UV rectangle (0..1, top-left origin) within the atlas.
    // Only valid before upload() — the CPU-side image is freed there.
    Rectangle get_uv(const std::string& file_name);

    // Uploads the packed image to the GPU. Call once, after every sprite has
    // been added via get_uv().
    void upload();

    const Texture2D& texture() const { return gpu_texture; }

private:
    int tile_size;
    int grid_size;
    // Each grid cell reserves this many pixels beyond tile_size, filled with
    // a duplicate of the sprite's own edge pixels — so nearest-filter
    // sampling that lands exactly on a tile's boundary (u or v of 0 or 1,
    // which happens at every block face's edge) reads a copy of this tile's
    // edge color instead of bleeding in the neighboring sprite.
    static constexpr int PADDING = 1;
    int stride;
    int next_slot = 0;
    Image cpu_image;
    std::unordered_map<std::string, Rectangle> uv_by_file;
    Texture2D gpu_texture{};
};
