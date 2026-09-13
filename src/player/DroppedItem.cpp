#include "player/DroppedItem.hpp"
#include "player/Item.hpp"
#include "world/World.hpp"
#include "rendering/BlockMesh.hpp"
#include "core/Tick.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace {
    // Vanilla's own per-tick item-entity numbers (blocks/tick, multiplicative
    // drag) - see http://minecraft.wiki/w/Entity's "Motion of entities" and
    // http://minecraft.wiki/w/Falling_Block: gravity 0.04, vertical drag
    // 0.98. Horizontal drag isn't published for items specifically; 0.91
    // (the generic living-entity figure) is used here as a reasonable
    // stand-in.
    constexpr float GRAVITY_PER_TICK    = 0.04f;
    constexpr float AIR_DRAG_VERTICAL   = 0.98f;
    constexpr float AIR_DRAG_HORIZONTAL = 0.91f;

    // Not vanilla numbers - pre-1.13 (this project's Beta-era target) items
    // don't float at all, and even modern vanilla doesn't vary buoyancy by
    // item type (see BlockProperties::density's own comment). Tuned by eye
    // for "settles at the surface in a few ticks instead of bobbing".
    constexpr float WATER_DRAG_VERTICAL     = 0.8f;
    constexpr float WATER_DRAG_HORIZONTAL   = 0.9f;
    constexpr float WATER_REFERENCE_DENSITY = 1.0f; // water's own density, the sink/float threshold
    // Square-rooted (see tick_physics()) rather than used directly - a
    // metal block's density (5.0) would otherwise scale gravity 5x, a
    // wildly fast fall for what's still just a small dropped item. The
    // sqrt curve keeps heavy-vs-light still clearly different while
    // reining in just how extreme the heaviest blocks get.
    constexpr float MIN_GRAVITY_SCALE = 0.5f;
    constexpr float MAX_GRAVITY_SCALE = 1.8f;

    constexpr float INITIAL_POP_VELOCITY = 0.11f; // blocks/tick, ~2.2 blocks/s - the old straight-up-pop speed

    constexpr float ITEM_HALF_SIZE = 0.14f;
    constexpr float BOB_HEIGHT = 0.04f;
    constexpr float BOB_SPEED = 3.0f;
    constexpr float ROTATION_SPEED = 45.0f;

    constexpr float MERGE_RADIUS = 0.7f;
    constexpr float MAGNET_RADIUS = 1.0f;
    constexpr float MAGNET_PULL_SPEED = 3.0f; // blocks/second, at its strongest right at the pickup radius

    bool blocked(const World* world, Vector3 position) {
        int x = static_cast<int>(std::floor(position.x));
        int y = static_cast<int>(std::floor(position.y));
        int z = static_cast<int>(std::floor(position.z));
        return get_block_properties(world->get_block(x, y, z)).solid;
    }

    // A tool has no BlockType (and so no BlockProperties::density) of its
    // own to fall/float by - treated as exactly water's own density
    // (falls at the plain baseline rate, neither floats nor sinks
    // unusually fast), same neutral-default spirit as a block that never
    // overrode "density" in blocks.json.
    float effective_density(const ItemStack& stack) {
        return stack.is_tool() ? WATER_REFERENCE_DENSITY : get_block_properties(stack.block).density;
    }

    // A tool's flat icon (items.png), as a cross of two quads at 90
    // degrees to each other - the same trick cross-plane plant sprites use
    // - so it still reads as an icon from any horizontal angle as it spins
    // in place, without this project needing to add a camera-facing
    // billboard path just for this one case.
    void draw_item_cross_quad(const ItemProperties& properties)
    {
        const Texture2D& atlas = get_item_atlas_texture();
        float u0 = properties.atlas_source.x / static_cast<float>(atlas.width);
        float v0 = properties.atlas_source.y / static_cast<float>(atlas.height);
        float u1 = u0 + properties.atlas_source.width / static_cast<float>(atlas.width);
        float v1 = v0 + properties.atlas_source.height / static_cast<float>(atlas.height);
        constexpr float H = 0.5f;
        const Vector3 planes[2][4] = {
            {{-H, H, 0.0f}, {-H, -H, 0.0f}, {H, -H, 0.0f}, {H, H, 0.0f}},
            {{0.0f, H, -H}, {0.0f, -H, -H}, {0.0f, -H, H}, {0.0f, H, H}},
        };

        rlSetTexture(atlas.id);
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        for (const auto& plane : planes) {
            rlTexCoord2f(u0, v0); rlVertex3f(plane[0].x, plane[0].y, plane[0].z);
            rlTexCoord2f(u0, v1); rlVertex3f(plane[1].x, plane[1].y, plane[1].z);
            rlTexCoord2f(u1, v1); rlVertex3f(plane[2].x, plane[2].y, plane[2].z);
            rlTexCoord2f(u1, v0); rlVertex3f(plane[3].x, plane[3].y, plane[3].z);
        }
        rlEnd();
        rlSetTexture(0);
    }
}

DroppedItem::DroppedItem(Vector3 item_position, ItemStack item_stack, Vector3 launch_velocity)
    : Entity(item_position), stack(item_stack)
{
    motion.reset(item_position);
    velocity = Vector3Add({0.0f, INITIAL_POP_VELOCITY, 0.0f}, launch_velocity);
}

