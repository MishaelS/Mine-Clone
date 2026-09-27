#include "entities/ai/CommonGoals.hpp"
#include "entities/Mob.hpp"
#include "world/World.hpp"

#include "raymath.h"

#include <cmath>

namespace ai {

namespace {
    constexpr float FLOAT_DEPTH = 0.4f;        // share of its height under water before it swims up
    constexpr float FLOAT_JUMP_CHANCE = 0.8f;  // per tick, so it bobs instead of hovering
    constexpr int STROLL_TRIES = 10;
    constexpr int STROLL_SNAP_RANGE = 3;       // blocks up/down to find ground under a random spot
    constexpr float LOOK_AWAY_CHANCE = 0.5f;   // after watching the player
    constexpr float LOOK_AWAY_SPREAD = 60.0f;  // degrees either side of straight away
    constexpr float RANDOM_LOOK_CHANCE = 0.02f;

    // A point `yaw` degrees around from its eyes, level with them.
    Vector3 point_toward(const Mob& mob, float yaw)
    {
        return Vector3Add(mob.eye_position(), {std::sin(yaw * DEG2RAD), 0.0f, std::cos(yaw * DEG2RAD)});
    }
}

bool FloatGoal::can_use(Mob& mob, const AiContext& context)
{
    return mob.water_at(context.world, mob.height() * FLOAT_DEPTH);
}

void FloatGoal::tick(Mob& mob, const AiContext&)
{
    if (mob.random_float() < FLOAT_JUMP_CHANCE) mob.set_jumping(true);
}

bool RandomStrollGoal::can_use(Mob& mob, const AiContext& context)
{
    if (mob.random_int(0, settings.interval - 1) != 0) return false;
    std::optional<Vector3> picked = pick_target(mob, context.world);
    if (!picked) return false;
    target = *picked;
    return true;
}

bool RandomStrollGoal::can_continue(Mob& mob, const AiContext&)
{
    return !mob.navigation().done();
}

void RandomStrollGoal::start(Mob& mob, const AiContext& context)
{
    mob.navigation().move_to(mob, context.world, target, settings.speed);
}

void RandomStrollGoal::stop(Mob& mob)
{
    mob.navigation().stop();
}

std::optional<Vector3> RandomStrollGoal::pick_target(Mob& mob, const World& world) const
{
    // A few random spots around it; the best-liked one wins (Minecraft's
    // DefaultRandomPos and walk-target values).
    const Vector3 feet = mob.get_position();
    const int height_cells = mob.path_settings().height_cells;
    std::optional<Vector3> best;
    float best_score = -1.0f;
    for (int i = 0; i < STROLL_TRIES; ++i) {
        const int x = static_cast<int>(std::floor(feet.x)) + mob.random_int(-settings.horizontal_range, settings.horizontal_range);
        const int z = static_cast<int>(std::floor(feet.z)) + mob.random_int(-settings.horizontal_range, settings.horizontal_range);
        const int y = static_cast<int>(std::floor(feet.y)) + mob.random_int(-settings.vertical_range, settings.vertical_range);
        if (!world.is_column_loaded(x, z)) continue;
        std::optional<PathNode> cell = standing_cell_near(world, x, y, z, height_cells, STROLL_SNAP_RANGE);
        if (!cell) continue;
        const Vector3 spot = cell->center();
        if (settings.avoid_water && world.get_block(cell->x, cell->y, cell->z) == BlockType::Water) continue;
        if (settings.home && Vector3Distance(spot, *settings.home) > settings.home_radius) continue;

        float score = mob.random_float();
        if (settings.prefer_grass && world.get_block(cell->x, cell->y - 1, cell->z) == BlockType::Grass) score += 10.0f;
        if (score > best_score) {
            best_score = score;
            best = spot;
        }
    }
    return best;
}

bool LookAtPlayerGoal::player_in_range(Mob& mob, const AiContext& context) const
{
    return context.player.present && Vector3Distance(mob.eye_position(), context.player.eyes) <= range;
}

bool LookAtPlayerGoal::can_use(Mob& mob, const AiContext& context)
{
    return mob.random_float() < chance && player_in_range(mob, context);
}

bool LookAtPlayerGoal::can_continue(Mob& mob, const AiContext& context)
{
    return look_ticks > 0 ? player_in_range(mob, context) : look_away_ticks > 0;
}

void LookAtPlayerGoal::start(Mob& mob, const AiContext&)
{
    look_ticks = mob.random_int(40, 79);
    look_away_ticks = 0;
}

void LookAtPlayerGoal::tick(Mob& mob, const AiContext& context)
{
    if (look_ticks > 0) {
        mob.look_control().look_at(context.player.eyes, 10.0f, 40.0f);
        if (--look_ticks == 0 && mob.random_float() < LOOK_AWAY_CHANCE) {
            // Had enough: turns its back on the player - the head first, the
            // body following once the head can't turn any further.
            const Vector3 feet = mob.get_position();
            const float away = std::atan2(feet.x - context.player.feet.x, feet.z - context.player.feet.z) * RAD2DEG;
            look_away_yaw = away + (mob.random_float() * 2.0f - 1.0f) * LOOK_AWAY_SPREAD;
            look_away_ticks = mob.random_int(30, 60);
        }
    } else if (look_away_ticks > 0) {
        mob.look_control().look_at(point_toward(mob, look_away_yaw), 8.0f, 40.0f);
        --look_away_ticks;
    }
}

bool RandomLookAroundGoal::can_use(Mob& mob, const AiContext&)
{
    return mob.random_float() < RANDOM_LOOK_CHANCE;
}

bool RandomLookAroundGoal::can_continue(Mob&, const AiContext&)
{
    return ticks >= 0;
}

void RandomLookAroundGoal::start(Mob& mob, const AiContext&)
{
    yaw = mob.random_float() * 360.0f - 180.0f;
    ticks = mob.random_int(20, 39);
}

void RandomLookAroundGoal::tick(Mob& mob, const AiContext&)
{
    --ticks;
    mob.look_control().look_at(point_toward(mob, yaw), 10.0f, 40.0f);
}

} // namespace ai
