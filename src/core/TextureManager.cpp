#include "core/TextureManager.hpp"

#include <unordered_map>

namespace TextureManager {
    namespace {
        std::unordered_map<std::string, Texture2D> textures;

        Texture2D LoadAndUpload(const std::string& path, int target_size) {
            Image image = LoadImage((std::string(ASSETS_PATH) + path).c_str());
            if (target_size > 0) {
                ImageResize(&image, target_size, target_size);
            }
            Texture2D texture = LoadTextureFromImage(image);
            UnloadImage(image);
            // raylib defaults to this already, but set it explicitly: point/nearest
            // sampling is what keeps low-res pixel art crisp instead of smeared.
            SetTextureFilter(texture, TEXTURE_FILTER_POINT);
            return texture;
        }
    }

    const Texture2D& get(const std::string& path) {
        return get_resized(path, 0);
    }

    const Texture2D& get_resized(const std::string& path, int target_size) {
        auto it = textures.find(path);
        if (it != textures.end()) {
            return it->second;
        }
        return textures.emplace(path, LoadAndUpload(path, target_size)).first->second;
    }

    void unload_all() {
        for (auto& [path, texture] : textures) {
            UnloadTexture(texture);
        }
        textures.clear();
    }
}