void DroppedItem::tick_physics(const World* world)
{
    motion.begin_tick();

    age += TICK_DURATION;
    if (age >= MAX_AGE) {
        active = false;
        return;
    }

    std::optional<int> submerged = world ? world->water_depth_at(motion.current) : std::nullopt;
    float density = effective_density(stack);

    if (submerged && density < WATER_REFERENCE_DENSITY) {
        // Buoyant: net upward accel proportional to how much less dense
        // than water this is, heavily damped so it settles at the surface
        // instead of oscillating.
        velocity.y += GRAVITY_PER_TICK * (WATER_REFERENCE_DENSITY - density);
        velocity.y *= WATER_DRAG_VERTICAL;
    } else {
        // Denser sinks faster, lighter sinks slower - a gameplay
        // simplification (real free-fall acceleration doesn't depend on
        // mass; this project's own "heavy things fall faster" comes from
        // scaling gravity itself, not drag, for a clearer, more readable
        // difference between block types).
        float gravity_scale = std::clamp(std::sqrt(density / WATER_REFERENCE_DENSITY), MIN_GRAVITY_SCALE, MAX_GRAVITY_SCALE);
        velocity.y -= GRAVITY_PER_TICK * gravity_scale;
        velocity.y *= submerged ? WATER_DRAG_VERTICAL : AIR_DRAG_VERTICAL;
    }
    velocity.x *= submerged ? WATER_DRAG_HORIZONTAL : AIR_DRAG_HORIZONTAL;
    velocity.z *= submerged ? WATER_DRAG_HORIZONTAL : AIR_DRAG_HORIZONTAL;

    Vector3 next = Vector3Add(motion.current, velocity);

    if (world) {
        // Ground collision - stop exactly on the surface instead of
        // sinking into it, same "check just below the item's own bottom"
        // approach as before falling was tick-based. Horizontal collision
        // is new: items never had sideways velocity before the break-
        // direction/Q-drop launch impulse existed to give them any.
        if (velocity.y <= 0.0f) {
            int gy = static_cast<int>(std::floor(next.y - ITEM_HALF_SIZE));
            if (blocked(world, {next.x, next.y - ITEM_HALF_SIZE, next.z})) {
                next.y = gy + 1.0f + ITEM_HALF_SIZE;
                velocity.y = 0.0f;
            }
        }
        if (blocked(world, {next.x, motion.current.y, motion.current.z})) {
            next.x = motion.current.x;
            velocity.x = 0.0f;
        }
        if (blocked(world, {motion.current.x, motion.current.y, next.z})) {
            next.z = motion.current.z;
            velocity.z = 0.0f;
        }
    }

    motion.current = next;
    set_position(next);
}

void DroppedItem::update_magnet_pull(float delta_time, Vector3 target)
{
    Vector3 to_target = Vector3Subtract(target, motion.current);
    float distance = Vector3Length(to_target);
    if (distance <= 0.0001f || distance > MAGNET_RADIUS) return;

    // Stronger the closer it already is, same shape real Minecraft's own
    // pickup pull uses - a gentle tug at the edge of the radius, a firm
    // pull right before it's collected.
    float strength = MAGNET_PULL_SPEED * (1.0f - distance / MAGNET_RADIUS);
    Vector3 pull = Vector3Scale(to_target, strength * delta_time / distance);
    motion.current = Vector3Add(motion.current, pull);
    set_position(motion.current);
}

bool DroppedItem::try_merge(DroppedItem& other)
{
    if (this == &other || !active || !other.active) return false;
    if (stack.is_tool() || other.stack.is_tool()) return false; // tools carry their own durability - never merge
    if (stack.block != other.stack.block) return false;
    // Still fresh from the same break - let them visibly separate first,
    // same PICKUP_DELAY that already gates picking one up too soon.
    if (!can_pick_up() || !other.can_pick_up()) return false;
    if (stack.count >= MAX_ITEM_STACK) return false;
    if (Vector3Distance(motion.current, other.motion.current) > MERGE_RADIUS) return false;

    int moved = std::min(other.stack.count, MAX_ITEM_STACK - stack.count);
    stack.count += moved;
    other.stack.count -= moved;
    if (other.stack.count <= 0) other.active = false;
    return true;
}

void DroppedItem::render(float tick_alpha) const
{
    Vector3 render_position = motion.interpolated(tick_alpha);
    float bob = std::sin(static_cast<float>(GetTime()) * BOB_SPEED) * BOB_HEIGHT;

    rlPushMatrix();
    rlTranslatef(render_position.x, render_position.y + bob, render_position.z);
    rlRotatef(static_cast<float>(GetTime()) * ROTATION_SPEED, 0.0f, 1.0f, 0.0f);
    if (stack.is_tool()) {
        rlScalef(ITEM_HALF_SIZE * 3.0f, ITEM_HALF_SIZE * 3.0f, ITEM_HALF_SIZE * 3.0f);
        draw_item_cross_quad(get_item_properties(stack.tool));
    } else {
        rlScalef(ITEM_HALF_SIZE * 2.0f, ITEM_HALF_SIZE * 2.0f, ITEM_HALF_SIZE * 2.0f);
        draw_block_cube(stack.block);
    }
    rlPopMatrix();
    // A merged stack (count > 1) still renders as a single icon, same as
    // it did before merging existed - no floating count label yet (the
    // inventory grid already has one; this would need its own 3D-billboard
    // text rendering, which nothing in this project does yet).
}
