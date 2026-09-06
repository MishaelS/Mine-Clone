#include "core/WorldSave.hpp"
#include "core/Block.hpp"
#include "core/Json.hpp"

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {
    constexpr const char* SAVES_ROOT = SAVE_DATA_PATH "saves";

    std::string read_whole_file(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    // Minimal escaping for the one place a player-typed string ends up
    // inside hand-written JSON text - Json's own reader only understands
    // \", \\, \n, \t (see Json.cpp's parse_string), so that's all this
    // needs to produce.
    std::string json_escape(const std::string& text)
    {
        std::string out;
        out.reserve(text.size());
        for (char c : text) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\t': out += "\\t"; break;
                default: out += c; break;
            }
        }
        return out;
    }

    const char* game_mode_json_value(GameMode mode)
    {
        return mode == GameMode::Survival ? "survival" : "creative";
    }

    GameMode game_mode_from_json_value(const std::string& value)
    {
        return value == "survival" ? GameMode::Survival : GameMode::Creative;
    }

    // Same mixing spirit as Chunk.cpp's cave_chunk_seed - a small,
    // deterministic, non-cryptographic hash, not std::hash<std::string>
    // (unspecified/unstable across implementations, unacceptable for a
    // seed players might want to share).
    uint32_t fnv1a(const std::string& text)
    {
        uint32_t hash = 2166136261u;
        for (unsigned char c : text) {
            hash ^= c;
            hash *= 16777619u;
        }
        return hash;
    }

    std::string trim(const std::string& text)
    {
        size_t begin = text.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return {};
        size_t end = text.find_last_not_of(" \t\r\n");
        return text.substr(begin, end - begin + 1);
    }
}

namespace WorldSave {

std::string sanitize_folder_name(const std::string& world_name)
{
    std::string trimmed = trim(world_name);
    std::string result;
    result.reserve(trimmed.size());
    for (unsigned char c : trimmed) {
        if (c == '/' || c == '\\' || c == ':' || c < 0x20) continue; // path separators, drive-letter colon, control chars
        result += static_cast<char>(c);
    }
    result = trim(result);
    return result.empty() ? "world" : result;
}

std::string world_directory(const std::string& folder_name)
{
    return std::string(SAVES_ROOT) + "/" + folder_name;
}

std::string next_available_folder_name(const std::string& world_name)
{
    std::string base = sanitize_folder_name(world_name);
    if (!fs::exists(world_directory(base))) return base;

    for (int suffix = 2; suffix < 10000; ++suffix) {
        std::string candidate = base + " (" + std::to_string(suffix) + ")";
        if (!fs::exists(world_directory(candidate))) return candidate;
    }
    return base + " (" + std::to_string(std::rand()) + ")"; // astronomically unlikely fallback
}

uint32_t derive_seed(const std::string& world_name, const std::string& seed_text)
{
    std::string trimmed = trim(seed_text);
    if (trimmed.empty()) return fnv1a(world_name);

    long long value = 0;
    const char* begin = trimmed.data();
    const char* end = trimmed.data() + trimmed.size();
    auto result = std::from_chars(begin, end, value);
    if (result.ec == std::errc() && result.ptr == end) {
        return static_cast<uint32_t>(value);
    }
    return fnv1a(trimmed);
}

bool create_world(const WorldInfo& info)
{
    std::string dir = world_directory(info.folder_name);
    std::error_code ec;
    fs::create_directories(dir + "/chunks", ec);
    if (ec) return false;

    std::ofstream out(dir + "/world.json", std::ios::binary | std::ios::trunc);
    if (!out) return false;

    out << "{\n";
    out << "  \"display_name\": \"" << json_escape(info.display_name) << "\",\n";
    out << "  \"seed\": " << info.seed << ",\n";
    out << "  \"game_mode\": \"" << game_mode_json_value(info.game_mode) << "\"\n";
    out << "}\n";

    return static_cast<bool>(out);
}

bool delete_world(const std::string& folder_name)
{
    std::error_code ec;
    fs::remove_all(world_directory(folder_name), ec);
    return !ec;
}

std::optional<WorldInfo> load_world_info(const std::string& folder_name)
{
    std::string text = read_whole_file(world_directory(folder_name) + "/world.json");
    if (text.empty()) return std::nullopt;

    try {
        Json root = Json::parse(text);
        WorldInfo info;
        info.folder_name = folder_name;
        info.display_name = root["display_name"].as_string(folder_name);
        info.seed = static_cast<uint32_t>(root["seed"].as_number(0));
        info.game_mode = game_mode_from_json_value(root["game_mode"].as_string("creative"));
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<WorldInfo> list_worlds()
{
    std::vector<WorldInfo> worlds;
    std::error_code ec;
    if (!fs::exists(SAVES_ROOT, ec)) return worlds;

    for (const auto& entry : fs::directory_iterator(SAVES_ROOT, ec)) {
        if (!entry.is_directory()) continue;
        if (auto info = load_world_info(entry.path().filename().string())) {
            worlds.push_back(std::move(*info));
        }
    }
    return worlds;
}

bool save_player_state(const std::string& folder_name, const PlayerSaveState& state)
{
    std::ofstream out(world_directory(folder_name) + "/player.json", std::ios::binary | std::ios::trunc);
    if (!out) return false;

    out << "{\n";
    out << "  \"position\": { \"x\": " << state.position.x << ", \"y\": " << state.position.y << ", \"z\": " << state.position.z << " },\n";
    out << "  \"forward\": { \"x\": " << state.forward.x << ", \"y\": " << state.forward.y << ", \"z\": " << state.forward.z << " },\n";
    out << "  \"hotbar\": [";
    for (size_t i = 0; i < state.inventory.hotbar.size(); ++i) {
        out << "\"" << get_block_name(state.inventory.hotbar[i]) << "\"";
        if (i + 1 < state.inventory.hotbar.size()) out << ", ";
    }
    out << "],\n";
    out << "  \"selected_slot\": " << state.inventory.selected_slot << "\n";
    out << "}\n";

    return static_cast<bool>(out);
}

std::optional<PlayerSaveState> load_player_state(const std::string& folder_name)
{
    std::string text = read_whole_file(world_directory(folder_name) + "/player.json");
    if (text.empty()) return std::nullopt;

    try {
        Json root = Json::parse(text);
        PlayerSaveState state;
        state.position = {
            static_cast<float>(root["position"]["x"].as_number(0.0)),
            static_cast<float>(root["position"]["y"].as_number(0.0)),
            static_cast<float>(root["position"]["z"].as_number(0.0)),
        };
        state.forward = {
            static_cast<float>(root["forward"]["x"].as_number(0.0)),
            static_cast<float>(root["forward"]["y"].as_number(0.0)),
            static_cast<float>(root["forward"]["z"].as_number(-1.0)),
        };

        const std::vector<Json>& hotbar_json = root["hotbar"].as_array();
        if (hotbar_json.size() != HOTBAR_SIZE) return std::nullopt; // corrupted/foreign format - discard, don't partial-fill

        for (size_t i = 0; i < hotbar_json.size(); ++i) {
            std::optional<BlockType> type = block_type_from_name(hotbar_json[i].as_string());
            if (!type) return std::nullopt; // unknown block name - same treatment as any other corruption
            state.inventory.hotbar[i] = *type;
        }
        state.inventory.selected_slot = static_cast<int>(root["selected_slot"].as_number(0));
        if (state.inventory.selected_slot < 0 || state.inventory.selected_slot >= HOTBAR_SIZE) {
            state.inventory.selected_slot = 0;
        }

        return state;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}
