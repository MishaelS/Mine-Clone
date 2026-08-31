#include "BlockTextures.hpp"

#include <unordered_map>

namespace {
    // The source sprites are 4000x4000 photo-scanned textures. Downscaling
    // only to something like 256 still leaves the smooth gradients that
    // scaling bakes in, which reads as "blurry" next to genuine blocky pixel
    // art — so shrink all the way to Minecraft's actual block resolution.
    constexpr int TEXTURE_SIZE = 16;

    std::unordered_map<std::string, Texture2D> textures;
}

void UnloadBlockTextures() {
    for (auto& [name, texture] : textures) {
        UnloadTexture(texture);
    }
    textures.clear();
}

const Texture2D& GetBlockTexture(const std::string& fileName) {
    auto it = textures.find(fileName);
    if (it != textures.end()) {
        return it->second;
    }

    Image image = LoadImage((std::string(ASSETS_PATH "sprites/") + fileName).c_str());
    ImageResize(&image, TEXTURE_SIZE, TEXTURE_SIZE);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    // raylib defaults to this already, but set it explicitly: point/nearest
    // sampling is what keeps low-res block textures crisp instead of smeared.
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);

    return textures.emplace(fileName, texture).first->second;
}
