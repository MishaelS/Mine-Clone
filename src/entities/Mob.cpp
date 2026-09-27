#include "entities/Mob.hpp"
#include "core/TextureManager.hpp"
#include "model/EntityModelRenderer.hpp"
#include "rendering/EntityLighting.hpp"
#include "world/World.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace {
    // Per-tick physics, blocks/tick - vanilla's living-entity values.
    constexpr float GRAVITY = 0.08f;
    constexpr float VERTICAL_DRAG = 0.98f;
    constexpr float JUMP_VELOCITY = 0.42f;    // clears one block
    constexpr float WATER_GRAVITY = 0.02f;
    constexpr float WATER_DRAG = 0.8f;
    constexpr float SWIM_UP = 0.04f;          // jumping in water - see FloatGoal
    constexpr float WATER_EXIT_JUMP = 0.3f;   // swimming into a bank: climbs out onto it
    constexpr float WATER_WALK_FACTOR = 0.5f; // swimming is slower than walking
    constexpr float COLLISION_EPSILON = 0.001f;
    constexpr float PUSH_DECAY = 0.6f; // share of a shove still moving it the next tick
    constexpr float DEFAULT_HEAD_TURN_LIMIT = 75.0f;
}

Mob::Mob(Vector3 feet_position, float yaw_degrees, uint32_t seed, Dimensions dimensions)
    : Entity(feet_position)
    , dimensions(dimensions)
    , body_yaw_degrees(yaw_degrees)
    , previous_body_yaw(yaw_degrees)
    , head_yaw_degrees(yaw_degrees)
    , previous_head_yaw(yaw_degrees)
    , rng(seed)
{
    motion.reset(feet_position);
}

void Mob::tick(const World& world, const ai::PlayerView& player)
{
    motion.begin_tick();
    previous_body_yaw = body_yaw_degrees;
    previous_head_yaw = head_yaw_degrees;
    previous_head_pitch = head_pitch_degrees;
    jumping = false;

    // Goals decide where to go and look; navigation turns that into the
    // next waypoint, the controls into body/head turning and walk input.
    const ai::AiContext context{world, player};
    goals.tick(*this, context);
    path_navigation.tick(*this);
    movement.tick(*this);
    looking.tick(*this, movement);

    move(world);
    set_position(motion.current);
}

bool Mob::water_at(const World& world, float height) const
{
    const Vector3 feet = motion.current;
    return world.get_block(static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y + height)),
                           static_cast<int>(std::floor(feet.z))) == BlockType::Water;
}

bool Mob::blocked(const World& world, Vector3 feet) const
{
    const float half = dimensions.width * 0.5f;
    BoundingBox box{
        {feet.x - half + COLLISION_EPSILON, feet.y + COLLISION_EPSILON, feet.z - half + COLLISION_EPSILON},
        {feet.x + half - COLLISION_EPSILON, feet.y + dimensions.height - COLLISION_EPSILON, feet.z + half - COLLISION_EPSILON},
    };
    for (int x = static_cast<int>(std::floor(box.min.x)); x <= static_cast<int>(std::floor(box.max.x)); ++x) {
        for (int y = static_cast<int>(std::floor(box.min.y)); y <= static_cast<int>(std::floor(box.max.y)); ++y) {
            for (int z = static_cast<int>(std::floor(box.min.z)); z <= static_cast<int>(std::floor(box.max.z)); ++z) {
                BlockShapeBoxes shape = world.collision_boxes_at(x, y, z);
                for (int i = 0; i < shape.count; ++i) {
                    if (CheckCollisionBoxes(box, shape.boxes[i])) return true;
                }
            }
        }
    }
    return false;
}

