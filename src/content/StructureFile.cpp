#include "content/StructureFile.hpp"
#include "core/Json.hpp"

#include "raylib.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace structure_file {

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

        void grow(Bounds& bounds, const Block& block) {
            if (bounds.empty) {
                bounds = {block.x, block.y, block.z, block.x, block.y, block.z, false};
                return;
            }
            bounds.min_x = std::min(bounds.min_x, block.x);
            bounds.min_y = std::min(bounds.min_y, block.y);
            bounds.min_z = std::min(bounds.min_z, block.z);
            bounds.max_x = std::max(bounds.max_x, block.x);
            bounds.max_y = std::max(bounds.max_y, block.y);
            bounds.max_z = std::max(bounds.max_z, block.z);
        }
    }

    Bounds bounds(const Variant& variant) {
        Bounds result;
        for (const Block& block : variant.blocks) grow(result, block);
        return result;
    }

    Bounds bounds(const StructureFile& structure) {
        Bounds result;
        for (const Variant& variant : structure.variants) {
            for (const Block& block : variant.blocks) grow(result, block);
        }
        return result;
    }

    Block* block_at(Variant& variant, int x, int y, int z) {
        for (Block& block : variant.blocks) {
            if (block.x == x && block.y == y && block.z == z) return &block;
        }
        return nullptr;
    }

    const Block* block_at(const Variant& variant, int x, int y, int z) {
        return block_at(const_cast<Variant&>(variant), x, y, z);
    }

    bool valid_name(const std::string& name) {
        if (name.empty()) return false;
        return std::all_of(name.begin(), name.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
    }

    std::string directory() {
        return std::string(ASSETS_PATH) + "structures/";
    }

    std::optional<StructureFile> load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        std::stringstream text;
        text << in.rdbuf();
        try {
            const Json root = Json::parse(text.str());
            StructureFile structure;
            structure.name = root["name"].as_string();
            if (structure.name.empty()) return std::nullopt;

            for (const Json& variant_json : root["variants"].as_array()) {
                Variant variant;
                for (const Json& block_json : variant_json["blocks"].as_array()) {
                    const std::vector<Json>& at = block_json["at"].as_array();
                    if (at.size() < 3) continue;
                    Block block;
                    block.x = static_cast<int>(at[0].as_number());
                    block.y = static_cast<int>(at[1].as_number());
                    block.z = static_cast<int>(at[2].as_number());
                    block.block = block_json["block"].as_string();
                    if (block.block.empty()) continue;
                    block.replace = index_of(REPLACE_IDS, block_json["replace"].as_string("air"), 0);
                    block.required = block_json["required"].as_bool(false);
                    // One block per cell: a later one wins.
                    if (Block* existing = block_at(variant, block.x, block.y, block.z)) *existing = block;
                    else variant.blocks.push_back(block);
                }
                structure.variants.push_back(std::move(variant));
            }

            const Json& generation = root["generation"];
            for (const Json& name : generation["on"].as_array()) {
                if (!name.as_string().empty()) structure.placed_on.push_back(name.as_string());
            }
            for (int b = 0; b < BIOME_COUNT; ++b) {
                structure.chance[static_cast<size_t>(b)] = std::clamp(static_cast<float>(generation["chance"][BIOME_IDS[b]].as_number(0.0)), 0.0f, 1.0f);
            }
            structure.tree_density = generation["tree_density"].as_bool(false);
            structure.grows_from = root["grows_from"].as_string();
            return structure;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    bool save(const StructureFile& structure, const std::string& path) {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "{\n";
        out << "  \"name\": \"" << escape(structure.name) << "\",\n";
        if (!structure.grows_from.empty()) out << "  \"grows_from\": \"" << escape(structure.grows_from) << "\",\n";

        out << "  \"generation\": {\n";
        out << "    \"on\": [";
        for (size_t i = 0; i < structure.placed_on.size(); ++i) {
            out << (i == 0 ? "" : ", ") << "\"" << escape(structure.placed_on[i]) << "\"";
        }
        out << "],\n";
        if (structure.tree_density) out << "    \"tree_density\": true,\n";
        // Only the biomes it's found in.
        out << "    \"chance\": {";
        bool first = true;
        for (int b = 0; b < BIOME_COUNT; ++b) {
            const float chance = structure.chance[static_cast<size_t>(b)];
            if (chance <= 0.0f) continue;
            out << (first ? " " : ", ") << "\"" << BIOME_IDS[b] << "\": " << number(chance);
            first = false;
        }
        out << (first ? "}\n" : " }\n");
        out << "  },\n";

        out << "  \"variants\": [\n";
        for (size_t v = 0; v < structure.variants.size(); ++v) {
            // Bottom to top, then along z and x - a file that reads layer by layer.
            std::vector<Block> blocks = structure.variants[v].blocks;
            std::sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) {
                if (a.y != b.y) return a.y < b.y;
                if (a.z != b.z) return a.z < b.z;
                return a.x < b.x;
            });
            out << "    { \"blocks\": [\n";
            for (size_t i = 0; i < blocks.size(); ++i) {
                const Block& block = blocks[i];
                out << "      { \"at\": [" << block.x << ", " << block.y << ", " << block.z << "], \"block\": \"" << escape(block.block) << "\"";
                if (block.replace != 0) out << ", \"replace\": \"" << REPLACE_IDS[std::clamp(block.replace, 0, REPLACE_COUNT - 1)] << "\"";
                if (block.required) out << ", \"required\": true";
                out << " }" << (i + 1 < blocks.size() ? "," : "") << "\n";
            }
            out << "    ] }" << (v + 1 < structure.variants.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return static_cast<bool>(out);
    }

    std::vector<StructureFile> load_all() {
        std::vector<StructureFile> structures;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(directory(), error)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            if (std::optional<StructureFile> structure = load(entry.path().string())) {
                structures.push_back(std::move(*structure));
            } else {
                TraceLog(LOG_WARNING, "structure file '%s' could not be read", entry.path().string().c_str());
            }
        }
        std::sort(structures.begin(), structures.end(), [](const StructureFile& a, const StructureFile& b) { return a.name < b.name; });
        return structures;
    }

} // namespace structure_file
