#include "player/PlayerController.hpp"
#include "core/Block.hpp"
#include "core/Tick.hpp"
#include "world/World.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr float HALF_WIDTH = PlayerController::WIDTH * 0.5f;
    constexpr float COLLISION_EPSILON = 0.001f;
    constexpr float GROUND_PROBE = 0.05f;

    constexpr float CREATIVE_SPEED = 4.0f;
    constexpr float CREATIVE_SPRINT_MULTIPLIER = 2.0f;
    constexpr float WALK_SPEED = 4.317f;
    constexpr float SPRINT_SPEED = 5.612f;
    constexpr float SNEAK_SPEED = 1.30f;
    constexpr float GROUND_ACCELERATION = 28.0f;
    constexpr float GROUND_DECELERATION = 34.0f;
    constexpr float AIR_ACCELERATION = 7.0f;
    constexpr float WATER_SPEED = 2.2f;
    constexpr float NORMAL_STEP_HEIGHT = 0.6f;
    constexpr float STEP_RISE_SPEED = 4.5f;
    // Vanilla applies a distinct upward impulse when a swimming entity is
    // horizontally blocked at a ledge.  It is intentionally stronger than
    // ordinary swim-up velocity so gravity cannot pull the hitbox back into
    // the fluid before its feet clear the bank.
    constexpr float WATER_EXIT_VELOCITY = 6.0f;
    constexpr float WATER_SURFACE_HEIGHT = 14.0f / 16.0f;
    constexpr float GRAVITY = 0.08f * TICKS_PER_SECOND * TICKS_PER_SECOND;
    // Continuous equivalent of vanilla's discrete 0.42 block/tick impulse;
    // calibrated to the same approximately 1.25-block jump height.
    constexpr float JUMP_VELOCITY = 9.55f;
    constexpr float VERTICAL_DRAG_PER_TICK = 0.98f;
    constexpr float TERMINAL_VELOCITY = -78.4f;

    float move_towards(float current, float target, float max_delta)
    {
        if (current < target) return std::min(current + max_delta, target);
        return std::max(current - max_delta, target);
    }

    bool box_blocked(const World& world, Vector3 feet)
    {
        int min_x = static_cast<int>(std::floor(feet.x - HALF_WIDTH + COLLISION_EPSILON));
        int max_x = static_cast<int>(std::floor(feet.x + HALF_WIDTH - COLLISION_EPSILON));
        int min_y = static_cast<int>(std::floor(feet.y + COLLISION_EPSILON));
        int max_y = static_cast<int>(std::floor(feet.y + PlayerController::HEIGHT - COLLISION_EPSILON));
        int min_z = static_cast<int>(std::floor(feet.z - HALF_WIDTH + COLLISION_EPSILON));
        int max_z = static_cast<int>(std::floor(feet.z + HALF_WIDTH - COLLISION_EPSILON));
        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int z = min_z; z <= max_z; ++z) {
                    if (get_block_properties(world.get_block(x, y, z)).solid) return true;
                }
            }
        }
        return false;
    }

    bool box_touches_water(const World& world, Vector3 feet)
    {
        const int min_x = static_cast<int>(std::floor(feet.x - HALF_WIDTH + COLLISION_EPSILON));
        const int max_x = static_cast<int>(std::floor(feet.x + HALF_WIDTH - COLLISION_EPSILON));
        const int min_y = static_cast<int>(std::floor(feet.y + COLLISION_EPSILON));
        const int max_y = static_cast<int>(std::floor(feet.y + PlayerController::HEIGHT - COLLISION_EPSILON));
        const int min_z = static_cast<int>(std::floor(feet.z - HALF_WIDTH + COLLISION_EPSILON));
        const int max_z = static_cast<int>(std::floor(feet.z + HALF_WIDTH - COLLISION_EPSILON));
        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int z = min_z; z <= max_z; ++z) {
                    if (world.get_block(x, y, z) == BlockType::Water &&
                        feet.y < y + WATER_SURFACE_HEIGHT &&
                        feet.y + PlayerController::HEIGHT > y) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool move_axis(const World& world, Vector3& feet, float delta, int axis)
    {
        if (std::fabs(delta) <= 0.000001f) return false;
        float* component = axis == 0 ? &feet.x : axis == 1 ? &feet.y : &feet.z;
        int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(delta) / GROUND_PROBE)));
        float step = delta / static_cast<float>(steps);
        for (int i = 0; i < steps; ++i) {
            Vector3 test = feet;
            float* test_component = axis == 0 ? &test.x : axis == 1 ? &test.y : &test.z;
            *test_component += step;
            if (box_blocked(world, test)) return true;
            *component += step;
        }
        return false;
    }

    // Finds the smallest clear height from which the requested horizontal
    // move fits. Raising the player separately from the horizontal retry
    // makes a dry-land step a short smooth climb rather than a one-frame
    // teleport onto the block. Water-bank exits use their own impulse below.
    bool rise_towards_step(const World& world, Vector3& feet, float horizontal_delta,
                           int axis, float maximum_height, float delta_time)
    {
        constexpr float SEARCH_INCREMENT = 0.05f;
        float required_height = 0.0f;
        for (float height = SEARCH_INCREMENT; height <= maximum_height + 0.0001f;
             height += SEARCH_INCREMENT) {
            Vector3 raised = feet;
            raised.y += height;
            Vector3 moved = raised;
            (axis == 0 ? moved.x : moved.z) += horizontal_delta;
            if (!box_blocked(world, raised) && !box_blocked(world, moved)) {
                required_height = height;
                break;
            }
        }
        if (required_height <= 0.0f) return false;

        Vector3 raised = feet;
        raised.y += std::min(required_height, STEP_RISE_SPEED * delta_time);
        if (box_blocked(world, raised)) return false;
        feet.y = raised.y;
        return true;
    }

    Vector3 horizontal_basis_forward(const Camera3D& eyes)
    {
        Vector3 look = Vector3Subtract(eyes.target, eyes.position);
        look.y = 0.0f;
        return Vector3LengthSqr(look) > 0.000001f ? Vector3Normalize(look) : Vector3{0.0f, 0.0f, -1.0f};
    }
}

