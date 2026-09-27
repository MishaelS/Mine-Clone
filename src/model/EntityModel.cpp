#include "model/EntityModel.hpp"
#include "core/Json.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {
    std::string escape(const std::string& text) {
        std::string out;
        for (char c : text) {
            if (c == '"' || c == '\\') out += '\\';
            if (c == '\n') { out += "\\n"; continue; }
            out += c;
        }
        return out;
    }

    std::string number(float value) {
        // Short, exact enough for pixels and degrees ("12", "22.5").
        std::ostringstream out;
        out << std::round(value * 1000.0f) / 1000.0f;
        return out.str();
    }

    std::string vec3(Vector3 v) {
        return "[" + number(v.x) + ", " + number(v.y) + ", " + number(v.z) + "]";
    }

    Vector3 read_vec3(const Json& json, Vector3 fallback) {
        const std::vector<Json>& values = json.as_array();
        if (values.size() < 3) return fallback;
        return {static_cast<float>(values[0].as_number()), static_cast<float>(values[1].as_number()),
                static_cast<float>(values[2].as_number())};
    }

    Vector3 lerp(Vector3 a, Vector3 b, float t) {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    }

    PartPose key_pose(const ModelKeyframe& key) { return {key.rotation, key.offset}; }

    PartPose blend(const ModelKeyframe& a, const ModelKeyframe& b, float t) {
        return {lerp(a.rotation, b.rotation, t), lerp(a.offset, b.offset, t)};
    }

    PartPose sample_track(const ModelTrack& track, float time, float length, bool loop) {
        const std::vector<ModelKeyframe>& keys = track.keys;
        if (keys.empty()) return {};
        if (keys.size() == 1) return key_pose(keys[0]);
        if (time <= keys.front().time && !loop) return key_pose(keys.front());
        if (time >= keys.back().time && !loop) return key_pose(keys.back());

        for (size_t i = 0; i + 1 < keys.size(); ++i) {
            if (time >= keys[i].time && time <= keys[i + 1].time) {
                float span = keys[i + 1].time - keys[i].time;
                float t = span > 0.0f ? (time - keys[i].time) / span : 0.0f;
                return blend(keys[i], keys[i + 1], t);
            }
        }
        // Looping, and outside the first..last key range: blend across the
        // wrap, from the last key back into the first one.
        const ModelKeyframe& last = keys.back();
        const ModelKeyframe& first = keys.front();
        float span = (length - last.time) + first.time;
        float since_last = time >= last.time ? time - last.time : (length - last.time) + time;
        float t = span > 0.0f ? since_last / span : 0.0f;
        return blend(last, first, t);
    }
}

int EntityModel::find_part(const std::string& part_name) const
{
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].name == part_name) return static_cast<int>(i);
    }
    return -1;
}

EntityModel make_humanoid_model()
{
    EntityModel model;
    model.name = "player";
    model.skin = "sprites/entities/player/Steve.png";

    auto part = [&model](const char* name, const char* parent, Vector3 pivot, Vector3 min, Vector3 max,
                         ModelCube cube) {
        ModelPart p;
        p.name   = name;
        p.parent = parent;
        p.pivot  = pivot;
        p.rotation_min = min;
        p.rotation_max = max;
        p.cubes.push_back(cube);
        model.parts.push_back(p);
    };
    // Feet at y = 0, 32 pixels (2 blocks) tall, facing +Z; the entity's
    // right side is -X. Pivots sit where each part hinges on the body.
    part("body"     , ""    , { 0, 24, 0}, { -30, -45, -15}, {30, 45, 15}, {{-4, 12, -2}, {8, 12, 4}, {16, 16}});
    part("head"     , "body", { 0, 24, 0}, { -60, -80, -20}, {60, 80, 20}, {{-4, 24, -4}, {8,  8, 8}, { 0,  0}});
    part("right_arm", "body", {-5, 22, 0}, {-180, -30, -90}, {90, 30, 10}, {{-8, 12, -2}, {4, 12, 4}, {40, 16}});
    part("left_arm" , "body", { 5, 22, 0}, {-180, -30, -10}, {90, 30, 90}, {{ 4, 12, -2}, {4, 12, 4}, {32, 48}});
    part("right_leg", "body", {-2, 12, 0}, { -90, -20, -30}, {90, 20, 10}, {{-4,  0, -2}, {4, 12, 4}, { 0, 16}});
    part("left_leg" , "body", { 2, 12, 0}, { -90, -20, -10}, {90, 20, 30}, {{ 0,  0, -2}, {4, 12, 4}, {16, 48}});
    model.parts[model.find_part(HEAD_PART)].look = true;
    return model;
}

