#pragma once

#include "items/Inventory.hpp"

#include "raylib.h"

#include <memory>
#include <vector>

class Mob;
class World;

namespace ai {

    // What a mob's AI can see of the player this tick.
    struct PlayerView {
        Vector3 feet = {0.0f, 0.0f, 0.0f};
        Vector3 eyes = {0.0f, 0.0f, 0.0f};
        ItemStack held;       // the selected hotbar slot - what tempts animals
        bool present = false; // false = no player to look at or follow (e.g. dead)
    };

    // Everything a goal reads besides the mob itself.
    struct AiContext {
        const World& world;
        const PlayerView& player;
    };

    // The controls a goal takes over while it runs - two goals sharing one
    // never run at once (Minecraft's Goal.Flag).
    enum GoalFlag : unsigned {
        MOVE_FLAG = 1u << 0, // walking/navigation
        LOOK_FLAG = 1u << 1, // where the head turns
        JUMP_FLAG = 1u << 2, // jumping/swimming up
    };

    // One behaviour - wander, follow a tempting item, look at the player...
    // Minecraft's own Goal: a GoalSelector starts it once can_use() says so and
    // no more important goal holds its flags, ticks it while can_continue(),
    // and stops it when that fails or a more important goal needs its flags.
    class Goal {
    public:
        explicit Goal(unsigned flags) : goal_flags(flags) {}
        virtual ~Goal() = default;

        virtual bool can_use(Mob& mob, const AiContext& context) = 0;
        virtual bool can_continue(Mob& mob, const AiContext& context) { return can_use(mob, context); }
        virtual void start(Mob&, const AiContext&) {}
        virtual void stop(Mob&) {}
        virtual void tick(Mob&, const AiContext&) {}
        // Whether a more important goal may take this one's flags mid-run.
        virtual bool interruptible() const { return true; }

        unsigned flags() const { return goal_flags; }

    private:
        unsigned goal_flags;
    };

    // A mob's goals by priority (lower number = more important), run once per
    // game tick - see Goal.
    class GoalSelector {
    public:
        void add(int priority, std::unique_ptr<Goal> goal);
        void tick(Mob& mob, const AiContext& context);
        void stop_all(Mob& mob);

    private:
        struct Entry {
            int priority;
            std::unique_ptr<Goal> goal;
            bool running = false;
        };
        std::vector<Entry> entries; // kept sorted by priority
    };

} // namespace ai
