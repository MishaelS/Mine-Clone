#include "content/BlockFile.hpp"
#include "core/Json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace block_file {

    namespace {
        template <size_t N>
        int index_of(const char* const (&ids)[N], const std::string& id, int fallback) {
            for (size_t i = 0; i < N; ++i) {
                if (id == ids[i]) return static_cast<int>(i);
            }
            return fallback;
        }

        std::string number(float value) {
            std::ostringstream out;
            out << value;
            return out.str();
        }

        std::string escape(const std::string& text) {
            std::string out;
            for (char c : text) {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            return out;
        }
    }

    std::string directory() {
        return std::string(ASSETS_PATH) + "blocks/";
    }

    std::optional<BlockFile> load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        std::stringstream text;
        text << in.rdbuf();
        try {
            const Json root = Json::parse(text.str());
            BlockFile block;
            block.id = static_cast<int>(root["id"].as_number(0));
            block.name = root["name"].as_string();
            if (block.id <= 0 || block.id > 255 || block.name.empty()) return std::nullopt;
            block.sound     = index_of(SOUND_IDS, root["sound"].as_string("stone"), 4);
            block.hardness  = static_cast<float>(root["hardness"].as_number(block.hardness));
            block.tool      = index_of(TOOL_IDS, root["tool"].as_string("none"), 0);
            block.density   = static_cast<float>(root["density"].as_number(block.density));
            block.luminance = std::clamp(static_cast<int>(root["luminance"].as_number(0)), 0, 15);
            block.solid            = root["solid"].as_bool(true);
            block.selectable       = root["selectable"].as_bool(true);
            block.replaceable      = root["replaceable"].as_bool(false);
            block.transparent      = root["transparent"].as_bool(false);
            block.translucent      = root["translucent"].as_bool(false);
            block.cutout           = root["cutout"].as_bool(false);
            block.keep_same_faces  = root["keep_same_faces"].as_bool(false);
            block.damages_on_touch = root["damages_on_touch"].as_bool(false);
            block.directional = root["directional"].as_bool(false);
            block.side_inset = std::clamp(static_cast<int>(root["side_inset"].as_number(0)), 0, 8);
            for (int f = 0; f < 6; ++f) {
                const Json& face = root["faces"][FACE_IDS[f]];
                const std::vector<Json>& tile = face["tile"].as_array();
                if (tile.size() >= 2) {
                    block.faces[f].tile_x = std::clamp(static_cast<int>(tile[0].as_number()), 0, ATLAS_TILES - 1);
                    block.faces[f].tile_y = std::clamp(static_cast<int>(tile[1].as_number()), 0, ATLAS_TILES - 1);
                }

                const std::vector<Json>& tint = face["tint"].as_array();
                if (tint.size() >= 3) {
                    auto channel = [&](size_t i, double fallback) {
                        return static_cast<unsigned char>(std::clamp(i < tint.size() ? tint[i].as_number(fallback) : fallback, 0.0, 255.0));
                    };
                    block.faces[f].tint = {channel(0, 255), channel(1, 255), channel(2, 255), channel(3, 255)};
                }
            }
            return block;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    bool save(const BlockFile& block, const std::string& path) {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "{\n";
        out << "  \"id\": " << block.id << ",\n";
        out << "  \"name\": \"" << escape(block.name) << "\",\n";
        out << "  \"sound\": \"" << SOUND_IDS[std::clamp(block.sound, 0, SOUND_COUNT - 1)] << "\",\n";
        out << "  \"hardness\": " << number(block.hardness) << ",\n";
        out << "  \"tool\": \"" << TOOL_IDS[std::clamp(block.tool, 0, TOOL_COUNT - 1)] << "\",\n";
        out << "  \"density\": " << number(block.density) << ",\n";
        out << "  \"luminance\": " << block.luminance << ",\n";
        // Only what differs from an ordinary solid cube.
        if (!block.solid) out << "  \"solid\": false,\n";
        if (!block.selectable) out << "  \"selectable\": false,\n";
        if (block.replaceable) out << "  \"replaceable\": true,\n";
        if (block.transparent) out << "  \"transparent\": true,\n";
        if (block.translucent) out << "  \"translucent\": true,\n";
        if (block.cutout) out << "  \"cutout\": true,\n";
        if (block.keep_same_faces) out << "  \"keep_same_faces\": true,\n";
        if (block.damages_on_touch) out << "  \"damages_on_touch\": true,\n";
        if (block.directional) out << "  \"directional\": true,\n";
        if (block.side_inset != 0) out << "  \"side_inset\": " << block.side_inset << ",\n";
        out << "  \"faces\": {\n";
        for (int f = 0; f < 6; ++f) {
            const Face& face = block.faces[f];
            out << "    \"" << FACE_IDS[f] << "\": { \"tile\": [" << face.tile_x << ", " << face.tile_y << "]";
            if (face.tint.r != 255 || face.tint.g != 255 || face.tint.b != 255 || face.tint.a != 255) {
                out << ", \"tint\": [" << static_cast<int>(face.tint.r) << ", " << static_cast<int>(face.tint.g) << ", "
                    << static_cast<int>(face.tint.b) << ", " << static_cast<int>(face.tint.a) << "]";
            }
            out << " }" << (f < 5 ? "," : "") << "\n";
        }
        out << "  }\n}\n";
        return static_cast<bool>(out);
    }

    std::vector<BlockFile> load_all() {
        std::vector<BlockFile> blocks;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(directory(), error)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            if (std::optional<BlockFile> block = load(entry.path().string())) {
                blocks.push_back(std::move(*block));
            } else {
                TraceLog(LOG_WARNING, "block file '%s' could not be read", entry.path().string().c_str());
            }
        }
        std::sort(blocks.begin(), blocks.end(), [](const BlockFile& a, const BlockFile& b) { return a.id < b.id; });
        return blocks;
    }

} // namespace block_file
