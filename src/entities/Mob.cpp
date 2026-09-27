#include "entities/Mob.hpp"
#include "core/TextureManager.hpp"
#include "core/Tick.hpp"
#include "model/EntityModelRenderer.hpp"
#include "model/ModelLibrary.hpp"
#include "rendering/EntityLighting.hpp"
#include "world/World.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace {
    // Per-tick physics, blocks/tick - vanilla's living-entity values.
    constexpr float GRAVITY                 = 0.08f;
    constexpr float VERTICAL_DRAG           = 0.98f;
    constexpr float JUMP_VELOCITY           = 0.42f;    // clears one block
    constexpr float WATER_GRAVITY           = 0.02f;
    constexpr float WATER_DRAG              =  0.8f;
    constexpr float SWIM_UP                 = 0.04f;    // jumping in water - see FloatGoal
    constexpr float WATER_EXIT_JUMP         =  0.3f;    // swimming into a bank: climbs out onto it
    constexpr float WATER_WALK_FACTOR       =  0.5f;    // swimming is slower than walking
    constexpr float COLLISION_EPSILON       = 0.001f;
    constexpr float PUSH_DECAY              =  0.6f;    // share of a shove still moving it the next tick
    constexpr float DEFAULT_HEAD_TURN_LIMIT = 75.0f;

    // Being hit - vanilla's knockback and invulnerability time.
    constexpr float KNOCKBACK_SPEED = 0.4f;     // blocks/tick, fading like a push
    constexpr float KNOCKBACK_LIFT  = 0.36f;    // blocks/tick up, when on the ground
    constexpr int HURT_TICKS        = 10;       // red flash, and no new hit counts meanwhile
    constexpr int HURT_MEMORY_TICKS = 100;      // see Mob::recently_hurt()
    constexpr int DEATH_TICKS       = 20;       // tipping over before it's gone
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
    previous_body_yaw   = body_yaw_degrees;
    previous_head_yaw   = head_yaw_degrees;
    previous_head_pitch = head_pitch_degrees;
    jumping = false;
    ++age_ticks;
    if (hurt_ticks > 0) --hurt_ticks;
    if (hurt_memory_ticks > 0) --hurt_memory_ticks;

    if (dying) {
        // No more thinking - it just tips over where it stands.
        ++death_ticks;
        forward_speed = 0.0f;
        move(world);
        set_position(motion.current);
        return;
    }

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

