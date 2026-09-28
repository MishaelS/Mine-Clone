#include "content/BlockFile.hpp"
#include "core/BlockShape.hpp"
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

        std::optional<Vector3> vector(const Json& value) {
            const std::vector<Json>& items = value.as_array();
            if (items.size() < 3) return std::nullopt;
            return Vector3{static_cast<float>(items[0].as_number()), static_cast<float>(items[1].as_number()),
                           static_cast<float>(items[2].as_number())};
        }

        std::string vector_text(Vector3 v) {
            return "[" + number(v.x) + ", " + number(v.y) + ", " + number(v.z) + "]";
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

    const char* state_id(int shape, int state) {
        if (static_cast<BlockShapeKind>(shape) == BlockShapeKind::Torch) return state == 0 ? "floor" : "wall";
        if (static_cast<BlockShapeKind>(shape) == BlockShapeKind::Door) return state == 0 ? "closed" : "open";
        return "default";
    }

    const char* half_id(int shape, int half) {
        if (static_cast<BlockShapeKind>(shape) == BlockShapeKind::Bed) return half == 0 ? "foot" : "head";
        return half == 0 ? "lower" : "upper";
    }

    StateModel default_state(int shape, int state) {
        StateModel model;
        if (static_cast<BlockShapeKind>(shape) == BlockShapeKind::Cross) {
            // Vanilla's plant box: 12x13 px, centered.
            model.has_hitbox = true;
            model.hitbox_from = {2, 0, 2};
            model.hitbox_to = {14, 13, 14};
            return model;
        }
        if (static_cast<BlockShapeKind>(shape) != BlockShapeKind::Torch) return model;
        model.has_hitbox = true;
        if (state == 0) {
            model.hitbox_from = {6, 0, 6};
            model.hitbox_to = {10, 10, 10};
        } else {
            // Its foot on the wall, 2.4px up, leaning 22.5 degrees out of it.
            model.hitbox_from = {5, 3, 1};
            model.hitbox_to = {11, 14, 7};
            model.offset = {0, 2.4f, -8};
            model.pivot = {8, 2.4f, 0};
            model.angle = 22.5f;
        }
        return model;
    }

    BlockStateModel to_state_model(const StateModel& state) {
        auto cells = [](Vector3 pixels) { return Vector3{pixels.x / 16.0f, pixels.y / 16.0f, pixels.z / 16.0f}; };
        BlockStateModel model;
        model.has_hitbox = state.has_hitbox;
        model.hitbox = {cells(state.hitbox_from), cells(state.hitbox_to)};
        model.offset = cells(state.offset);
        model.pivot = cells(state.pivot);
        model.angle = state.angle;
        return model;
    }

    bool elements_allowed(int shape) {
        const BlockShapeKind kind = static_cast<BlockShapeKind>(shape);
        return kind == BlockShapeKind::Cube || kind == BlockShapeKind::Torch;
    }

    std::vector<Element> default_elements(int shape) {
        if (static_cast<BlockShapeKind>(shape) != BlockShapeKind::Torch) return {};
        // Only some faces of each: the planes' two broad sides, the cap's
        // flame on top and the stick's foot under it - all evenly lit.
        auto part = [](const char* name, Vector3 from, Vector3 to, std::initializer_list<int> shown) {
            Element element;
            element.name = name;
            element.from = from;
            element.to = to;
            element.shade = false;
            for (ElementFace& face : element.faces) face.enabled = false;
            for (int f : shown) element.faces[static_cast<size_t>(f)].enabled = true;
            return element;
        };
        Element cap = part("cap", {7, 0, 7}, {9, 10, 9}, {0, 1});
        cap.faces[0].auto_uv = false;
        cap.faces[0].uv = {7, 6, 9, 8};   // the flame
        cap.faces[1].auto_uv = false;
        cap.faces[1].uv = {7, 13, 9, 15}; // the stick's foot
        return {cap, part("planes_x", {7, 0, 0}, {9, 16, 16}, {4, 5}), part("planes_z", {0, 0, 7}, {16, 16, 9}, {2, 3})};
    }

    std::array<float, 4> face_uv(const Element& element, int face) {
        const ElementFace& source = element.faces[static_cast<size_t>(face)];
        if (!source.auto_uv) return source.uv;
        const BoundingBox box = {{element.from.x / 16.0f, element.from.y / 16.0f, element.from.z / 16.0f},
                                 {element.to.x / 16.0f, element.to.y / 16.0f, element.to.z / 16.0f}};
        const Rectangle crop = crop_tile_to_box({0, 0, 16, 16}, static_cast<BlockFace>(face), box);
        return {crop.x, crop.y, crop.x + crop.width, crop.y + crop.height};
    }

    std::vector<BlockElement> to_elements(const std::vector<Element>& elements) {
        std::vector<BlockElement> result;
        for (const Element& element : elements) {
            BlockElement part;
            part.box = {{std::min(element.from.x, element.to.x) / 16.0f, std::min(element.from.y, element.to.y) / 16.0f,
                         std::min(element.from.z, element.to.z) / 16.0f},
                        {std::max(element.from.x, element.to.x) / 16.0f, std::max(element.from.y, element.to.y) / 16.0f,
                         std::max(element.from.z, element.to.z) / 16.0f}};
            part.shade = element.shade;
            for (int f = 0; f < 6; ++f) {
                const ElementFace& face = element.faces[static_cast<size_t>(f)];
                const std::array<float, 4> uv = face_uv(element, f);
                part.faces[static_cast<size_t>(f)] = {face.enabled, {uv[0] / 16.0f, uv[1] / 16.0f, (uv[2] - uv[0]) / 16.0f, (uv[3] - uv[1]) / 16.0f},
                                                      static_cast<ElementNormal>(std::clamp(face.normal, 0, NORMAL_COUNT - 1))};
            }
            result.push_back(part);
        }
        return result;
    }

    std::vector<BlockParticleEmitter> to_emitters(const std::vector<ParticleEmitter>& emitters) {
        std::vector<BlockParticleEmitter> result;
        for (const ParticleEmitter& source : emitters) {
            BlockParticleEmitter emitter;
            emitter.kind = static_cast<BlockParticleKind>(std::clamp(source.kind, 0, PARTICLE_COUNT - 1));
            emitter.at = {source.at.x / 16.0f, source.at.y / 16.0f, source.at.z / 16.0f};
            emitter.spread = {source.spread.x / 16.0f, source.spread.y / 16.0f, source.spread.z / 16.0f};
            emitter.chance = std::clamp(source.chance, 0.0f, 1.0f);
            emitter.count = std::clamp(source.count, 1, 16);
            emitter.color = source.color;
            emitter.only_above_air = source.only_above_air;
            result.push_back(emitter);
        }
        return result;
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
            block.shape = index_of(SHAPE_IDS, root["shape"].as_string("cube"), 0);
            block.double_block = root["double_block"].as_string();
            block.script = root["script"].as_string();
            const std::vector<Json>& cut = root["cut"].as_array();
            if (cut.size() >= 2) {
                block.has_cut = true;
                block.cut_x = std::clamp(static_cast<int>(cut[0].as_number()), 0, ATLAS_TILES - 1);
                block.cut_y = std::clamp(static_cast<int>(cut[1].as_number()), 0, ATLAS_TILES - 1);
            }
            const std::vector<Json>& sprite = root["item_sprite"].as_array();
            if (sprite.size() >= 2) {
                block.item_sprite_x = static_cast<int>(sprite[0].as_number());
                block.item_sprite_y = static_cast<int>(sprite[1].as_number());
            }
            block.side_inset = std::clamp(static_cast<int>(root["side_inset"].as_number(0)), 0, 8);
            auto tile_of = [](const Json& value, int& x, int& y) {
                const std::vector<Json>& tile = value.as_array();
                if (tile.size() < 2) return false;
                x = std::clamp(static_cast<int>(tile[0].as_number()), 0, ATLAS_TILES - 1);
                y = std::clamp(static_cast<int>(tile[1].as_number()), 0, ATLAS_TILES - 1);
                return true;
            };
            for (const Json& soil : root["placed_on"].as_array()) {
                if (!soil.as_string().empty()) block.placed_on.push_back(soil.as_string());
            }
            const Json& pair = root["pair"];
            if (pair.get_type() == Json::Type::Object) {
                block.half = pair["half"].as_string() == half_id(block.shape, 1) ? 1 : 0;
                block.partner = pair["partner"].as_string();
                block.item = pair["item"].as_bool(true);
            }
            block.has_end = tile_of(root["end"], block.end_x, block.end_y);
            const Json& joins = root["joins"];
            if (joins.get_type() == Json::Type::Object) {
                block.joins = true;
                for (int i = 0; i < 4; ++i) {
                    const std::vector<Json>& list = joins[i < 2 ? "front" : "back"].as_array();
                    if (list.size() > static_cast<size_t>(i % 2)) {
                        tile_of(list[static_cast<size_t>(i % 2)], block.joined[static_cast<size_t>(i)][0], block.joined[static_cast<size_t>(i)][1]);
                    }
                }
            }
            // Its parts; a torch without any gets its vanilla ones.
            if (root["elements"].get_type() == Json::Type::Array) {
                for (const Json& saved : root["elements"].as_array()) {
                    Element element;
                    element.name = saved["name"].as_string();
                    auto clamped = [](Vector3 v) {
                        return Vector3{std::clamp(v.x, -16.0f, 32.0f), std::clamp(v.y, -16.0f, 32.0f), std::clamp(v.z, -16.0f, 32.0f)};
                    };
                    if (auto from = vector(saved["from"])) element.from = clamped(*from);
                    if (auto to = vector(saved["to"])) element.to = clamped(*to);
                    element.shade = saved["shade"].as_bool(true);
                    for (int f = 0; f < 6; ++f) {
                        ElementFace& face = element.faces[static_cast<size_t>(f)];
                        const Json& saved_face = saved["faces"][FACE_IDS[f]];
                        face.enabled = saved_face.get_type() == Json::Type::Object;
                        const std::vector<Json>& uv = saved_face["uv"].as_array();
                        if (uv.size() >= 4) {
                            face.auto_uv = false;
                            for (size_t i = 0; i < 4; ++i) face.uv[i] = static_cast<float>(uv[i].as_number());
                        }
                        face.normal = index_of(NORMAL_IDS, saved_face["normal"].as_string("out"), 0);
                    }
                    block.elements.push_back(element);
                }
            } else {
                block.elements = default_elements(block.shape);
            }
            for (const Json& saved : root["particles"].as_array()) {
                ParticleEmitter emitter;
                emitter.kind = index_of(PARTICLE_IDS, saved["type"].as_string("smoke"), 1);
                if (auto at = vector(saved["at"])) emitter.at = *at;
                if (auto spread = vector(saved["spread"])) emitter.spread = *spread;
                emitter.chance = std::clamp(static_cast<float>(saved["chance"].as_number(1.0)), 0.0f, 1.0f);
                emitter.count = std::clamp(static_cast<int>(saved["count"].as_number(1)), 1, 16);
                const std::vector<Json>& color = saved["color"].as_array();
                if (color.size() >= 3) {
                    auto channel = [&](size_t i) {
                        return static_cast<unsigned char>(std::clamp(i < color.size() ? color[i].as_number(255) : 255.0, 0.0, 255.0));
                    };
                    emitter.color = {channel(0), channel(1), channel(2), channel(3)};
                }
                emitter.only_above_air = saved["only_above_air"].as_bool(false);
                block.particles.push_back(emitter);
            }
            for (const Json& saved : root["properties"].as_array()) {
                StateProperty property;
                property.name = saved["name"].as_string();
                if (property.name.empty()) continue;
                property.max = std::clamp(static_cast<int>(saved["max"].as_number(1)), 1, 65535);
                property.default_value = std::clamp(static_cast<int>(saved["default"].as_number(0)), 0, property.max);
                block.properties.push_back(property);
            }
            // Each state over its shape's defaults.
            for (int s = 0; s < MAX_BLOCK_STATES; ++s) {
                StateModel& state = block.states[static_cast<size_t>(s)];
                state = default_state(block.shape, s);
                const Json& saved = root["states"][state_id(block.shape, s)];
                const std::vector<Json>& hitbox = saved["hitbox"].as_array();
                if (hitbox.size() >= 6) {
                    state.has_hitbox = true;
                    auto at = [&](size_t i) { return std::clamp(static_cast<float>(hitbox[i].as_number()), 0.0f, 16.0f); };
                    state.hitbox_from = {at(0), at(1), at(2)};
                    state.hitbox_to = {at(3), at(4), at(5)};
                }
                if (auto offset = vector(saved["offset"])) state.offset = *offset;
                if (auto pivot = vector(saved["pivot"])) state.pivot = *pivot;
                state.angle = static_cast<float>(saved["angle"].as_number(state.angle));
            }
            for (int f = 0; f < 6; ++f) {
                const Json& face = root["faces"][FACE_IDS[f]];
                const std::vector<Json>& tile = face["tile"].as_array();
                if (tile.size() >= 2) {
                    block.faces[f].tile_x = std::clamp(static_cast<int>(tile[0].as_number()), 0, ATLAS_TILES - 1);
                    block.faces[f].tile_y = std::clamp(static_cast<int>(tile[1].as_number()), 0, ATLAS_TILES - 1);
                }

                block.faces[f].biome = index_of(BIOME_IDS, face["biome"].as_string("none"), 0);
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
        if (block.shape != 0) out << "  \"shape\": \"" << SHAPE_IDS[std::clamp(block.shape, 0, SHAPE_COUNT - 1)] << "\",\n";
        if (!block.double_block.empty()) out << "  \"double_block\": \"" << escape(block.double_block) << "\",\n";
        if (!block.script.empty()) out << "  \"script\": \"" << escape(block.script) << "\",\n";
        if (block.has_cut) out << "  \"cut\": [" << block.cut_x << ", " << block.cut_y << "],\n";
        if (block.item_sprite_x >= 0) out << "  \"item_sprite\": [" << block.item_sprite_x << ", " << block.item_sprite_y << "],\n";
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
        if (is_pair_kind(static_cast<BlockShapeKind>(block.shape))) {
            out << "  \"pair\": { \"half\": \"" << half_id(block.shape, block.half) << "\", \"partner\": \"" << escape(block.partner)
                << "\"" << (block.item ? "" : ", \"item\": false") << " },\n";
        }
        if (block.has_end) out << "  \"end\": [" << block.end_x << ", " << block.end_y << "],\n";
        if (!block.placed_on.empty()) {
            out << "  \"placed_on\": [";
            for (size_t i = 0; i < block.placed_on.size(); ++i) out << (i ? ", " : "") << "\"" << escape(block.placed_on[i]) << "\"";
            out << "],\n";
        }
        if (block.joins && block.shape == 0 && block.directional) {
            auto tile = [&](int i) {
                return "[" + std::to_string(block.joined[static_cast<size_t>(i)][0]) + ", " + std::to_string(block.joined[static_cast<size_t>(i)][1]) + "]";
            };
            out << "  \"joins\": { \"front\": [" << tile(0) << ", " << tile(1) << "], \"back\": [" << tile(2) << ", " << tile(3) << "] },\n";
        }
        if (!block.elements.empty()) {
            out << "  \"elements\": [\n";
            for (size_t e = 0; e < block.elements.size(); ++e) {
                const Element& element = block.elements[e];
                out << "    { ";
                if (!element.name.empty()) out << "\"name\": \"" << escape(element.name) << "\", ";
                out << "\"from\": " << vector_text(element.from) << ", \"to\": " << vector_text(element.to);
                if (!element.shade) out << ", \"shade\": false";
                out << ", \"faces\": {";
                bool first = true;
                for (int f = 0; f < 6; ++f) {
                    const ElementFace& face = element.faces[static_cast<size_t>(f)];
                    if (!face.enabled) continue;
                    out << (first ? "\n" : ",\n") << "      \"" << FACE_IDS[f] << "\": {";
                    std::string inside;
                    if (!face.auto_uv) {
                        inside += " \"uv\": [" + number(face.uv[0]) + ", " + number(face.uv[1]) + ", " + number(face.uv[2]) + ", " +
                                  number(face.uv[3]) + "]";
                    }
                    if (face.normal != 0) {
                        inside += std::string(inside.empty() ? "" : ",") + " \"normal\": \"" +
                                  NORMAL_IDS[std::clamp(face.normal, 0, NORMAL_COUNT - 1)] + "\"";
                    }
                    out << inside << (inside.empty() ? "}" : " }");
                    first = false;
                }
                out << (first ? "} }" : "\n    } }") << (e + 1 < block.elements.size() ? "," : "") << "\n";
            }
            out << "  ],\n";
        }
        if (!block.particles.empty()) {
            out << "  \"particles\": [\n";
            for (size_t i = 0; i < block.particles.size(); ++i) {
                const ParticleEmitter& emitter = block.particles[i];
                out << "    { \"type\": \"" << PARTICLE_IDS[std::clamp(emitter.kind, 0, PARTICLE_COUNT - 1)] << "\", \"at\": "
                    << vector_text(emitter.at);
                if (emitter.spread.x != 0 || emitter.spread.y != 0 || emitter.spread.z != 0) out << ", \"spread\": " << vector_text(emitter.spread);
                if (emitter.chance != 1.0f) out << ", \"chance\": " << number(emitter.chance);
                if (emitter.count != 1) out << ", \"count\": " << emitter.count;
                const Color& c = emitter.color;
                if (c.r != 255 || c.g != 255 || c.b != 255 || c.a != 255) {
                    out << ", \"color\": [" << static_cast<int>(c.r) << ", " << static_cast<int>(c.g) << ", " << static_cast<int>(c.b) << ", "
                        << static_cast<int>(c.a) << "]";
                }
                if (emitter.only_above_air) out << ", \"only_above_air\": true";
                out << " }" << (i + 1 < block.particles.size() ? "," : "") << "\n";
            }
            out << "  ],\n";
        }
        if (!block.properties.empty()) {
            out << "  \"properties\": [\n";
            for (size_t i = 0; i < block.properties.size(); ++i) {
                const StateProperty& property = block.properties[i];
                out << "    { \"name\": \"" << escape(property.name) << "\", \"max\": " << property.max;
                if (property.default_value != 0) out << ", \"default\": " << property.default_value;
                out << " }" << (i + 1 < block.properties.size() ? "," : "") << "\n";
            }
            out << "  ],\n";
        }
        // Only the states that have something of their own.
        std::vector<std::string> states;
        for (int s = 0; s < shape_state_count(static_cast<BlockShapeKind>(block.shape)); ++s) {
            const StateModel& state = block.states[static_cast<size_t>(s)];
            const bool moved = state_model_moves(to_state_model(state));
            if (!state.has_hitbox && !moved) continue;
            std::string line = "    \"" + std::string(state_id(block.shape, s)) + "\": { ";
            if (state.has_hitbox) {
                line += "\"hitbox\": [" + number(state.hitbox_from.x) + ", " + number(state.hitbox_from.y) + ", " +
                        number(state.hitbox_from.z) + ", " + number(state.hitbox_to.x) + ", " + number(state.hitbox_to.y) + ", " +
                        number(state.hitbox_to.z) + "]";
            }
            if (moved) {
                if (state.has_hitbox) line += ", ";
                line += "\"offset\": " + vector_text(state.offset) + ", \"pivot\": " + vector_text(state.pivot) +
                        ", \"angle\": " + number(state.angle);
            }
            states.push_back(line + " }");
        }
        if (!states.empty()) {
            out << "  \"states\": {\n";
            for (size_t i = 0; i < states.size(); ++i) out << states[i] << (i + 1 < states.size() ? "," : "") << "\n";
            out << "  },\n";
        }
        out << "  \"faces\": {\n";
        for (int f = 0; f < 6; ++f) {
            const Face& face = block.faces[f];
            out << "    \"" << FACE_IDS[f] << "\": { \"tile\": [" << face.tile_x << ", " << face.tile_y << "]";
            if (face.tint.r != 255 || face.tint.g != 255 || face.tint.b != 255 || face.tint.a != 255) {
                out << ", \"tint\": [" << static_cast<int>(face.tint.r) << ", " << static_cast<int>(face.tint.g) << ", "
                    << static_cast<int>(face.tint.b) << ", " << static_cast<int>(face.tint.a) << "]";
            }
            if (face.biome != 0) out << ", \"biome\": \"" << BIOME_IDS[std::clamp(face.biome, 0, BIOME_COUNT - 1)] << "\"";
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
