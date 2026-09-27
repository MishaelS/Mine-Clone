#include "entities/ai/MobAI.hpp"
#include "entities/ai/CommonGoals.hpp"
#include "entities/Mob.hpp"
#include "world/World.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace ai {

    namespace {
        constexpr float TEMPT_RANGE         = 10.0f; // blocks
        constexpr float TEMPT_STOP_DISTANCE = 2.5f;  // close enough - stands and watches
        constexpr int TEMPT_CALM_DOWN_TICKS = 100;
        constexpr int TEMPT_REPLAN_TICKS    = 10;
        constexpr float TEMPT_REPLAN_DISTANCE = 1.0f; // the player moved this far since the last plan

        constexpr int EAT_GRASS_CHANCE    = 1000;   // 1 in this many ticks, Minecraft's own
        constexpr int EAT_GRASS_TICKS     = 40;     // head down this long
        constexpr int EAT_GRASS_BITE_TICK = 4;      // ticks before the end the grass actually goes
        constexpr const char* EAT_ANIMATION = "eat";

        struct GrassSpot {
            int x, y, z;
            bool tall; // tall grass at its feet, rather than the grass block under them
        };

        std::optional<GrassSpot> grass_under(const Mob& mob, const World& world) {
            const Vector3 feet = mob.get_position();
            const int x = static_cast<int>(std::floor(feet.x));
            const int y = static_cast<int>(std::floor(feet.y + 0.01f));
            const int z = static_cast<int>(std::floor(feet.z));
            if (world.get_block(x, y    , z) == BlockType::ShortGrass) return GrassSpot{x, y    , z, true };
            if (world.get_block(x, y - 1, z) == BlockType::Grass     ) return GrassSpot{x, y - 1, z, false};
            return std::nullopt;
        }
    }

    bool TemptGoal::tempted(Mob& mob, const AiContext& context) const {
        const PlayerView& player = context.player;
        if (!player.present || player.held.empty() || !player.held.holds_item()) return false;
        if (std::find(items.begin(), items.end(), player.held.tool) == items.end()) return false;
        return Vector3Distance(mob.get_position(), player.feet) <= TEMPT_RANGE;
    }

    bool TemptGoal::can_use(Mob& mob, const AiContext& context) {
        if (calm_down_ticks > 0) {
            --calm_down_ticks;
            return false;
        }
        return tempted(mob, context);
    }

    bool TemptGoal::can_continue(Mob& mob, const AiContext& context) {
        return tempted(mob, context);
    }

    void TemptGoal::start(Mob&, const AiContext&) {
        replan_ticks = 0;
    }

    void TemptGoal::stop(Mob& mob) {
        mob.navigation().stop();
        calm_down_ticks = TEMPT_CALM_DOWN_TICKS;
    }

    void TemptGoal::tick(Mob& mob, const AiContext& context) {
        const PlayerView& player = context.player;
        mob.look_control().look_at(player.eyes, 30.0f, 40.0f);

        if (Vector3Distance(mob.get_position(), player.feet) < TEMPT_STOP_DISTANCE) {
            mob.navigation().stop();
            return;
        }
        // Re-planned as the player moves - and now and then anyway, in case a
        // block placed or broken opened or closed the way.
        if (--replan_ticks <= 0 || Vector3Distance(planned_for, player.feet) > TEMPT_REPLAN_DISTANCE) {
            mob.navigation().move_to(mob, context.world, player.feet, speed);
            planned_for = player.feet;
            replan_ticks = TEMPT_REPLAN_TICKS;
        }
    }

    bool EatGrassGoal::can_use(Mob& mob, const AiContext& context) {
        return mob.random_int(0, EAT_GRASS_CHANCE - 1) == 0 && grass_under(mob, context.world).has_value();
    }

    bool EatGrassGoal::can_continue(Mob&, const AiContext&) {
        return ticks > 0;
    }

    void EatGrassGoal::start(Mob& mob, const AiContext&) {
        ticks = EAT_GRASS_TICKS;
        mob.navigation().stop();
        mob.play_animation(EAT_ANIMATION);
    }

    void EatGrassGoal::stop(Mob& mob)
    {
        mob.stop_animation(); // cut short (panicked mid-bite) - the head comes straight back up
    }

    void EatGrassGoal::tick(Mob& mob, const AiContext& context) {
        // The head goes down in the "eat" animation; holding the look flag
        // meanwhile just keeps other goals from turning it anywhere else.
        --ticks;

        if (ticks != EAT_GRASS_BITE_TICK) return;
        if (std::optional<GrassSpot> spot = grass_under(mob, context.world)) {
            mob.request_block_change({spot->x, spot->y, spot->z, spot->tall ? BlockType::Air : BlockType::Dirt});
            mob.on_ate_grass();
        }
    }

    void add_animal_goals(GoalSelector& goals, const AnimalAiSettings& settings) {
        StrollSettings stroll;
        stroll.speed = settings.stroll_speed;
        stroll.avoid_water = true;
        stroll.prefer_grass = true;

        goals.add(0, std::make_unique<FloatGoal>());
        goals.add(1, std::make_unique<PanicGoal>(settings.panic_speed));
        if (!settings.tempt_items.empty()) goals.add(3, std::make_unique<TemptGoal>(settings.tempt_speed, settings.tempt_items));
        if (settings.eats_grass) goals.add(5, std::make_unique<EatGrassGoal>());
        goals.add(5, std::make_unique<RandomStrollGoal>(stroll));
        goals.add(6, std::make_unique<LookAtPlayerGoal>(settings.look_range, 0.02f));
        goals.add(7, std::make_unique<RandomLookAroundGoal>());
    }

} // namespace ai
