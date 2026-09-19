#include "core/WorldSave.hpp"
#include "core/Block.hpp"
#include "core/Json.hpp"
#include "player/Item.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {
    constexpr const char* SAVES_ROOT = SAVE_DATA_PATH "saves";

    std::string read_whole_file(const fs::path& path) {
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
    std::string json_escape(const std::string& text) {
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

    // Shared by player.json's hotbar/inventory arrays and items.json's own
    // dropped-item list (see save_dropped_items()/load_dropped_items()) -
    // one canonical ItemStack<->JSON mapping instead of two copies that
    // could quietly drift apart.
    void write_item_stack(std::ostream& out, const ItemStack& stack) {
        if (stack.empty()) {
            out << "{ \"type\": \"air\", \"count\": 0 }";
        } else if (stack.is_tool()) {
            out << "{ \"tool\": \"" << get_item_name(stack.tool) << "\", \"durability\": " << stack.durability << " }";
        } else if (stack.is_material()) {
            out << "{ \"item\": \"" << get_item_name(stack.tool) << "\", \"count\": " << stack.count << " }";
        } else {
            out << "{ \"type\": \"" << get_block_name(stack.block) << "\", \"count\": " << stack.count << " }";
        }
    }

    std::optional<ItemStack> read_item_stack(const Json& value) {
        // Legacy saves stored each hotbar entry as a plain block name.
        if (value.get_type() == Json::Type::String) {
            std::string name = value.as_string();
            if (name == "air") return ItemStack{};
            std::optional<BlockType> type = block_type_from_name(name);
            if (!type) return std::nullopt;
            ItemStack stack;
            stack.block = *type;
            stack.count = 1;
            return stack;
        }

        if (value["tool"].get_type() == Json::Type::String) {
            std::optional<ItemType> tool_type = item_type_from_name(value["tool"].as_string());
            if (!tool_type) return std::nullopt;
            int max_durability = get_item_properties(*tool_type).max_durability;
            int durability = std::clamp(
                static_cast<int>(value["durability"].as_number(max_durability)), 1, max_durability);
            ItemStack stack;
            stack.tool = *tool_type;
            stack.count = 1;
            stack.durability = durability;
            return stack;
        }

        if (value["item"].get_type() == Json::Type::String) {
            std::optional<ItemType> item_type = item_type_from_name(value["item"].as_string());
            if (!item_type) return std::nullopt;
            int count = static_cast<int>(value["count"].as_number(0));
            if (count <= 0) return ItemStack{};
            ItemStack stack;
            stack.tool = *item_type;
            stack.count = std::clamp(count, 1, MAX_ITEM_STACK);
            return stack;
        }

        std::string name = value["type"].as_string("air");
        int count = static_cast<int>(value["count"].as_number(0));
        if (name == "air" || count <= 0) return ItemStack{};
        std::optional<BlockType> type = block_type_from_name(name);
        if (!type) return std::nullopt;
        ItemStack stack;
        stack.block = *type;
        stack.count = std::clamp(count, 1, MAX_ITEM_STACK);
        return stack;
    }

    const char* game_mode_json_value(GameMode mode) {
        return mode == GameMode::Survival ? "survival" : "creative";
    }

    GameMode game_mode_from_json_value(const std::string& value) {
        return value == "survival" ? GameMode::Survival : GameMode::Creative;
    }

    // Same mixing spirit as Chunk.cpp's cave_chunk_seed - a small,
    // deterministic, non-cryptographic hash, not std::hash<std::string>
    // (unspecified/unstable across implementations, unacceptable for a
    // seed players might want to share).
    uint32_t fnv1a(const std::string& text) {
        uint32_t hash = 2166136261u;
        for (unsigned char c : text) {
            hash ^= c;
            hash *= 16777619u;
        }
        return hash;
    }

    std::string trim(const std::string& text) {
        size_t begin = text.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return {};
        size_t end = text.find_last_not_of(" \t\r\n");
        return text.substr(begin, end - begin + 1);
    }
}

namespace WorldSave {
    std::string sanitize_folder_name(const std::string& world_name) {
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

    std::string world_directory(const std::string& folder_name) {
        return std::string(SAVES_ROOT) + "/" + folder_name;
    }

    std::string next_available_folder_name(const std::string& world_name) {
        std::string base = sanitize_folder_name(world_name);
        if (!fs::exists(world_directory(base))) return base;

        for (int suffix = 2; suffix < 10000; ++suffix) {
            std::string candidate = base + " (" + std::to_string(suffix) + ")";
            if (!fs::exists(world_directory(candidate))) return candidate;
        }
        return base + " (" + std::to_string(std::rand()) + ")"; // astronomically unlikely fallback
    }

    uint32_t derive_seed(const std::string& world_name, const std::string& seed_text) {
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

    bool write_world_info_file(const WorldInfo& info) {
        std::string dir = world_directory(info.folder_name);
        std::ofstream out(dir + "/world.json", std::ios::binary | std::ios::trunc);
        if (!out) return false;

        out << "{\n";
        out << "  \"display_name\": \"" << json_escape(info.display_name) << "\",\n";
        out << "  \"seed\": " << info.seed << ",\n";
        out << "  \"game_mode\": \"" << game_mode_json_value(info.game_mode) << "\",\n";
        out << "  \"allow_commands\": " << (info.allow_commands ? "true" : "false") << "\n";
        out << "}\n";

        return static_cast<bool>(out);
    }

    bool create_world(const WorldInfo& info) {
        std::string dir = world_directory(info.folder_name);
        std::error_code ec;
        fs::create_directories(dir + "/chunks", ec);
        if (ec) return false;

        return write_world_info_file(info);
    }

    bool save_world_info(const WorldInfo& info) {
        if (info.folder_name.empty()) return false;
        if (!fs::exists(world_directory(info.folder_name))) return false;
        return write_world_info_file(info);
    }

    bool delete_world(const std::string& folder_name) {
        std::error_code ec;
        fs::remove_all(world_directory(folder_name), ec);
        return !ec;
    }

    std::optional<WorldInfo> load_world_info(const std::string& folder_name) {
        std::string text = read_whole_file(world_directory(folder_name) + "/world.json");
        if (text.empty()) return std::nullopt;

        try {
            Json root = Json::parse(text);
            WorldInfo info;
            info.folder_name = folder_name;
            info.display_name = root["display_name"].as_string(folder_name);
            info.seed = static_cast<uint32_t>(root["seed"].as_number(0));
            info.game_mode = game_mode_from_json_value(root["game_mode"].as_string("creative"));
            info.allow_commands = root["allow_commands"].as_bool(true);
            return info;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    std::vector<WorldInfo> list_worlds() {
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

    bool save_player_state(const std::string& folder_name, const PlayerSaveState& state) {
        std::ofstream out(world_directory(folder_name) + "/player.json", std::ios::binary | std::ios::trunc);
        if (!out) return false;

        out << "{\n";
        out << "  \"position\": { \"x\": " << state.position.x << ", \"y\": " << state.position.y << ", \"z\": " << state.position.z << " },\n";
        out << "  \"forward\": { \"x\": " << state.forward.x << ", \"y\": " << state.forward.y << ", \"z\": " << state.forward.z << " },\n";
        out << "  \"hotbar\": [";
        for (size_t i = 0; i < state.inventory.hotbar.size(); ++i) {
            write_item_stack(out, state.inventory.hotbar[i]);
            if (i + 1 < state.inventory.hotbar.size()) out << ", ";
        }
        out << "],\n";
        out << "  \"inventory\": [";
        for (size_t i = 0; i < state.inventory.storage.size(); ++i) {
            write_item_stack(out, state.inventory.storage[i]);
            if (i + 1 < state.inventory.storage.size()) out << ", ";
        }
        out << "],\n";
        out << "  \"selected_slot\": " << state.inventory.selected_slot << ",\n";
        out << "  \"health\": " << state.health << ",\n";
        out << "  \"game_tick\": " << state.game_tick << "\n";
        out << "}\n";

        return static_cast<bool>(out);
    }

    std::optional<PlayerSaveState> load_player_state(const std::string& folder_name) {
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
                std::optional<ItemStack> stack = read_item_stack(hotbar_json[i]);
                if (!stack) return std::nullopt;
                state.inventory.hotbar[i] = *stack;
            }
            const std::vector<Json>& inventory_json = root["inventory"].as_array();
            if (!inventory_json.empty() && inventory_json.size() != INVENTORY_STORAGE_SIZE) return std::nullopt;
            for (size_t i = 0; i < inventory_json.size(); ++i) {
                std::optional<ItemStack> stack = read_item_stack(inventory_json[i]);
                if (!stack) return std::nullopt;
                state.inventory.storage[i] = *stack;
            }
            state.inventory.selected_slot = static_cast<int>(root["selected_slot"].as_number(0));
            if (state.inventory.selected_slot < 0 || state.inventory.selected_slot >= HOTBAR_SIZE) {
                state.inventory.selected_slot = 0;
            }

            // Missing (a save from before health existed) defaults to full
            // health (20 - PlayerHealth::MAX_HEALTH, not included here just
            // for this one constant - see the header's own comment).
            state.health = std::clamp(static_cast<int>(root["health"].as_number(20.0)), 0, 20);

            // Missing (a save from before day/night existed) defaults to 0
            // (dawn, day 0) via as_number()'s own fallback.
            double game_tick = root["game_tick"].as_number(0.0);
            state.game_tick = game_tick > 0.0 ? static_cast<uint64_t>(game_tick) : 0;

            return state;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    bool save_dropped_items(const std::string& folder_name, const std::vector<DroppedItemSaveState>& items) {
        std::ofstream out(world_directory(folder_name) + "/items.json", std::ios::binary | std::ios::trunc);
        if (!out) return false;

        out << "{\n  \"items\": [\n";
        for (size_t i = 0; i < items.size(); ++i) {
            const DroppedItemSaveState& item = items[i];
            out << "    { \"position\": { \"x\": " << item.position.x << ", \"y\": " << item.position.y
                << ", \"z\": " << item.position.z << " }, \"stack\": ";
            write_item_stack(out, item.stack);
            out << ", \"age\": " << item.age;
            if (item.block_tint) {
                out << ", \"tint\": { \"r\": " << static_cast<int>(item.block_tint->r)
                    << ", \"g\": " << static_cast<int>(item.block_tint->g)
                    << ", \"b\": " << static_cast<int>(item.block_tint->b) << " }";
            }
            out << " }";
            if (i + 1 < items.size()) out << ",";
            out << "\n";
        }
        out << "  ]\n}\n";

        return static_cast<bool>(out);
    }

    std::vector<DroppedItemSaveState> load_dropped_items(const std::string& folder_name) {
        std::vector<DroppedItemSaveState> result;
        std::string text = read_whole_file(world_directory(folder_name) + "/items.json");
        if (text.empty()) return result;

        try {
            Json root = Json::parse(text);
            for (const Json& entry : root["items"].as_array()) {
                std::optional<ItemStack> stack = read_item_stack(entry["stack"]);
                // An unresolvable stack (a name from a newer/foreign build)
                // skips just this one item rather than discarding the
                // whole file the way a corrupted player.json is discarded
                // wholesale - one bad entry among many independent ones
                // shouldn't cost every other item still on the ground.
                if (!stack || stack->empty()) continue;

                DroppedItemSaveState item;
                item.stack = *stack;
                item.position = {
                    static_cast<float>(entry["position"]["x"].as_number(0.0)),
                    static_cast<float>(entry["position"]["y"].as_number(0.0)),
                    static_cast<float>(entry["position"]["z"].as_number(0.0)),
                };
                item.age = std::max(0.0f, static_cast<float>(entry["age"].as_number(0.0)));
                if (entry["tint"].get_type() == Json::Type::Object) {
                    item.block_tint = Color{
                        static_cast<unsigned char>(entry["tint"]["r"].as_number(255)),
                        static_cast<unsigned char>(entry["tint"]["g"].as_number(255)),
                        static_cast<unsigned char>(entry["tint"]["b"].as_number(255)),
                        255,
                    };
                }
                result.push_back(item);
            }
        } catch (const std::exception&) {
            return {};
        }
        return result;
    }

    bool save_chests(const std::string& folder_name, const std::vector<ChestSaveState>& chests) {
        std::ofstream out(world_directory(folder_name) + "/chests.json", std::ios::binary | std::ios::trunc);
        if (!out) return false;

        out << "{\n  \"chests\": [\n";
        bool wrote_any = false;
        for (const ChestSaveState& chest : chests) {
            // A chest that was opened but never actually had anything put
            // in it (or had everything taken back out) has nothing worth
            // writing - skipping it keeps this file proportional to actual
            // player activity instead of growing by one entry per chest
            // ever merely looked at.
            bool has_any_item = false;
            for (const ItemStack& slot : chest.slots) {
                if (!slot.empty()) { has_any_item = true; break; }
            }
            if (!has_any_item) continue;

            if (wrote_any) out << ",\n";
            wrote_any = true;
            out << "    { \"x\": " << chest.x << ", \"y\": " << chest.y << ", \"z\": " << chest.z
                << ", \"slots\": [";
            for (size_t i = 0; i < chest.slots.size(); ++i) {
                write_item_stack(out, chest.slots[i]);
                if (i + 1 < chest.slots.size()) out << ", ";
            }
            out << "] }";
        }
        out << "\n  ]\n}\n";

        return static_cast<bool>(out);
    }

    std::vector<ChestSaveState> load_chests(const std::string& folder_name) {
        std::vector<ChestSaveState> result;
        std::string text = read_whole_file(world_directory(folder_name) + "/chests.json");
        if (text.empty()) return result;

        try {
            Json root = Json::parse(text);
            for (const Json& entry : root["chests"].as_array()) {
                const std::vector<Json>& slots_json = entry["slots"].as_array();
                // A foreign/corrupted entry (wrong slot count) is skipped,
                // same "one bad entry doesn't cost every other chest"
                // reasoning as load_dropped_items().
                if (slots_json.size() != INVENTORY_STORAGE_SIZE) continue;

                ChestSaveState chest;
                chest.x = static_cast<int>(entry["x"].as_number(0));
                chest.y = static_cast<int>(entry["y"].as_number(0));
                chest.z = static_cast<int>(entry["z"].as_number(0));
                bool all_resolved = true;
                for (size_t i = 0; i < slots_json.size(); ++i) {
                    std::optional<ItemStack> stack = read_item_stack(slots_json[i]);
                    if (!stack) { all_resolved = false; break; }
                    chest.slots[i] = *stack;
                }
                if (!all_resolved) continue;
                result.push_back(chest);
            }
        } catch (const std::exception&) {
            return {};
        }
        return result;
    }
    bool save_furnaces(const std::string& folder_name, const std::vector<FurnaceSaveState>& furnaces) {
        std::ofstream out(world_directory(folder_name) + "/furnaces.json", std::ios::binary | std::ios::trunc);
        if (!out) return false;

        out << "{\n  \"furnaces\": [\n";
        bool wrote_any = false;
        for (const FurnaceSaveState& furnace : furnaces) {
            // Nothing in it and not burning - nothing worth restoring.
            if (furnace.state.empty()) continue;
            if (wrote_any) out << ",\n";
            wrote_any = true;
            out << "    { \"x\": " << furnace.x << ", \"y\": " << furnace.y << ", \"z\": " << furnace.z
                << ", \"input\": ";
            write_item_stack(out, furnace.state.input);
            out << ", \"fuel\": ";
            write_item_stack(out, furnace.state.fuel);
            out << ", \"output\": ";
            write_item_stack(out, furnace.state.output);
            out << ", \"burn_ticks_left\": " << furnace.state.burn_ticks_left
                << ", \"burn_ticks_total\": " << furnace.state.burn_ticks_total
                << ", \"cook_ticks\": " << furnace.state.cook_ticks << " }";
        }
        out << "\n  ]\n}\n";
        return static_cast<bool>(out);
    }

    std::vector<FurnaceSaveState> load_furnaces(const std::string& folder_name) {
        std::vector<FurnaceSaveState> result;
        std::string text = read_whole_file(world_directory(folder_name) + "/furnaces.json");
        if (text.empty()) return result;

        try {
            Json root = Json::parse(text);
            for (const Json& entry : root["furnaces"].as_array()) {
                std::optional<ItemStack> input = read_item_stack(entry["input"]);
                std::optional<ItemStack> fuel = read_item_stack(entry["fuel"]);
                std::optional<ItemStack> output = read_item_stack(entry["output"]);
                // Same "one bad entry doesn't cost every other one" rule as
                // load_chests().
                if (!input || !fuel || !output) continue;

                FurnaceSaveState furnace;
                furnace.x = static_cast<int>(entry["x"].as_number(0));
                furnace.y = static_cast<int>(entry["y"].as_number(0));
                furnace.z = static_cast<int>(entry["z"].as_number(0));
                furnace.state.input = *input;
                furnace.state.fuel = *fuel;
                furnace.state.output = *output;
                furnace.state.burn_ticks_left = std::max(0, static_cast<int>(entry["burn_ticks_left"].as_number(0)));
                furnace.state.burn_ticks_total = std::max(0, static_cast<int>(entry["burn_ticks_total"].as_number(0)));
                furnace.state.cook_ticks = std::max(0, static_cast<int>(entry["cook_ticks"].as_number(0)));
                result.push_back(furnace);
            }
        } catch (const std::exception&) {
            return {};
        }
        return result;
    }
}
