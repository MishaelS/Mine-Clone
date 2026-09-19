#include "player/DroppedItem.hpp"
#include "player/Item.hpp"
#include "world/World.hpp"
#include "rendering/BlockMesh.hpp"
#include "rendering/EntityLighting.hpp"
#include "core/Tick.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <optional>

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

    constexpr float ITEM_HALF_SIZE       = 0.14f;
    constexpr float BOB_HEIGHT           = 0.04f;
    constexpr float BOB_SPEED            = 3.0f;
    constexpr float BLOCK_ROTATION_SPEED = 45.0f;

    constexpr float MERGE_RADIUS      = 0.7f;
    constexpr float MAGNET_RADIUS     = 1.0f;
    constexpr float MAGNET_PULL_SPEED = 3.0f; // blocks/second, at its strongest right at the pickup radius

    bool blocked(const World* world, Vector3 position) {
        int x = static_cast<int>(std::floor(position.x));
        int y = static_cast<int>(std::floor(position.y));
        int z = static_cast<int>(std::floor(position.z));
        return get_block_properties(world->get_block(x, y, z)).solid;
    }

    // A tool or material has no BlockType (and so no BlockProperties::
    // density) of its own to fall/float by - treated as exactly water's own
    // density (falls at the plain baseline rate, neither floats nor sinks
    // unusually fast), same neutral-default spirit as a block that never
    // overrode "density" in blocks.json.
    float effective_density(const ItemStack& stack) {
        return stack.holds_item() ? WATER_REFERENCE_DENSITY : get_block_properties(stack.block).density;
    }

    struct BillboardTexture {
        const Texture2D* texture;
        Rectangle uv;
        Color tint;
        float size;
    };

    // Flat item-atlas sprite for this stack, if it has one: always for a
    // tool/material, and for the handful of blocks items.json gives a
    // "block_items" sprite (torches, sapling, doors, bed). Everything else
    // renders as a small 3D block cube instead.
    std::optional<Rectangle> item_sprite(const ItemStack& stack)
    {
        if (stack.holds_item()) return get_item_properties(stack.tool).atlas_source;
        return get_block_item_sprite(stack.block);
    }

    BillboardTexture item_billboard_texture(Rectangle source)
    {
        const Texture2D& atlas = get_item_atlas_texture();
        constexpr float sub_texel = 1.0f / 1024.0f;
        Rectangle uv = {
            (source.x + sub_texel) / static_cast<float>(atlas.width),
            (source.y + sub_texel) / static_cast<float>(atlas.height),
            (source.width - sub_texel * 2.0f) / static_cast<float>(atlas.width),
            (source.height - sub_texel * 2.0f) / static_cast<float>(atlas.height),
        };
        return {&atlas, uv, WHITE, ITEM_HALF_SIZE * 3.0f};
    }

    void draw_billboard_quad(const BillboardTexture& billboard)
    {
        const float half = billboard.size * 0.5f;
        const float u0   = billboard.uv.x;
        const float v0   = billboard.uv.y;
        const float u1   = billboard.uv.x + billboard.uv.width;
        const float v1   = billboard.uv.y + billboard.uv.height;

        rlSetTexture(billboard.texture->id);
        rlBegin(RL_QUADS);
        rlColor4ub(billboard.tint.r, billboard.tint.g, billboard.tint.b, billboard.tint.a);
        rlNormal3f(0.0f, 0.0f, 1.0f);
        rlTexCoord2f(u0, v0); rlVertex3f(-half,  half, 0.0f);
        rlTexCoord2f(u0, v1); rlVertex3f(-half, -half, 0.0f);
        rlTexCoord2f(u1, v1); rlVertex3f( half, -half, 0.0f);
        rlTexCoord2f(u1, v0); rlVertex3f( half,  half, 0.0f);
        rlEnd();
        rlSetTexture(0);
    }
}

DroppedItem::DroppedItem(Vector3 item_position, ItemStack item_stack,
                         Vector3 launch_velocity, DroppedItemOrigin origin,
                         std::optional<Color> item_block_tint)
    : Entity(item_position)
    , stack(item_stack)
    , pickup_delay(origin == DroppedItemOrigin::PlayerThrown
          ? PLAYER_THROWN_PICKUP_DELAY : NATURAL_PICKUP_DELAY)
    , block_tint(item_block_tint)
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
    // The pickup delay also disables attraction. Merely preventing the
    // final collect is not enough: a Q-thrown item would otherwise reverse
    // direction immediately and wait at the player's hitbox.
    if (!can_pick_up()) return;

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
    // A material's `block` field is unused (stays Air) same as another
    // material's - comparing blocks alone would let two different
    // materials (e.g. Coal and Stick) merge into one bogus stack, so item
    // type has to match too.
    if (stack.block != other.stack.block || stack.tool != other.stack.tool) return false;
    // Still fresh from the same break - let them visibly separate first,
    // same per-item pickup delay that already gates picking one up too soon.
    if (!can_pick_up() || !other.can_pick_up()) return false;
    if (stack.count >= MAX_ITEM_STACK) return false;
    if (Vector3Distance(motion.current, other.motion.current) > MERGE_RADIUS) return false;

    int moved = std::min(other.stack.count, MAX_ITEM_STACK - stack.count);
    stack.count += moved;
    other.stack.count -= moved;
    if (other.stack.count <= 0) other.active = false;
    return true;
}

void DroppedItem::render(float tick_alpha, Vector3 viewer_position, const World& world) const
{
    Vector3 render_position = motion.interpolated(tick_alpha);
    float bob = std::sin(static_cast<float>(GetTime()) * BOB_SPEED) * BOB_HEIGHT;
    Color environment_tint = entity_environment_tint(world, render_position);

    rlPushMatrix();
    rlTranslatef(render_position.x, render_position.y + bob, render_position.z);
    if (std::optional<Rectangle> sprite = item_sprite(stack)) {
        Vector3 to_viewer = Vector3Subtract(viewer_position, render_position);
        // Cylindrical billboard: ignore pitch so the item remains vertical
        // even when the camera is above or below it.
        float yaw = std::atan2(to_viewer.x, to_viewer.z) * RAD2DEG;
        rlRotatef(yaw, 0.0f, 1.0f, 0.0f);
        BillboardTexture billboard = item_billboard_texture(*sprite);
        billboard.tint = multiply_tint(billboard.tint, environment_tint);
        draw_billboard_quad(billboard);
    } else {
        rlRotatef(static_cast<float>(GetTime()) * BLOCK_ROTATION_SPEED, 0.0f, 1.0f, 0.0f);
        rlScalef(ITEM_HALF_SIZE * 2.0f, ITEM_HALF_SIZE * 2.0f, ITEM_HALF_SIZE * 2.0f);
        draw_block_cube(stack.block, 255, block_tint, environment_tint);
    }
    rlPopMatrix();
    // A merged stack (count > 1) still renders as a single icon, same as
    // it did before merging existed - no floating count label yet (the
    // inventory grid already has one; this would need its own 3D-billboard
    // text rendering, which nothing in this project does yet).
}