void Mob::move(const World& world)
{
    Vector3 feet = motion.current;
    const bool in_water = water_at(world, 0.1f);

    // Walking goes only one way: forward, along the body.
    const float yaw = body_yaw_degrees * DEG2RAD;
    const float speed = forward_speed * (in_water ? WATER_WALK_FACTOR : 1.0f);
    Vector3 step = {std::sin(yaw) * speed, 0.0f, std::cos(yaw) * speed};

    // Shoved by an overlapping hitbox (see push()), on top of its own walk.
    step = Vector3Add(step, push_velocity);
    push_velocity = Vector3Scale(push_velocity, PUSH_DECAY);

    if (in_water) {
        velocity.y = (velocity.y - WATER_GRAVITY) * WATER_DRAG + (jumping ? SWIM_UP : 0.0f);
    } else if (jumping && grounded) {
        velocity.y = JUMP_VELOCITY;
    } else {
        velocity.y = (velocity.y - GRAVITY) * VERTICAL_DRAG;
    }

    // Axis by axis, stopping at anything solid.
    bool hit_wall = false;
    Vector3 next = {feet.x + step.x, feet.y, feet.z};
    if (step.x != 0.0f && blocked(world, next)) { hit_wall = true; push_velocity.x = 0.0f; } else feet = next;
    next = {feet.x, feet.y, feet.z + step.z};
    if (step.z != 0.0f && blocked(world, next)) { hit_wall = true; push_velocity.z = 0.0f; } else feet = next;
    next = {feet.x, feet.y + velocity.y, feet.z};
    if (blocked(world, next)) {
        grounded = velocity.y < 0.0f;
        velocity.y = 0.0f;
    } else {
        feet = next;
        grounded = false;
    }
    // Swam into the shore: climb out, like vanilla's own water jump.
    if (in_water && hit_wall && forward_speed > 0.0f) velocity.y = WATER_EXIT_JUMP;

    motion.current = feet;
}

void Mob::turn_body_toward(float yaw, float max_step)
{
    body_yaw_degrees += std::clamp(wrap_degrees(yaw - body_yaw_degrees), -max_step, max_step);
}

void Mob::set_head(float yaw, float pitch)
{
    head_yaw_degrees = yaw;
    head_pitch_degrees = pitch;
}

float Mob::head_turn_limit() const
{
    const EntityModel& entity = model();
    const int part = look_part(entity);
    return part >= 0 ? entity.parts[static_cast<size_t>(part)].body_turn_angle : DEFAULT_HEAD_TURN_LIMIT;
}

void Mob::render(float tick_alpha, const World& world) const
{
    const EntityModel& entity = model();
    if (entity.parts.empty()) return;

    const Vector3 position = motion.interpolated(tick_alpha);
    const float yaw = previous_body_yaw + wrap_degrees(body_yaw_degrees - previous_body_yaw) * tick_alpha;
    const float head_yaw = previous_head_yaw + wrap_degrees(head_yaw_degrees - previous_head_yaw) * tick_alpha;
    const float head_pitch = previous_head_pitch + (head_pitch_degrees - previous_head_pitch) * tick_alpha;

    // Distance moved since the last frame drives the walk animation.
    float moved = 0.0f;
    if (rendered_before) {
        const Vector3 d = Vector3Subtract(position, last_render_position);
        moved = std::sqrt(d.x * d.x + d.z * d.z);
        if (moved > 2.0f) moved = 0.0f;
    }
    last_render_position = position;
    rendered_before = true;
    animator.update(GetFrameTime(), moved, false);

    ModelPose pose = animator.pose(entity);
    apply_head_look(entity, pose, wrap_degrees(head_yaw - yaw), head_pitch);

    const Texture2D& skin = entity.skin.empty() ? Texture2D{} : TextureManager::get(entity.skin);
    const Color tint = entity_environment_tint(world, Vector3Add(position, {0.0f, dimensions.height * 0.5f, 0.0f}));
    rlPushMatrix();
    rlTranslatef(position.x, position.y, position.z);
    rlRotatef(yaw, 0.0f, 1.0f, 0.0f);
    draw_entity_model(entity, skin, pose, model_scale(), tint);
    rlPopMatrix();
}

Vector3 Mob::eye_position() const
{
    return Vector3Add(motion.current, {0.0f, dimensions.eye_height, 0.0f});
}

Vector3 Mob::look_direction() const
{
    const float yaw = head_yaw_degrees * DEG2RAD, pitch = head_pitch_degrees * DEG2RAD; // pitch: positive = down
    return {std::cos(pitch) * std::sin(yaw), -std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
}

void Mob::push(float dx, float dz)
{
    push_velocity.x += dx;
    push_velocity.z += dz;
}

bool Mob::intersects_block(int x, int y, int z) const
{
    const Vector3 feet = motion.current;
    const float half = dimensions.width * 0.5f;
    return feet.x + half > x && feet.x - half < x + 1.0f &&
           feet.y + dimensions.height > y && feet.y < y + 1.0f &&
           feet.z + half > z && feet.z - half < z + 1.0f;
}
