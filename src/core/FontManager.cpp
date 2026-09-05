#include "core/FontManager.hpp"

#include <vector>

namespace FontManager {
    namespace {
        constexpr const char* FONT_PATH = ASSETS_PATH "fonts/Minecraft Rus/minecraft.ttf";

        // Baked at this pixel size — larger than any current on-screen text
        // (the debug overlay draws at 18px) so scaling down still looks
        // crisp; raylib upsamples a smaller baked size instead of re-baking
        // when text is drawn larger.
        constexpr int BASE_FONT_SIZE = 48;

        Font font{};
        bool loaded = false;

        // Basic Latin + Cyrillic, since this is a Latin+Cyrillic font meant
        // for Russian text — raylib's default codepoint set (0-255) would
        // leave every Cyrillic glyph missing.
        std::vector<int> build_codepoints() {
            std::vector<int> codepoints;
            for (int c = 0x0020; c <= 0x007E; ++c) codepoints.push_back(c); // basic Latin
            for (int c = 0x0400; c <= 0x04FF; ++c) codepoints.push_back(c); // Cyrillic
            return codepoints;
        }
    }

    const Font& get() {
        if (!loaded) {
            std::vector<int> codepoints = build_codepoints();
            font = LoadFontEx(FONT_PATH, BASE_FONT_SIZE, codepoints.data(), static_cast<int>(codepoints.size()));
            // Point filtering keeps the font's blocky pixel-art style crisp
            // instead of smearing it the way bilinear scaling would (same
            // reasoning as TextureManager's block textures).
            SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
            loaded = true;
        }
        return font;
    }

    void unload() {
        if (loaded) {
            UnloadFont(font);
            loaded = false;
        }
    }
}
