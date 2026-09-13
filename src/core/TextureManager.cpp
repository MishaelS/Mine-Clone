#include "core/TextureManager.hpp"

#include <unordered_map>

namespace TextureManager {
    namespace {
        std::unordered_map<std::string, Texture2D> textures;

        Texture2D load_and_upload(const std::string& path) {
            Image image = LoadImage((std::string(ASSETS_PATH) + path).c_str());
            Texture2D texture = LoadTextureFromImage(image);
            UnloadImage(image);
            // raylib defaults to this already, but set it explicitly: point/nearest
            // sampling is what keeps low-res pixel art crisp instead of smeared.
            SetTextureFilter(texture, TEXTURE_FILTER_POINT);
            SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
            return texture;
        }
    }

    const Texture2D& get(const std::string& path) {
        auto it = textures.find(path);
        if (it != textures.end()) {
            return it->second;
        }
        return textures.emplace(path, load_and_upload(path)).first->second;
    }

    void unload_all() {
        for (auto& [path, texture] : textures) {
            UnloadTexture(texture);
        }
        textures.clear();
    }
}