namespace {
    const char* TRIGGER_IDS[] = {"hit", "use"};
    const char* HELD_IDS[] = {"any", "empty", "item"};

    template <typename Enum, size_t N>
    Enum enum_from_id(const std::string& id, const char* (&ids)[N], Enum fallback)
    {
        for (size_t i = 0; i < N; ++i) {
            if (id == ids[i]) return static_cast<Enum>(i);
        }
        return fallback;
    }

    EntityInfo read_entity_info(const Json& json)
    {
        EntityInfo info;
        if (json.get_type() != Json::Type::Object) return info;
        info.description = json["description"].as_string();
        info.health = std::max(1, static_cast<int>(json["health"].as_number(info.health)));
        info.environment = entity_environment_from_id(json["environment"].as_string("land"));
        for (const Json& d : json["drops"].as_array()) {
            EntityDropInfo drop;
            drop.item = d["item"].as_string();
            drop.min_count = std::max(0, static_cast<int>(d["min"].as_number(1)));
            drop.max_count = std::max(drop.min_count, static_cast<int>(d["max"].as_number(drop.min_count)));
            drop.chance = std::clamp(static_cast<float>(d["chance"].as_number(1.0)), 0.0f, 1.0f);
            drop.unless_state = d["unless_state"].as_string();
            info.drops.push_back(drop);
        }
        for (const Json& r : json["interactions"].as_array()) {
            EntityInteractionInfo rule;
            rule.trigger = enum_from_id(r["trigger"].as_string("use"), TRIGGER_IDS, EntityTrigger::Use);
            rule.held = enum_from_id(r["held"].as_string("any"), HELD_IDS, EntityHeld::Anything);
            rule.held_item = r["held_item"].as_string();
            rule.chance = std::clamp(static_cast<float>(r["chance"].as_number(1.0)), 0.0f, 1.0f);
            rule.required_state = r["required_state"].as_string();
            rule.blocking_state = r["blocking_state"].as_string();
            rule.drop_item = r["drop"].as_string();
            rule.drop_min = std::max(0, static_cast<int>(r["drop_min"].as_number(1)));
            rule.drop_max = std::max(rule.drop_min, static_cast<int>(r["drop_max"].as_number(rule.drop_min)));
            rule.hand_result = r["hand_result"].as_string();
            rule.set_state = r["set_state"].as_string();
            rule.clear_state = r["clear_state"].as_string();
            info.interactions.push_back(rule);
        }
        const Json& spawn = json["spawn"];
        info.spawn.enabled = spawn["enabled"].as_bool(false);
        for (const Json& biome : spawn["biomes"].as_array()) {
            if (!biome.as_string().empty()) info.spawn.biomes.push_back(biome.as_string());
        }
        info.spawn.weight = std::max(1, static_cast<int>(spawn["weight"].as_number(info.spawn.weight)));
        info.spawn.min_group = std::max(1, static_cast<int>(spawn["min_group"].as_number(info.spawn.min_group)));
        info.spawn.max_group = std::max(info.spawn.min_group, static_cast<int>(spawn["max_group"].as_number(info.spawn.max_group)));
        return info;
    }

