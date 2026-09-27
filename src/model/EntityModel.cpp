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
        p.name = name;
        p.parent = parent;
        p.pivot = pivot;
        p.rotation_min = min;
        p.rotation_max = max;
        p.cubes.push_back(cube);
        model.parts.push_back(p);
    };
    // Feet at y = 0, 32 pixels (2 blocks) tall, facing +Z; the entity's
    // right side is -X. Pivots sit where each part hinges on the body.
    part("body", "", {0, 24, 0}, {-30, -45, -15}, {30, 45, 15}, {{-4, 12, -2}, {8, 12, 4}, {16, 16}});
    part("head", "body", {0, 24, 0}, {-60, -80, -20}, {60, 80, 20}, {{-4, 24, -4}, {8, 8, 8}, {0, 0}});
    part("right_arm", "body", {-5, 22, 0}, {-180, -30, -90}, {90, 30, 10}, {{-8, 12, -2}, {4, 12, 4}, {40, 16}});
    part("left_arm", "body", {5, 22, 0}, {-180, -30, -10}, {90, 30, 90}, {{4, 12, -2}, {4, 12, 4}, {32, 48}});
    part("right_leg", "body", {-2, 12, 0}, {-90, -20, -30}, {90, 20, 10}, {{-4, 0, -2}, {4, 12, 4}, {0, 16}});
    part("left_leg", "body", {2, 12, 0}, {-90, -20, -10}, {90, 20, 30}, {{0, 0, -2}, {4, 12, 4}, {16, 48}});
    model.parts[model.find_part(HEAD_PART)].look = true;
    return model;
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
        bool any_look_node = false;
        for (const Json& p : root["parts"].as_array()) {
            ModelPart part;
            if (p["look"].get_type() == Json::Type::Bool) {
                part.look = p["look"].as_bool();
                any_look_node = true;
            }
            part.body_turn_angle = std::clamp(static_cast<float>(p["body_turn_angle"].as_number(part.body_turn_angle)), 0.0f, 180.0f);
            part.name = p["name"].as_string();
            part.parent = p["parent"].as_string();
            part.pivot = read_vec3(p["pivot"], part.pivot);
            part.rotation_min = read_vec3(p["rotation_min"], part.rotation_min);
            part.rotation_max = read_vec3(p["rotation_max"], part.rotation_max);
            for (const Json& c : p["cubes"].as_array()) {
                ModelCube cube;
                cube.origin = read_vec3(c["origin"], cube.origin);
                cube.size = read_vec3(c["size"], cube.size);
                const std::vector<Json>& uv = c["uv"].as_array();
                if (uv.size() >= 2) cube.uv = {static_cast<float>(uv[0].as_number()), static_cast<float>(uv[1].as_number())};
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
    out << "  \"parts\": [";
    for (size_t i = 0; i < model.parts.size(); ++i) {
        const ModelPart& part = model.parts[i];
        out << (i ? ",\n" : "\n") << "    {\n";
        out << "      \"name\": \"" << escape(part.name) << "\",\n";
        out << "      \"parent\": \"" << escape(part.parent) << "\",\n";
        out << "      \"pivot\": " << vec3(part.pivot) << ",\n";
        out << "      \"rotation_min\": " << vec3(part.rotation_min) << ",\n";
        out << "      \"rotation_max\": " << vec3(part.rotation_max) << ",\n";
        out << "      \"look\": " << (part.look ? "true" : "false") << ",\n";
        out << "      \"body_turn_angle\": " << number(part.body_turn_angle) << ",\n";
        out << "      \"cubes\": [";
        for (size_t c = 0; c < part.cubes.size(); ++c) {
            const ModelCube& cube = part.cubes[c];
            out << (c ? ", " : "") << "{ \"origin\": " << vec3(cube.origin) << ", \"size\": " << vec3(cube.size)
                << ", \"uv\": [" << number(cube.uv.x) << ", " << number(cube.uv.y) << "] }";
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
        case AnimationTrigger::Always: return "always";
        case AnimationTrigger::Moving: return "moving";
        case AnimationTrigger::Sneaking: return "sneaking";
        default:                       return "manual";
    }
}

AnimationTrigger animation_trigger_from_id(const std::string& id)
{
    if (id == "always") return AnimationTrigger::Always;
    if (id == "moving") return AnimationTrigger::Moving;
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
        pose[index].offset = Vector3Add(pose[index].offset, Vector3Scale(sampled.offset, weight));
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
