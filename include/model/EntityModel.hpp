#pragma once

#include "raylib.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// An entity's model: a tree of parts (body -> head, arms, legs, ...), each a
// few textured cubes rotating about its own pivot, plus named keyframe
// animations. Built in the model editor (tools/model_editor), saved as JSON
// under assets/models/, and drawn by draw_entity_model()
// (model/EntityModelRenderer.hpp).
//
// Units are model pixels - 16 per block, Minecraft's own convention - with
// Y up and +Z the entity's front. Coordinates are absolute (in the model's
// own space, not relative to the parent part): a child's pivot and cubes are
// written where they sit in the rest pose, and a parent's rotation carries
// them along.

// One textured box. Its six faces are laid out on the skin the standard
// Minecraft "box UV" way from `uv` (the layout's top-left corner) and
// `size` - see EntityModelRenderer.cpp.
struct ModelCube {
    Vector3 origin = {0.0f, 0.0f, 0.0f}; // min corner, model pixels
    Vector3 size = {1.0f, 1.0f, 1.0f};   // model pixels
    Vector2 uv = {0.0f, 0.0f};           // skin pixels
};

struct ModelPart {
    std::string name;
    std::string parent;                        // "" = attached to the model root
    Vector3 pivot = {0.0f, 0.0f, 0.0f};        // rotation point, model pixels
    // Allowed rotation per axis, degrees - every pose is clamped into this.
    Vector3 rotation_min = {-180.0f, -180.0f, -180.0f};
    Vector3 rotation_max = {180.0f, 180.0f, 180.0f};
    std::vector<ModelCube> cubes;

    // Look node: this part turns toward where the entity looks, on top of
    // any animation (see apply_head_look()) - on by default for the head.
    // Once the look is more than `body_turn_angle` degrees to either side
    // of the body, the body turns too (see body_yaw_following_look()).
    // Only one part per model looks; with several, the first one counts.
    bool look = false;
    float body_turn_angle = 50.0f;
};

struct ModelKeyframe {
    float time = 0.0f;                   // seconds
    Vector3 rotation = {0.0f, 0.0f, 0.0f}; // degrees, applied X, then Y, then Z
    Vector3 offset = {0.0f, 0.0f, 0.0f};   // model pixels, in the parent's space - moves the part (and its children)
};

// One part's keyframes in one animation, kept sorted by time.
struct ModelTrack {
    std::string part;
    std::vector<ModelKeyframe> keys;
};

// When an animation plays by itself - see EntityAnimator.
enum class AnimationTrigger : uint8_t {
    Manual,   // only when game code starts it (attacks, jumps, ... - later)
    Always,   // continuously, on real time - idle breathing/sway
    Moving,   // while the entity moves: its clock runs on distance travelled
              // (blocks_per_loop per cycle, so feet never slide) and it fades
              // in and out with speed
    Sneaking, // a state: while the entity sneaks. While a state plays, the
              // Always/Moving animations fade out - except the ones it links
              // to (EntityAnimation::links), which keep playing on top of it
    Count,
};

struct EntityAnimation {
    std::string name;
    float length = 1.0f; // seconds
    bool loop = true;
    AnimationTrigger trigger = AnimationTrigger::Manual;
    float blocks_per_loop = 2.0f; // Moving only: distance walked per full cycle
    std::vector<ModelTrack> tracks;

    // Animations that keep playing while this one does - how a state
    // (Sneaking) says "idle and walk still apply while sneaking". Edited as
    // arrows in the model editor's animation graph.
    std::vector<std::string> links;
    Vector2 graph_position = {0.0f, 0.0f}; // where its block sits in that graph (editor only)
};

const char* animation_trigger_id(AnimationTrigger trigger); // "manual" / "always" / "moving" / "sneaking"
AnimationTrigger animation_trigger_from_id(const std::string& id);