    void write_entity_info(std::ostream& out, const EntityInfo& info)
    {
        auto field = [&](const char* key, const std::string& value) {
            if (!value.empty()) out << ", \"" << key << "\": \"" << escape(value) << "\"";
        };
        out << "  \"entity\": {\n";
        out << "    \"description\": \"" << escape(info.description) << "\",\n";
        out << "    \"health\": " << info.health << ",\n";
        out << "    \"environment\": \"" << entity_environment_id(info.environment) << "\",\n";
        out << "    \"drops\": [";
        for (size_t i = 0; i < info.drops.size(); ++i) {
            const EntityDropInfo& drop = info.drops[i];
            out << (i ? ",\n      " : "\n      ") << "{ \"item\": \"" << escape(drop.item) << "\", \"min\": " << drop.min_count
                << ", \"max\": " << drop.max_count << ", \"chance\": " << number(drop.chance);
            field("unless_state", drop.unless_state);
            out << " }";
        }
        out << (info.drops.empty() ? "],\n" : "\n    ],\n");
        out << "    \"interactions\": [";
        for (size_t i = 0; i < info.interactions.size(); ++i) {
            const EntityInteractionInfo& rule = info.interactions[i];
            out << (i ? ",\n      " : "\n      ") << "{ \"trigger\": \"" << TRIGGER_IDS[static_cast<int>(rule.trigger)]
                << "\", \"held\": \"" << HELD_IDS[static_cast<int>(rule.held)] << "\"";
            if (rule.held == EntityHeld::Item) field("held_item", rule.held_item);
            out << ", \"chance\": " << number(rule.chance);
            field("required_state", rule.required_state);
            field("blocking_state", rule.blocking_state);
            if (!rule.drop_item.empty()) {
                field("drop", rule.drop_item);
                out << ", \"drop_min\": " << rule.drop_min << ", \"drop_max\": " << rule.drop_max;
            }
            field("hand_result", rule.hand_result);
            field("set_state", rule.set_state);
            field("clear_state", rule.clear_state);
            out << " }";
        }
        out << (info.interactions.empty() ? "],\n" : "\n    ],\n");
        out << "    \"spawn\": { \"enabled\": " << (info.spawn.enabled ? "true" : "false") << ", \"biomes\": [";
        for (size_t i = 0; i < info.spawn.biomes.size(); ++i) out << (i ? ", " : "") << "\"" << escape(info.spawn.biomes[i]) << "\"";
        out << "], \"weight\": " << info.spawn.weight << ", \"min_group\": " << info.spawn.min_group
            << ", \"max_group\": " << info.spawn.max_group << " }\n";
        out << "  },\n";
    }
}

const char* entity_environment_id(EntityEnvironment environment)
{
    switch (environment) {
        case EntityEnvironment::Water: return "water";
        case EntityEnvironment::Air: return "air";
        default: return "land";
    }
}

EntityEnvironment entity_environment_from_id(const std::string& id)
{
    if (id == "water") return EntityEnvironment::Water;
    if (id == "air") return EntityEnvironment::Air;
    return EntityEnvironment::Land;
}