void PlayerController::reset()
{
    horizontal_velocity = {0.0f, 0.0f, 0.0f};
    vertical_velocity = 0.0f;
    grounded = false;
    touching_water = false;
}

void PlayerController::update(Camera3D& eyes, const World& world, GameMode mode,
                              const PlayerInput& input, float delta_time)
{
    delta_time = std::min(delta_time, 0.1f);
    Vector3 forward = horizontal_basis_forward(eyes);
    Vector3 right = { -forward.z, 0.0f, forward.x };
    Vector3 wish = Vector3Add(Vector3Scale(forward, input.forward), Vector3Scale(right, input.right));
    if (Vector3LengthSqr(wish) > 1.0f) wish = Vector3Normalize(wish);

    Vector3 previous = eyes.position;
    Vector3 feet = feet_position(eyes);
    touching_water = box_touches_water(world, feet);
    if (mode == GameMode::Creative) {
        float speed = CREATIVE_SPEED * (input.sprint ? CREATIVE_SPRINT_MULTIPLIER : 1.0f);
        Vector3 delta = Vector3Scale(wish, speed * delta_time);
        delta.y = ((input.jump ? 1.0f : 0.0f) - (input.sneak ? 1.0f : 0.0f)) * speed * delta_time;
        eyes.position = Vector3Add(eyes.position, delta);
        eyes.target = Vector3Add(eyes.target, delta);
        horizontal_velocity = {0.0f, 0.0f, 0.0f};
        vertical_velocity = 0.0f;
        grounded = false;
        return;
    }

    grounded = box_blocked(world, {feet.x, feet.y - GROUND_PROBE, feet.z});
    // Fluid contact is an AABB-volume query, not one sample at the player's
    // centre.  Keeping water physics active while any part of the 0.6-wide
    // hitbox still intersects the bank-side water cell is essential for
    // climbing out instead of losing buoyancy halfway over the edge.
    bool in_water = touching_water;
    float desired_speed = input.sneak ? SNEAK_SPEED : input.sprint ? SPRINT_SPEED : WALK_SPEED;
    if (in_water) desired_speed = WATER_SPEED;
    Vector3 desired = Vector3Scale(wish, desired_speed);
    float acceleration = grounded ? GROUND_ACCELERATION : AIR_ACCELERATION;
    if (Vector3LengthSqr(wish) < 0.000001f && grounded) acceleration = GROUND_DECELERATION;
    horizontal_velocity.x = move_towards(horizontal_velocity.x, desired.x, acceleration * delta_time);
    horizontal_velocity.z = move_towards(horizontal_velocity.z, desired.z, acceleration * delta_time);

    if (in_water) {
        float target_vertical = input.jump ? WATER_SPEED : input.sneak ? -WATER_SPEED : -0.35f;
        vertical_velocity = move_towards(vertical_velocity, target_vertical, 10.0f * delta_time);
    } else if (grounded && vertical_velocity <= 0.0f) {
        vertical_velocity = input.jump ? JUMP_VELOCITY : 0.0f;
        if (input.jump) grounded = false;
    } else {
        vertical_velocity = std::max(TERMINAL_VELOCITY, vertical_velocity - GRAVITY * delta_time);
        vertical_velocity *= std::pow(VERTICAL_DRAG_PER_TICK, delta_time * TICKS_PER_SECOND);
    }

    bool blocked_x = move_axis(world, feet, horizontal_velocity.x * delta_time, 0);
    bool blocked_z = move_axis(world, feet, horizontal_velocity.z * delta_time, 2);
    // Dry-land stepping and water-bank climbing are deliberately separate:
    // applying both the positional step and the fluid exit impulse in one
    // frame would double the vertical motion and visibly pop the player.
    const bool may_step = grounded && !in_water;
    const float step_height = NORMAL_STEP_HEIGHT;
    if (blocked_x && may_step && rise_towards_step(
            world, feet, horizontal_velocity.x * delta_time, 0, step_height, delta_time)) {
        blocked_x = move_axis(world, feet, horizontal_velocity.x * delta_time, 0);
    }
    if (blocked_z && may_step && rise_towards_step(
            world, feet, horizontal_velocity.z * delta_time, 2, step_height, delta_time)) {
        blocked_z = move_axis(world, feet, horizontal_velocity.z * delta_time, 2);
    }
    if (in_water && input.jump && (blocked_x || blocked_z)) {
        vertical_velocity = std::max(vertical_velocity, WATER_EXIT_VELOCITY);
    }
    bool blocked_y = move_axis(world, feet, vertical_velocity * delta_time, 1);
    if (blocked_x) horizontal_velocity.x = 0.0f;
    if (blocked_z) horizontal_velocity.z = 0.0f;
    if (blocked_y) {
        if (vertical_velocity < 0.0f) grounded = true;
        vertical_velocity = 0.0f;
    }

    eyes.position = {feet.x, feet.y + EYE_HEIGHT, feet.z};
    Vector3 shift = Vector3Subtract(eyes.position, previous);
    eyes.target = Vector3Add(eyes.target, shift);
}

Vector3 PlayerController::feet_position(const Camera3D& eyes) const
{
    return {eyes.position.x, eyes.position.y - EYE_HEIGHT, eyes.position.z};
}

Vector3 PlayerController::closest_hitbox_point(const Camera3D& eyes, Vector3 point) const
{
    Vector3 feet = feet_position(eyes);
    return {
        std::clamp(point.x, feet.x - HALF_WIDTH, feet.x + HALF_WIDTH),
        std::clamp(point.y, feet.y, feet.y + HEIGHT),
        std::clamp(point.z, feet.z - HALF_WIDTH, feet.z + HALF_WIDTH),
    };
}

bool PlayerController::intersects_block(const Camera3D& eyes, int x, int y, int z) const
{
    Vector3 feet = feet_position(eyes);
    return feet.x + HALF_WIDTH > x && feet.x - HALF_WIDTH < x + 1.0f &&
           feet.y + HEIGHT > y && feet.y < y + 1.0f &&
           feet.z + HALF_WIDTH > z && feet.z - HALF_WIDTH < z + 1.0f;
}

float PlayerController::horizontal_speed() const
{
    return std::sqrt(horizontal_velocity.x * horizontal_velocity.x + horizontal_velocity.z * horizontal_velocity.z);
}
