#pragma once

#include "raylib.h"

#include <array>
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
    // A fixed turn of this cube alone (degrees, X then Y then Z) about
    // `rotation_origin` (model pixels) - for a box whose skin is laid out
    // for another orientation, like a cow's body lying on its side. Unlike
    // a part's animated rotation it never moves the part's other cubes or
    // children.
    Vector3 rotation = {0.0f, 0.0f, 0.0f};
    Vector3 rotation_origin = {0.0f, 0.0f, 0.0f};

    // Per-side UV override (skin pixels, the side's top-left corner) - a
    // side moved on its own in the editor's UV window instead of with the
    // rest of the box layout. Indexed like MODEL_FACE_IDS; unset = where
    // the box layout from `uv` puts it.
    std::array<std::optional<Vector2>, 6> face_uv{};

    // Decoration (Minecraft's second skin layer - hat, jacket, sleeves, or
    // any small detail): keeps its own size but stands MODEL_OVERLAY_GAP
    // pixels off the part's regular cubes it lies against - see
    // cube_draw_bounds(). Its transparent pixels show nothing - only the
    // painted bits stand out, giving the skin some depth.
    bool overlay = false;

    // Drawn this many pixels bigger on every side (Minecraft's "inflate") -
    // e.g. a sheep's wool wrapped loosely around its body.
    float inflate = 0.0f;

    // Whether a cube drawn bigger than `size` (inflate, a decoration's gap)
    // stretches its skin layout over the bigger box - Minecraft's own way,
    // what its sheep wool texture is drawn for. Off (the default), the
    // layout is as big as the cube is drawn: one skin pixel per model pixel,
    // never stretched.
    bool stretch_texture = false;
};

constexpr float MODEL_OVERLAY_GAP = 0.5f; // model pixels

// The six sides of a cube, in drawing/face_uv order: top, bottom, back
// (-Z), front (+Z), the entity's right (-X), its left (+X). Also the keys
// face_uv is saved under.
constexpr const char* MODEL_FACE_IDS[6] = {"top", "bottom", "back", "front", "right", "left"};

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

// --- What the entity is, beyond its looks: the model editor's left panel.
// The game reads it too - health, drops, interactions, natural spawning. ---

// Where it lives: decides where it spawns (on the ground, in water, in the
// air).
enum class EntityEnvironment : uint8_t { Land, Water, Air, Count };
const char* entity_environment_id(EntityEnvironment environment); // "land" / "water" / "air"
EntityEnvironment entity_environment_from_id(const std::string& id);

// Biome ids spawning can name - lower-case get_biome_name().
constexpr const char* ENTITY_BIOME_IDS[] = {"plains", "forest", "desert", "hills", "ocean", "sea"};

// Dropped when it dies: `item` (a block or item name, as /give takes it)
// with this chance, min..max of it - unless it has `unless_state` (a
// sheared sheep keeps no wool to drop).
struct EntityDropInfo {
    std::string item;
    int min_count = 1;
    int max_count = 1;
    float chance = 1.0f;
    std::string unless_state;
};

// One way the player can act on it - see entities/Interaction.hpp, which
// turns these into the rules the game runs.
enum class EntityTrigger : uint8_t { Hit, Use, Count };   // left / right click
enum class EntityHeld : uint8_t { Anything, EmptyHand, Item, Count };
struct EntityInteractionInfo {
    EntityTrigger trigger = EntityTrigger::Use;
    EntityHeld held = EntityHeld::Anything;
    std::string held_item;      // with EntityHeld::Item
    float chance = 1.0f;
    std::string required_state; // only while it has this state ("" = always)
    std::string blocking_state; // ...and not while it has this one
    std::string drop_item;      // dropped next to it ("" = nothing)
    int drop_min = 1;
    int drop_max = 1;
    std::string hand_result;    // one held item turns into this ("" = stays)
    std::string set_state;
    std::string clear_state;
};

// Natural spawning: in these biomes (none listed = any), picked against
// every other spawning entity there by `weight`, `min_group`..`max_group`
// at a time.
struct EntitySpawnInfo {
    bool enabled = false;
    std::vector<std::string> biomes;
    int weight = 10;
    int min_group = 2;
    int max_group = 4;
};

struct EntityInfo {
    std::string description;
    int health = 10; // hit points - 2 per heart, like the player's
    EntityEnvironment environment = EntityEnvironment::Land;
    std::vector<EntityDropInfo> drops;
    std::vector<EntityInteractionInfo> interactions;
    EntitySpawnInfo spawn;
};

struct EntityModel {
    std::string name;
    std::string skin; // relative to ASSETS_PATH, e.g. "sprites/entities/player/Steve.png"
    int skin_width = 64;
    int skin_height = 64;
    std::vector<ModelPart> parts;
    std::vector<EntityAnimation> animations;
    // Other models (by name, assets/models/<name>.json) drawn on top of this
    // one in the same pose, each with its own skin - matched part by part
    // by name. A sheep's wool is one: a layer the game hides once it's
    // sheared (see Mob::shows_layer()).
    std::vector<std::string> layers;
    EntityInfo entity;

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

// `pose` (of `from`) for `to`: each of `to`'s parts takes the pose of the
// part of `from` with the same name - how a layer model follows its base.
ModelPose map_pose(const EntityModel& from, const ModelPose& pose, const EntityModel& to);

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

    // The Manual animation game code started `seconds` ago ("" = none) -
    // played on top of everything else; a non-looping one ends by itself
    // after its length. Set every frame by whoever draws the entity.
    void set_manual(const std::string& name, float seconds);

private:
    std::string manual_name;
    float manual_time = 0.0f;
    float time = 0.0f;            // seconds
    float distance = 0.0f;        // blocks walked
    float moving_weight = 0.0f;   // 0 standing .. 1 at full walking speed
    float sneaking_weight = 0.0f; // fades 0 <-> 1 as sneaking starts/stops
};

// --- Looking around - every model's look node (ModelPart::look) ---
// A model file that doesn't say which part looks (saved before look nodes
// existed) gets it on the part with this name.
constexpr const char* HEAD_PART = "head";

// Where `cube` (one of `part`'s) is actually drawn, model pixels. A regular
// cube: its own box. A decoration moves MODEL_OVERLAY_GAP off each regular
// cube of the part it touches: a side lying on the same side of a regular
// cube (a layer wrapped around it) moves out by the gap - so a layer as big
// as the cube under it hovers all round - and a decoration sitting on a
// cube's side moves away from it by the gap, keeping its own size (a
// 1-pixel detail stays 1 pixel). Its skin layout always follows `size`.
// Either way ModelCube::inflate then grows it on every side.
BoundingBox cube_draw_bounds(const ModelPart& part, const ModelCube& cube);

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