struct EntityModel {
    std::string name;
    std::string skin; // relative to ASSETS_PATH, e.g. "sprites/entities/player/Steve.png"
    int skin_width = 64;
    int skin_height = 64;
    std::vector<ModelPart> parts;
    std::vector<EntityAnimation> animations;

    int find_part(const std::string& part_name) const; // -1 if absent
};

// Where every part is posed, indexed like EntityModel::parts.
struct PartPose {
    Vector3 rotation = {0.0f, 0.0f, 0.0f}; // degrees
    Vector3 offset = {0.0f, 0.0f, 0.0f};   // model pixels
};
using ModelPose = std::vector<PartPose>;

// The player-shaped starting point: body (the root) with head, arms and
// legs attached, laid out on the standard 64x64 player skin.
EntityModel make_humanoid_model();

std::optional<EntityModel> load_entity_model(const std::string& path);
bool save_entity_model(const EntityModel& model, const std::string& path);

Vector3 clamp_rotation(const ModelPart& part, Vector3 rotation);

// Every part's rotation at `time` seconds into `animation` (null = rest
// pose, all zero), interpolated linearly between keyframes and clamped to
// each part's limits. A looping animation wraps around its length,
// blending from its last keyframe back into its first.
ModelPose sample_pose(const EntityModel& model, const EntityAnimation* animation, float time);

// Adds `animation` at `time`, scaled by `weight`, onto `pose` - unclamped,
// so several animations can be layered before clamp_pose().
void add_animation(const EntityModel& model, const EntityAnimation& animation, float time, float weight, ModelPose& pose);
void clamp_pose(const EntityModel& model, ModelPose& pose);

// Plays an entity's self-triggered animations (every trigger but Manual) -
// one per entity instance, updated every frame it's drawn.
class EntityAnimator {
public:
    // `moved`: horizontal distance travelled since the last update, blocks.
    // `sneaking`: whether the entity is sneaking right now.
    void update(float delta_time, float moved, bool sneaking = false);

    // Every Always animation, every Moving one (by distance walked, faded
    // by current speed) and every active state (Sneaking), added together
    // and clamped to the limits. An active state fades out the Always/
    // Moving animations it doesn't link to.
    ModelPose pose(const EntityModel& model) const;

private:
    float time = 0.0f;            // seconds
    float distance = 0.0f;        // blocks walked
    float moving_weight = 0.0f;   // 0 standing .. 1 at full walking speed
    float sneaking_weight = 0.0f; // fades 0 <-> 1 as sneaking starts/stops
};

// --- Looking around - every model's look node (ModelPart::look) ---
// A model file that doesn't say which part looks (saved before look nodes
// existed) gets it on the part with this name.
constexpr const char* HEAD_PART = "head";

// The part that looks (ModelPart::look), -1 if none.
int look_part(const EntityModel& model);

// Angle wrapped into -180..180 degrees.
float wrap_degrees(float degrees);

// Turns the look part toward where the entity looks, on top of whatever the
// animation already does with it: `yaw_offset` is the look direction
// relative to the body (degrees, same sense as the body's own yaw),
// `pitch_down` how far it looks down (negative = up). The result is clamped
// to the part's rotation limits. No-op for a model without a look part.
void apply_head_look(const EntityModel& model, ModelPose& pose, float yaw_offset, float pitch_down);

// The body's yaw after the entity looks toward `look_yaw` (degrees): it
// stays put while the look is within the look part's body_turn_angle of it,
// and is dragged along just enough to stay at that angle once the look
// goes further. Unchanged for a model without a look part.
float body_yaw_following_look(const EntityModel& model, float body_yaw, float look_yaw);

// A part's track in `animation`, created empty if `create` and missing.
ModelTrack* find_track(EntityAnimation& animation, const std::string& part, bool create);

// Sets (or inserts, keeping the track sorted) the keyframe at `time`.
void set_keyframe(ModelTrack& track, float time, Vector3 rotation, Vector3 offset);

// The keyframe within `tolerance` seconds of `time`, if any.
int keyframe_at(const ModelTrack& track, float time, float tolerance);