std::optional<EntityModel> load_entity_model(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream buffer;
    buffer << in.rdbuf();

    try {
        Json root = Json::parse(buffer.str());
        EntityModel model;
        model.name = root["name"].as_string();
        model.skin = root["skin"].as_string();
        const std::vector<Json>& skin_size = root["skin_size"].as_array();
        if (skin_size.size() >= 2) {
            model.skin_width = std::max(1, static_cast<int>(skin_size[0].as_number(64)));
            model.skin_height = std::max(1, static_cast<int>(skin_size[1].as_number(64)));
        }

        model.entity = read_entity_info(root["entity"]);
        for (const Json& layer : root["layers"].as_array()) {
            if (!layer.as_string().empty()) model.layers.push_back(layer.as_string());
        }

        bool any_look_node = false;
        for (const Json& p : root["parts"].as_array()) {
            ModelPart part;
            if (p["look"].get_type() == Json::Type::Bool) {
                part.look = p["look"].as_bool();
                any_look_node = true;
            }
            part.body_turn_angle = std::clamp(static_cast<float>(p["body_turn_angle"].as_number(part.body_turn_angle)), 0.0f, 180.0f);
            part.name   = p["name"].as_string();
            part.parent = p["parent"].as_string();
            part.pivot  = read_vec3(p["pivot"], part.pivot);
            part.rotation = read_vec3(p["rotation"], part.rotation);
            part.item_slot = p["item_slot"].as_string();
            part.rotation_min = read_vec3(p["rotation_min"], part.rotation_min);
            part.rotation_max = read_vec3(p["rotation_max"], part.rotation_max);
            for (const Json& c : p["cubes"].as_array()) {
                ModelCube cube;
                cube.origin = read_vec3(c["origin"], cube.origin);
                cube.size = read_vec3(c["size"], cube.size);
                const std::vector<Json>& uv = c["uv"].as_array();
                if (uv.size() >= 2) cube.uv = {static_cast<float>(uv[0].as_number()), static_cast<float>(uv[1].as_number())};
                cube.rotation = read_vec3(c["rotation"], cube.rotation);
                cube.rotation_origin = read_vec3(c["rotation_origin"], cube.rotation_origin);
                cube.overlay = c["overlay"].as_bool(false);
                cube.inflate = std::max(0.0f, static_cast<float>(c["inflate"].as_number(0.0)));
                cube.stretch_texture = c["stretch_texture"].as_bool(false);
                for (int f = 0; f < 6; ++f) {
                    const std::vector<Json>& face = c["face_uv"][MODEL_FACE_IDS[f]].as_array();
                    if (face.size() >= 2) {
                        cube.face_uv[f] = Vector2{static_cast<float>(face[0].as_number()), static_cast<float>(face[1].as_number())};
                    }
                }
                part.cubes.push_back(cube);
            }
            model.parts.push_back(part);
        }

        // Saved before look nodes existed: the head looks, as it always did.
        if (!any_look_node) {
            if (int head = model.find_part(HEAD_PART); head >= 0) model.parts[head].look = true;
        }

        for (const Json& a : root["animations"].as_array()) {
            EntityAnimation animation;
            animation.name = a["name"].as_string();
            animation.length = std::max(0.05f, static_cast<float>(a["length"].as_number(1.0)));
            animation.loop = a["loop"].as_bool(true);
            // Saved before triggers existed: go by the conventional names.
            std::string fallback_trigger = animation.name == "idle" ? "always" : animation.name == "walk" ? "moving" : "manual";
            if (animation.name == "sneak") fallback_trigger = "sneaking";
            animation.trigger = animation_trigger_from_id(a["trigger"].as_string(fallback_trigger));
            for (const Json& link : a["links"].as_array()) animation.links.push_back(link.as_string());
            const std::vector<Json>& position = a["graph_position"].as_array();
            if (position.size() >= 2) {
                animation.graph_position = {static_cast<float>(position[0].as_number()), static_cast<float>(position[1].as_number())};
            }

            animation.blocks_per_loop = std::max(0.1f, static_cast<float>(a["blocks_per_loop"].as_number(2.0)));
            for (const Json& t : a["tracks"].as_array()) {
                ModelTrack track;
                track.part = t["part"].as_string();
                for (const Json& k : t["keys"].as_array()) {
                    track.keys.push_back({static_cast<float>(k["time"].as_number()), read_vec3(k["rotation"], {0, 0, 0}),
                                          read_vec3(k["offset"], {0, 0, 0})});
                }
                std::sort(track.keys.begin(), track.keys.end(),
                          [](const ModelKeyframe& a_key, const ModelKeyframe& b_key) { return a_key.time < b_key.time; });
                animation.tracks.push_back(track);
            }
            model.animations.push_back(animation);
        }
        return model;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool save_entity_model(const EntityModel& model, const std::string& path)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    out << "{\n";
    out << "  \"name\": \"" << escape(model.name) << "\",\n";
    out << "  \"skin\": \"" << escape(model.skin) << "\",\n";
    out << "  \"skin_size\": [" << model.skin_width << ", " << model.skin_height << "],\n";
    if (!model.layers.empty()) {
        out << "  \"layers\": [";
        for (size_t i = 0; i < model.layers.size(); ++i) out << (i ? ", " : "") << "\"" << escape(model.layers[i]) << "\"";
        out << "],\n";
    }
    write_entity_info(out, model.entity);
    out << "  \"parts\": [";
    for (size_t i = 0; i < model.parts.size(); ++i) {
        const ModelPart& part = model.parts[i];
        out << (i ? ",\n" : "\n") << "    {\n";
        out << "      \"name\": \"" << escape(part.name) << "\",\n";
        out << "      \"parent\": \"" << escape(part.parent) << "\",\n";
        out << "      \"pivot\": " << vec3(part.pivot) << ",\n";
        if (part.rotation.x != 0.0f || part.rotation.y != 0.0f || part.rotation.z != 0.0f) {
            out << "      \"rotation\": " << vec3(part.rotation) << ",\n";
        }
        if (!part.item_slot.empty()) out << "      \"item_slot\": \"" << escape(part.item_slot) << "\",\n";
        out << "      \"rotation_min\": " << vec3(part.rotation_min) << ",\n";
        out << "      \"rotation_max\": " << vec3(part.rotation_max) << ",\n";
        out << "      \"look\": " << (part.look ? "true" : "false") << ",\n";
        out << "      \"body_turn_angle\": " << number(part.body_turn_angle) << ",\n";
        out << "      \"cubes\": [";
        for (size_t c = 0; c < part.cubes.size(); ++c) {
            const ModelCube& cube = part.cubes[c];
            out << (c ? ", " : "") << "{ \"origin\": " << vec3(cube.origin) << ", \"size\": " << vec3(cube.size)
                << ", \"uv\": [" << number(cube.uv.x) << ", " << number(cube.uv.y) << "]";
            if (cube.rotation.x != 0.0f || cube.rotation.y != 0.0f || cube.rotation.z != 0.0f) {
                out << ", \"rotation\": " << vec3(cube.rotation) << ", \"rotation_origin\": " << vec3(cube.rotation_origin);
            }
            if (cube.overlay) out << ", \"overlay\": true";
            if (cube.inflate != 0.0f) out << ", \"inflate\": " << number(cube.inflate);
            if (cube.stretch_texture) out << ", \"stretch_texture\": true";
            bool any_face = false;
            for (int f = 0; f < 6; ++f) {
                if (!cube.face_uv[f]) continue;
                out << (any_face ? ", " : ", \"face_uv\": { ") << "\"" << MODEL_FACE_IDS[f] << "\": ["
                    << number(cube.face_uv[f]->x) << ", " << number(cube.face_uv[f]->y) << "]";
                any_face = true;
            }
            if (any_face) out << " }";
            out << " }";
        }
        out << "]\n    }";
    }
    out << "\n  ],\n";
    out << "  \"animations\": [";
    for (size_t i = 0; i < model.animations.size(); ++i) {
        const EntityAnimation& animation = model.animations[i];
        out << (i ? ",\n" : "\n") << "    {\n";
        out << "      \"name\": \"" << escape(animation.name) << "\",\n";
        out << "      \"length\": " << number(animation.length) << ",\n";
        out << "      \"loop\": " << (animation.loop ? "true" : "false") << ",\n";
        out << "      \"trigger\": \"" << animation_trigger_id(animation.trigger) << "\",\n";
        out << "      \"blocks_per_loop\": " << number(animation.blocks_per_loop) << ",\n";
        out << "      \"links\": [";
        for (size_t l = 0; l < animation.links.size(); ++l) out << (l ? ", " : "") << "\"" << escape(animation.links[l]) << "\"";
        out << "],\n";
        out << "      \"graph_position\": [" << number(animation.graph_position.x) << ", " << number(animation.graph_position.y) << "],\n";
        out << "      \"tracks\": [";
        for (size_t t = 0; t < animation.tracks.size(); ++t) {
            const ModelTrack& track = animation.tracks[t];
            out << (t ? ",\n" : "\n") << "        { \"part\": \"" << escape(track.part) << "\", \"keys\": [";
            for (size_t k = 0; k < track.keys.size(); ++k) {
                const ModelKeyframe& key = track.keys[k];
                out << (k ? ", " : "") << "{ \"time\": " << number(key.time) << ", \"rotation\": " << vec3(key.rotation);
                if (key.offset.x != 0.0f || key.offset.y != 0.0f || key.offset.z != 0.0f) out << ", \"offset\": " << vec3(key.offset);
                out << " }";
            }
            out << "] }";
        }
        out << (animation.tracks.empty() ? "]\n" : "\n      ]\n") << "    }";
    }
    out << (model.animations.empty() ? "]\n" : "\n  ]\n") << "}\n";
    return static_cast<bool>(out);
}

Vector3 clamp_rotation(const ModelPart& part, Vector3 rotation)
{
    return {std::clamp(rotation.x, part.rotation_min.x, part.rotation_max.x),
            std::clamp(rotation.y, part.rotation_min.y, part.rotation_max.y),
            std::clamp(rotation.z, part.rotation_min.z, part.rotation_max.z)};
}

const char* animation_trigger_id(AnimationTrigger trigger)
{
    switch (trigger) {
        case AnimationTrigger::Always:    return "always";
        case AnimationTrigger::Moving:    return "moving";
        case AnimationTrigger::Sneaking: return "sneaking";
        default:
            return "manual";
    }
}

AnimationTrigger animation_trigger_from_id(const std::string& id)
{
    if (id == "always"  ) return AnimationTrigger::Always;
    if (id == "moving"  ) return AnimationTrigger::Moving;
    if (id == "sneaking") return AnimationTrigger::Sneaking;
    return AnimationTrigger::Manual;
}

void add_animation(const EntityModel& model, const EntityAnimation& animation, float time, float weight, ModelPose& pose)
{
    if (weight <= 0.0f) return;
    pose.resize(model.parts.size());
    float length = std::max(0.0001f, animation.length);
    float t = animation.loop ? std::fmod(std::fmod(time, length) + length, length) : std::clamp(time, 0.0f, length);
    for (const ModelTrack& track : animation.tracks) {
        int index = model.find_part(track.part);
        if (index < 0) continue;
        PartPose sampled = sample_track(track, t, length, animation.loop);
        pose[index].rotation = Vector3Add(pose[index].rotation, Vector3Scale(sampled.rotation, weight));
        pose[index].offset   = Vector3Add(pose[index].offset, Vector3Scale(sampled.offset, weight));
    }
}

void clamp_pose(const EntityModel& model, ModelPose& pose)
{
    pose.resize(model.parts.size());
    for (size_t i = 0; i < pose.size(); ++i) pose[i].rotation = clamp_rotation(model.parts[i], pose[i].rotation);
}

ModelPose sample_pose(const EntityModel& model, const EntityAnimation* animation, float time)
{
    ModelPose pose(model.parts.size());
    if (animation == nullptr) return pose;
    add_animation(model, *animation, time, 1.0f, pose);
    clamp_pose(model, pose);
    return pose;
}

namespace {
    // Horizontal speed (blocks/second) at which a Moving animation reaches
    // full strength - about a normal walk.
    constexpr float FULL_MOVING_SPEED = 3.5f;
    // How fast the Moving strength catches up with the current speed
    // (fraction of the gap per second) - fades legs in/out instead of
    // snapping them when starting or stopping.
    constexpr float MOVING_WEIGHT_RATE = 8.0f;
}

void EntityAnimator::update(float delta_time, float moved, bool sneaking)
{
    delta_time = std::max(delta_time, 0.0001f);
    time += delta_time;
    distance += moved;
    const float rate = std::min(1.0f, delta_time * MOVING_WEIGHT_RATE);
    const float target = std::clamp(moved / delta_time / FULL_MOVING_SPEED, 0.0f, 1.0f);
    moving_weight += (target - moving_weight) * rate;
    sneaking_weight += ((sneaking ? 1.0f : 0.0f) - sneaking_weight) * rate;
}

void EntityAnimator::set_manual(const std::string& name, float seconds)
{
    manual.clear();
    if (!name.empty()) add_manual(name, seconds);
}

void EntityAnimator::add_manual(const std::string& name, float seconds, float weight)
{
    manual.push_back({name, std::max(0.0f, seconds), weight});
}

ModelPose EntityAnimator::pose(const EntityModel& model) const
{
    // How strongly an active state (only Sneaking so far) holds each
    // Always/Moving animation: one it links to plays on at full strength,
    // any other fades out as the state fades in.
    float state_weight = 0.0f;
    for (const EntityAnimation& animation : model.animations) {
        if (animation.trigger == AnimationTrigger::Sneaking) state_weight = sneaking_weight;
    }

    auto kept_by_state = [&](const std::string& name) {
        for (const EntityAnimation& state : model.animations) {
            if (state.trigger != AnimationTrigger::Sneaking) continue;
            if (std::find(state.links.begin(), state.links.end(), name) != state.links.end()) return true;
        }
        return false;
    };

    ModelPose pose(model.parts.size());
    for (const EntityAnimation& animation : model.animations) {
        const float base = kept_by_state(animation.name) ? 1.0f : 1.0f - state_weight;
        switch (animation.trigger) {
            case AnimationTrigger::Always:
                add_animation(model, animation, time, base, pose);
                break;
            case AnimationTrigger::Moving: {
                float cycles = distance / std::max(0.1f, animation.blocks_per_loop);
                add_animation(model, animation, cycles * animation.length, moving_weight * base, pose);
                break;
            }
            case AnimationTrigger::Sneaking:
                add_animation(model, animation, time, sneaking_weight, pose);
                break;
            case AnimationTrigger::Manual:
                for (const ManualLayer& layer : manual) {
                    if (animation.name == layer.name && (animation.loop || layer.time <= animation.length)) {
                        add_animation(model, animation, layer.time, layer.weight, pose);
                    }
                }
                break;
            default:
                break;
        }
    }
    clamp_pose(model, pose);
    return pose;
}

ModelTrack* find_track(EntityAnimation& animation, const std::string& part, bool create)
{
    for (ModelTrack& track : animation.tracks) {
        if (track.part == part) return &track;
    }
    if (!create) return nullptr;
    animation.tracks.push_back({part, {}});
    return &animation.tracks.back();
}

void set_keyframe(ModelTrack& track, float time, Vector3 rotation, Vector3 offset)
{
    int existing = keyframe_at(track, time, 0.0005f);
    if (existing >= 0) {
        track.keys[existing].rotation = rotation;
        track.keys[existing].offset = offset;
        return;
    }
    auto position = std::lower_bound(track.keys.begin(), track.keys.end(), time,
                                     [](const ModelKeyframe& key, float t) { return key.time < t; });
    track.keys.insert(position, {time, rotation, offset});
}

int keyframe_at(const ModelTrack& track, float time, float tolerance)
{
    for (size_t i = 0; i < track.keys.size(); ++i) {
        if (std::fabs(track.keys[i].time - time) <= tolerance) return static_cast<int>(i);
    }
    return -1;
}

float wrap_degrees(float degrees)
{
    degrees = std::fmod(degrees + 180.0f, 360.0f);
    if (degrees < 0.0f) degrees += 360.0f;
    return degrees - 180.0f;
}

BoundingBox cube_draw_bounds(const ModelPart& part, const ModelCube& cube)
{
    const Vector3 low = cube.origin;
    const Vector3 high = Vector3Add(cube.origin, cube.size);
    const Vector3 grow = {cube.inflate, cube.inflate, cube.inflate};
    if (!cube.overlay) return {Vector3Subtract(low, grow), Vector3Add(high, grow)};

    constexpr float TOUCH_EPSILON = 0.001f;
    auto near = [](float a, float b) { return std::fabs(a - b) < TOUCH_EPSILON; };
    auto axis = [](const Vector3& v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; };
    float grow_low[3] = {0, 0, 0}, grow_high[3] = {0, 0, 0}, shift[3] = {0, 0, 0};
    for (const ModelCube& base : part.cubes) {
        if (base.overlay) continue;
        // Only a cube turned the same way shares its sides' planes.
        if (!Vector3Equals(base.rotation, cube.rotation) ||
            (!Vector3Equals(cube.rotation, {0, 0, 0}) && !Vector3Equals(base.rotation_origin, cube.rotation_origin))) {
            continue;
        }
        const Vector3 base_low = base.origin;
        const Vector3 base_high = Vector3Add(base.origin, base.size);
        for (int a = 0; a < 3; ++a) {
            // The two sides only touch if they overlap across the other axes.
            bool overlaps = true;
            for (int b = 0; b < 3; ++b) {
                if (b == a) continue;
                overlaps = overlaps && std::min(axis(high, b), axis(base_high, b)) - std::max(axis(low, b), axis(base_low, b)) > TOUCH_EPSILON;
            }
            if (!overlaps) continue;
            if (near(axis(high, a), axis(base_high, a))) grow_high[a] = MODEL_OVERLAY_GAP; // wraps its + side
            if (near(axis(low, a), axis(base_low, a))) grow_low[a] = MODEL_OVERLAY_GAP;    // wraps its - side
            if (near(axis(low, a), axis(base_high, a))) shift[a] = MODEL_OVERLAY_GAP;      // sits on its + side
            if (near(axis(high, a), axis(base_low, a))) shift[a] = -MODEL_OVERLAY_GAP;     // sits on its - side
        }
    }
    return {Vector3Subtract({low.x - grow_low[0] + shift[0], low.y - grow_low[1] + shift[1], low.z - grow_low[2] + shift[2]}, grow),
            Vector3Add({high.x + grow_high[0] + shift[0], high.y + grow_high[1] + shift[1], high.z + grow_high[2] + shift[2]}, grow)};
}

ModelPose map_pose(const EntityModel& from, const ModelPose& pose, const EntityModel& to)
{
    ModelPose mapped(to.parts.size());
    for (size_t i = 0; i < to.parts.size(); ++i) {
        const int source = from.find_part(to.parts[i].name);
        if (source >= 0 && source < static_cast<int>(pose.size())) mapped[i] = pose[static_cast<size_t>(source)];
    }
    return mapped;
}

int look_part(const EntityModel& model)
{
    for (size_t i = 0; i < model.parts.size(); ++i) {
        if (model.parts[i].look) return static_cast<int>(i);
    }
    return -1;
}

void apply_head_look(const EntityModel& model, ModelPose& pose, float yaw_offset, float pitch_down)
{
    int head = look_part(model);
    if (head < 0 || head >= static_cast<int>(pose.size())) return;
    pose[head].rotation.y += wrap_degrees(yaw_offset);
    pose[head].rotation.x += pitch_down;
    pose[head].rotation = clamp_rotation(model.parts[head], pose[head].rotation);
}

float body_yaw_following_look(const EntityModel& model, float body_yaw, float look_yaw)
{
    const int head = look_part(model);
    if (head < 0) return body_yaw;
    const float max_turn = model.parts[head].body_turn_angle;
    const float offset = wrap_degrees(look_yaw - body_yaw);
    if (offset > max_turn) body_yaw = look_yaw - max_turn;
    else if (offset < -max_turn) body_yaw = look_yaw + max_turn;
    return wrap_degrees(body_yaw);
}