void Mob::play_animation(const std::string& name)
{
    manual_animation = name;
    manual_animation_start = age_ticks;
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
    // Timed by game ticks, so it stays in step with the AI that started it.
    animator.set_manual(manual_animation,
                        (static_cast<float>(age_ticks - manual_animation_start) + tick_alpha) * TICK_DURATION);

    ModelPose pose = animator.pose(entity);
    apply_head_look(entity, pose, wrap_degrees(head_yaw - yaw), head_pitch);

    const Texture2D& skin = entity.skin.empty() ? Texture2D{} : TextureManager::get(entity.skin);
    Color tint = entity_environment_tint(world, Vector3Add(position, {0.0f, dimensions.height * 0.5f, 0.0f}));
    if (hurt_ticks > 0 || dying) {
        // Just hit (or dying): flushed red, like Minecraft.
        tint.g = static_cast<unsigned char>(tint.g * 0.4f);
        tint.b = static_cast<unsigned char>(tint.b * 0.4f);
    }
    rlPushMatrix();
    rlTranslatef(position.x, position.y, position.z);
    rlRotatef(yaw, 0.0f, 1.0f, 0.0f);
    if (dying) {
        // Falls over onto its side, fast at first - Minecraft's death tilt.
        const float fall = std::min(1.0f, (static_cast<float>(death_ticks) + tick_alpha) / DEATH_TICKS);
        rlRotatef(std::sqrt(fall) * 90.0f, 0.0f, 0.0f, 1.0f);
    }
    draw_entity_model(entity, skin, pose, model_scale(), tint);
    // Its layers (a sheep's wool) on top, in the same pose.
    for (const std::string& layer_name : entity.layers) {
        if (!shows_layer(layer_name)) continue;
        const EntityModel& layer = entity_model(layer_name);
        if (layer.parts.empty()) continue;
        const Texture2D& layer_skin = layer.skin.empty() ? Texture2D{} : TextureManager::get(layer.skin);
        draw_entity_model(layer, layer_skin, map_pose(entity, pose, layer), model_scale(), tint);
    }
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

std::optional<float> Mob::ray_distance(Vector3 origin, Vector3 direction) const
{
    const Vector3 feet = motion.current;
    const float half = dimensions.width * 0.5f;
    const RayCollision hit = GetRayCollisionBox(
        {origin, direction}, {{feet.x - half, feet.y, feet.z - half}, {feet.x + half, feet.y + dimensions.height, feet.z + half}});
    if (!hit.hit) return std::nullopt;
    return hit.distance;
}

int Mob::health() const
{
    return hit_points_left >= 0 ? hit_points_left : model().entity.health;
}

bool Mob::is_dead() const
{
    return dying && death_ticks >= DEATH_TICKS;
}

std::vector<ItemStack> Mob::roll_drops()
{
    std::vector<ItemStack> drops;
    for (const EntityDropInfo& drop : model().entity.drops) {
        if (!drop.unless_state.empty() && has_state(drop.unless_state)) continue;
        if (random_float() >= drop.chance) continue;
        const int count = random_int(drop.min_count, std::max(drop.min_count, drop.max_count));
        if (count <= 0) continue;
        if (std::optional<ItemRef> item = item_ref_from_name(drop.item)) {
            drops.push_back(item->stack(count));
        } else {
            TraceLog(LOG_WARNING, "%s drop: no block or item named '%s'", type_id(), drop.item.c_str());
        }
    }
    return drops;
}

const std::vector<InteractionRule>& Mob::rules()
{
    if (!rules_built) {
        std::vector<InteractionRule> from_model;
        for (const EntityInteractionInfo& info : model().entity.interactions) from_model.push_back(rule_from_info(info));
        all_rules.insert(all_rules.begin(), from_model.begin(), from_model.end());
        rules_built = true;
    }
    return all_rules;
}

bool Mob::hurt(Vector3 from, int damage)
{
    if (hurt_ticks > 0 || dying) return false;
    hit_points_left = std::max(0, health() - std::max(0, damage));
    if (hit_points_left == 0) dying = true;
    hurt_ticks = HURT_TICKS;
    hurt_memory_ticks = HURT_MEMORY_TICKS;
    hurt_from = from;

    Vector3 away = {motion.current.x - from.x, 0.0f, motion.current.z - from.z};
    const float length = std::sqrt(away.x * away.x + away.z * away.z);
    away = length > 0.001f ? Vector3Scale(away, 1.0f / length) : Vector3{std::sin(body_yaw_degrees * DEG2RAD), 0.0f, std::cos(body_yaw_degrees * DEG2RAD)};
    push_velocity = Vector3Add(Vector3Scale(push_velocity, 0.5f), Vector3Scale(away, KNOCKBACK_SPEED));
    if (grounded) velocity.y = KNOCKBACK_LIFT;
    return true;
}

InteractionResult Mob::interact(InteractionTrigger trigger, const ItemStack& held)
{
    InteractionResult result;
    if (dying) return result;
    for (const InteractionRule& rule : rules()) {
        if (rule.trigger != trigger || !rule.holds_right_item(held)) continue;
        if (!rule.required_state.empty() && !has_state(rule.required_state)) continue;
        if (!rule.blocking_state.empty() && has_state(rule.blocking_state)) continue;
        result.handled = true; // a matching right click is used up even when its chance misses
        if (random_float() >= rule.probability) continue;

        if (rule.drop) result.drops.push_back(rule.drop->stack(random_int(rule.drop_min, std::max(rule.drop_min, rule.drop_max))));
        if (rule.hand_result && !result.in_hand) result.in_hand = rule.hand_result->stack(1);
        if (!rule.state_to_set.empty()) set_state(rule.state_to_set, true);
        if (!rule.state_to_clear.empty()) set_state(rule.state_to_clear, false);
    }
    return result;
}

bool Mob::has_state(const std::string& state) const
{
    return std::find(state_flags.begin(), state_flags.end(), state) != state_flags.end();
}

void Mob::set_state(const std::string& state, bool on)
{
    auto found = std::find(state_flags.begin(), state_flags.end(), state);
    if (on && found == state_flags.end()) state_flags.push_back(state);
    if (!on && found != state_flags.end()) state_flags.erase(found);
}

std::optional<Mob::BlockChange> Mob::take_block_change()
{
    std::optional<BlockChange> change = requested_change;
    requested_change.reset();
    return change;
}
