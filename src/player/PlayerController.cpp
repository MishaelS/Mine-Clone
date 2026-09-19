#include "player/PlayerController.hpp"
#include "core/Block.hpp"
#include "core/BlockShape.hpp"
#include "core/Tick.hpp"
#include "world/World.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr float HALF_WIDTH = PlayerController::WIDTH * 0.5f;
    constexpr float COLLISION_EPSILON = 0.001f;
    constexpr float GROUND_PROBE = 0.05f;

    constexpr float CREATIVE_SPEED             = 4.0f;
    constexpr float CREATIVE_SPRINT_MULTIPLIER = 2.0f;
    constexpr float WALK_SPEED                 = 4.317f;
    constexpr float SPRINT_SPEED               = 5.612f;
    constexpr float SNEAK_SPEED                = 1.30f;
    constexpr float GROUND_ACCELERATION        = 28.0f;
    constexpr float GROUND_DECELERATION        = 34.0f;
    constexpr float AIR_ACCELERATION           = 7.0f;
    constexpr float WATER_SPEED                = 1.2f;
    constexpr float WATER_FLOW_ACCELERATION    = 8.0f;
    constexpr float WATER_FLOW_MAX_SPEED       = 8.45f;
    constexpr float NORMAL_STEP_HEIGHT         = 0.6f;
    constexpr float STEP_SMOOTH_SPEED          = 7.0f;
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

    // True if the player's hitbox at `feet` overlaps ANY collision box of
    // ANY nearby cell - not just "is this whole cell solid" the way the
    // pre-shaped-block version of this function worked. A plain full-cube
    // solid block (the overwhelming majority) still costs exactly one
    // World::collision_boxes_at() fast-path call plus one box-overlap test
    // per cell; only a block_has_custom_shape() cell (stairs, trapdoors,
    // doors, beds) pays for testing its real, possibly-partial box list,
    // which is what lets the player stand on a stair's step at the right
    // height or walk under an open trapdoor instead of the whole cell
    // blocking/passing as one unit.
    bool box_blocked(const World& world, Vector3 feet)
    {
        BoundingBox player_box{
            {feet.x - HALF_WIDTH + COLLISION_EPSILON, feet.y + COLLISION_EPSILON, feet.z - HALF_WIDTH + COLLISION_EPSILON},
            {feet.x + HALF_WIDTH - COLLISION_EPSILON, feet.y + PlayerController::HEIGHT - COLLISION_EPSILON, feet.z + HALF_WIDTH - COLLISION_EPSILON},
        };
        int min_x = static_cast<int>(std::floor(player_box.min.x));
        int max_x = static_cast<int>(std::floor(player_box.max.x));
        int min_y = static_cast<int>(std::floor(player_box.min.y));
        int max_y = static_cast<int>(std::floor(player_box.max.y));
        int min_z = static_cast<int>(std::floor(player_box.min.z));
        int max_z = static_cast<int>(std::floor(player_box.max.z));
        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int z = min_z; z <= max_z; ++z) {
                    BlockShapeBoxes shape = world.collision_boxes_at(x, y, z);
                    for (int i = 0; i < shape.count; ++i) {
                        if (CheckCollisionBoxes(player_box, shape.boxes[i])) return true;
                    }
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

    Vector3 box_water_flow(const World& world, Vector3 feet)
    {
        const int min_x = static_cast<int>(std::floor(feet.x - HALF_WIDTH + COLLISION_EPSILON));
        const int max_x = static_cast<int>(std::floor(feet.x + HALF_WIDTH - COLLISION_EPSILON));
        const int min_y = static_cast<int>(std::floor(feet.y + COLLISION_EPSILON));
        const int max_y = static_cast<int>(std::floor(feet.y + PlayerController::HEIGHT - COLLISION_EPSILON));
        const int min_z = static_cast<int>(std::floor(feet.z - HALF_WIDTH + COLLISION_EPSILON));
        const int max_z = static_cast<int>(std::floor(feet.z + HALF_WIDTH - COLLISION_EPSILON));

        Vector3 total{0.0f, 0.0f, 0.0f};
        int count = 0;
        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int z = min_z; z <= max_z; ++z) {
                    if (world.get_block(x, y, z) != BlockType::Water ||
                        feet.y >= y + WATER_SURFACE_HEIGHT ||
                        feet.y + PlayerController::HEIGHT <= y) {
                        continue;
                    }

                    Vector3 flow = world.water_flow_at({x + 0.5f, y + 0.5f, z + 0.5f});
                    if (Vector3LengthSqr(flow) <= 0.000001f) continue;
                    total = Vector3Add(total, flow);
                    ++count;
                }
            }
        }
        if (count == 0 || Vector3LengthSqr(total) <= 0.000001f) return {0.0f, 0.0f, 0.0f};
        return Vector3Normalize(total);
    }

    // Same shape as box_touches_water, minus the partial-surface-height
    // check (lava has no equivalent "floating on the surface" case that
    // matters for damage - any overlap with the cell counts) - used for
    // lava-contact damage/catching fire.
    bool box_touches_lava(const World& world, Vector3 feet)
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
                    if (world.get_block(x, y, z) == BlockType::Lava) return true;
                }
            }
        }
        return false;
    }

    // Generalizes what used to be a single hardcoded BlockType::Cactus
    // check into anything blocks.json flags damages_on_touch (see
    // BlockProperties::damages_on_touch) - cactus is a full solid collision
    // cube in this engine (unlike real Minecraft's own slightly-inset
    // cactus hitbox), so the movement solver above never actually lets the
    // hitbox overlap a damaging cell; probing with a small horizontal
    // margin instead means standing flush against one still counts as
    // contact, the same way vanilla's inset hitbox lets a flush-pressed
    // player take damage without ever being "inside" it.
    bool box_touches_damaging_block(const World& world, Vector3 feet)
    {
        constexpr float MARGIN = 0.1f;
        const int min_x = static_cast<int>(std::floor(feet.x - HALF_WIDTH - MARGIN + COLLISION_EPSILON));
        const int max_x = static_cast<int>(std::floor(feet.x + HALF_WIDTH + MARGIN - COLLISION_EPSILON));
        const int min_y = static_cast<int>(std::floor(feet.y + COLLISION_EPSILON));
        const int max_y = static_cast<int>(std::floor(feet.y + PlayerController::HEIGHT - COLLISION_EPSILON));
        const int min_z = static_cast<int>(std::floor(feet.z - HALF_WIDTH - MARGIN + COLLISION_EPSILON));
        const int max_z = static_cast<int>(std::floor(feet.z + HALF_WIDTH + MARGIN - COLLISION_EPSILON));
        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int z = min_z; z <= max_z; ++z) {
                    if (get_block_properties(world.get_block(x, y, z)).damages_on_touch) return true;
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
    // move fits. Commit that height atomically: partial per-frame rises can
    // still leave the hitbox intersecting the slab/stair side, which makes
    // the horizontal solver cancel velocity and produces visible jitter.
    // Water-bank exits use their own impulse below.
    float rise_towards_step(const World& world, Vector3& feet, float horizontal_delta,
                            int axis, float maximum_height)
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
        if (required_height <= 0.0f) return 0.0f;

        Vector3 raised = feet;
        raised.y += required_height;
        if (box_blocked(world, raised)) return 0.0f;
        feet.y = raised.y;
        return required_height;
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
    step_visual_offset = 0.0f;
    grounded = false;
    touching_water = false;
    touching_lava = false;
    touching_cactus = false;
    head_submerged = false;
    suffocating = false;
    fall_distance = 0.0f;
    just_landed = false;
    landing_fall_distance = 0.0f;
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
    touching_lava = box_touches_lava(world, feet);
    touching_cactus = box_touches_damaging_block(world, feet);
    if (mode == GameMode::Creative) {
        float speed = CREATIVE_SPEED * (input.sprint ? CREATIVE_SPRINT_MULTIPLIER : 1.0f);
        Vector3 delta = Vector3Scale(wish, speed * delta_time);
        delta.y = ((input.jump ? 1.0f : 0.0f) - (input.sneak ? 1.0f : 0.0f)) * speed * delta_time;
        eyes.position = Vector3Add(eyes.position, delta);
        eyes.target = Vector3Add(eyes.target, delta);
        horizontal_velocity = {0.0f, 0.0f, 0.0f};
        vertical_velocity = 0.0f;
        step_visual_offset = 0.0f;
        grounded = false;
        // Creative is invulnerable (GameEngine never reads these while in
        // that mode), but keep them from holding a stale true from before
        // a hypothetical mode switch.
        head_submerged = false;
        suffocating = false;
        fall_distance = 0.0f;
        just_landed = false;
        return;
    }

    bool prev_grounded = grounded; // for the landing edge below - see just_landed
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
        Vector3 flow = box_water_flow(world, feet);
        if (Vector3LengthSqr(flow) > 0.000001f) {
            Vector3 flow_target = Vector3Scale(flow, WATER_FLOW_MAX_SPEED);
            horizontal_velocity.x = move_towards(horizontal_velocity.x, desired.x + flow_target.x,
                                                 WATER_FLOW_ACCELERATION * delta_time);
            horizontal_velocity.z = move_towards(horizontal_velocity.z, desired.z + flow_target.z,
                                                 WATER_FLOW_ACCELERATION * delta_time);
        }
    }

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
    float feet_y_before_step = feet.y;
    float step_x = blocked_x && may_step
        ? rise_towards_step(world, feet, horizontal_velocity.x * delta_time, 0, step_height)
        : 0.0f;
    if (step_x > 0.0f) {
        blocked_x = move_axis(world, feet, horizontal_velocity.x * delta_time, 0);
    }
    float step_z = blocked_z && may_step
        ? rise_towards_step(world, feet, horizontal_velocity.z * delta_time, 2, step_height)
        : 0.0f;
    if (step_z > 0.0f) {
        blocked_z = move_axis(world, feet, horizontal_velocity.z * delta_time, 2);
    }
    float stepped_height = feet.y - feet_y_before_step;
    if (stepped_height > 0.0f) {
        step_visual_offset = std::min(NORMAL_STEP_HEIGHT, step_visual_offset + stepped_height);
    }
    if (in_water && input.jump && (blocked_x || blocked_z)) {
        vertical_velocity = std::max(vertical_velocity, WATER_EXIT_VELOCITY);
    }
    float feet_y_before_vertical = feet.y;
    bool blocked_y = move_axis(world, feet, vertical_velocity * delta_time, 1);
    if (blocked_x) horizontal_velocity.x = 0.0f;
    if (blocked_z) horizontal_velocity.z = 0.0f;
    if (blocked_y) {
        if (vertical_velocity < 0.0f) grounded = true;
        vertical_velocity = 0.0f;
    }

    // Fall-distance bookkeeping (real Minecraft's own rule): resets the
    // instant any part of the hitbox touches water (a water landing is
    // always safe), accumulates only while actually descending, and resets
    // on an ascending step too (a jump's upward half isn't "falling" -
    // distance starts fresh again from whatever apex it reaches). Reported
    // back to GameEngine only on the exact frame grounded flips false ->
    // true, via consume_landing_fall_distance() - see its own comment.
    float descended = feet_y_before_vertical - feet.y;
    if (in_water) {
        fall_distance = 0.0f;
    } else if (descended > 0.0f) {
        fall_distance += descended;
    } else if (vertical_velocity > 0.0f) {
        fall_distance = 0.0f;
    }
    just_landed = grounded && !prev_grounded;
    if (just_landed) {
        landing_fall_distance = fall_distance;
        fall_distance = 0.0f;
    }

    step_visual_offset = std::max(0.0f, step_visual_offset - STEP_SMOOTH_SPEED * delta_time);
    eyes.position = {feet.x, feet.y + EYE_HEIGHT - step_visual_offset, feet.z};
    Vector3 shift = Vector3Subtract(eyes.position, previous);
    eyes.target = Vector3Add(eyes.target, shift);

    // Suffocation/drowning both key off the eye position specifically (a
    // block clipped into just the player's head, or a head that's dipped
    // below the water surface while the feet aren't necessarily submerged
    // at all), not the whole hitbox AABB the checks above use.
    int eye_x = static_cast<int>(std::floor(eyes.position.x));
    int eye_y = static_cast<int>(std::floor(eyes.position.y));
    int eye_z = static_cast<int>(std::floor(eyes.position.z));
    BlockType eye_block = world.get_block(eye_x, eye_y, eye_z);
    const BlockProperties& eye_properties = get_block_properties(eye_block);
    // Solid and NOT flagged transparent - excludes leaves/glass (both
    // solid but transparent) the same way real Minecraft's own
    // non-suffocating full blocks are excluded, while still catching an
    // ordinary opaque block (stone, dirt, ...) placed into the player.
    suffocating = eye_properties.solid && !eye_properties.transparent;
    head_submerged = eye_block == BlockType::Water && eyes.position.y < eye_y + WATER_SURFACE_HEIGHT;
}

float PlayerController::consume_landing_fall_distance()
{
    if (!just_landed) return -1.0f;
    just_landed = false;
    return landing_fall_distance;
}

Vector3 PlayerController::feet_position(const Camera3D& eyes) const
{
    return {eyes.position.x, eyes.position.y - EYE_HEIGHT + step_visual_offset, eyes.position.z};
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
