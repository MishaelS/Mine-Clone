#include "core/TextureAtlas.hpp"

#include <stdexcept>

TextureAtlas::TextureAtlas(int tile_size, int grid_size)
    : tile_size(tile_size), grid_size(grid_size), stride(tile_size + 2 * PADDING)
{
    cpu_image = GenImageColor(stride * grid_size, stride * grid_size, BLANK);
}

Rectangle TextureAtlas::get_uv(const std::string& file_name)
{
    auto it = uv_by_file.find(file_name);
    if (it != uv_by_file.end()) {
        return it->second;
    }

    if (next_slot >= grid_size * grid_size) {
        throw std::runtime_error("TextureAtlas: out of space for '" + file_name + "'");
    }
    int col = next_slot % grid_size;
    int row = next_slot / grid_size;
    ++next_slot;

    Image sprite = LoadImage((std::string(ASSETS_PATH "sprites/") + file_name).c_str());
    ImageResize(&sprite, tile_size, tile_size);

    int px = col * stride + PADDING;
    int py = row * stride + PADDING;
    ImageDrawImage(&cpu_image, sprite, px, py, WHITE);

    // Bleed the outermost row/column of the sprite into the padding on each
    // side (see the PADDING comment in the header for why).
    float w = static_cast<float>(tile_size), h = static_cast<float>(tile_size);
    ImageDrawImageRec(&cpu_image, sprite, {0, 0, w, 1}, {static_cast<float>(px), static_cast<float>(py - PADDING)}, WHITE); // top
    ImageDrawImageRec(&cpu_image, sprite, {0, h - 1, w, 1}, {static_cast<float>(px), static_cast<float>(py + tile_size)}, WHITE); // bottom
    ImageDrawImageRec(&cpu_image, sprite, {0, 0, 1, h}, {static_cast<float>(px - PADDING), static_cast<float>(py)}, WHITE); // left
    ImageDrawImageRec(&cpu_image, sprite, {w - 1, 0, 1, h}, {static_cast<float>(px + tile_size), static_cast<float>(py)}, WHITE); // right

    UnloadImage(sprite);

    float atlas_pixels = static_cast<float>(stride * grid_size);
    Rectangle uv = {
        px / atlas_pixels,
        py / atlas_pixels,
        tile_size / atlas_pixels,
        tile_size / atlas_pixels,
    };
    uv_by_file.emplace(file_name, uv);
    return uv;
}

void TextureAtlas::upload()
{
    gpu_texture = LoadTextureFromImage(cpu_image);
    UnloadImage(cpu_image);
    SetTextureFilter(gpu_texture, TEXTURE_FILTER_POINT);
}
