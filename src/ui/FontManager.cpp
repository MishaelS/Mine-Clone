#include "ui/FontManager.hpp"
#include "ui/Localization.hpp"

#include <set>
#include <vector>

namespace FontManager {
    namespace {
        constexpr const char* FONT_PATH = ASSETS_PATH "fonts/Minecraft Rus/minecraft.ttf";

        // Baked at this pixel size - larger than any current on-screen text
        // (the debug overlay draws at 18px) so scaling down still looks
        // crisp; raylib upsamples a smaller baked size instead of re-baking
        // when text is drawn larger.
        constexpr int BASE_FONT_SIZE = 48;

        Font font{};
        bool loaded = false;

        // Basic Latin + Cyrillic (raylib's default set, 0-255, would leave
        // every Cyrillic glyph missing), plus every character any loaded
        // translation file uses - so a newly added language's own letters
        // get baked too, as far as the .ttf itself actually has them.
        std::vector<int> build_codepoints() {
            std::set<int> codepoints;
            for (int c = 0x0020; c <= 0x007E; ++c) codepoints.insert(c); // basic Latin
            for (int c = 0x0400; c <= 0x04FF; ++c) codepoints.insert(c); // Cyrillic
            for (int c : ui::translation_codepoints()) {
                if (c >= 0x20) codepoints.insert(c);
            }
            return {codepoints.begin(), codepoints.end()};
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
