#pragma once

#include "entities/Mob.hpp"

// A cow: an animal (see ai/MobAI.hpp) - wanders about along planned paths,
// glances at a player close by, follows one holding wheat, and gives milk
// to a right click with an empty bucket. Drawn from
// assets/models/cow.json - its "idle" animation always, "walk" while it
// moves.
class Cow : public Mob {
public:
    static constexpr const char* TYPE_ID = "cow";
    static constexpr float WIDTH = 0.9f; // blocks, Minecraft's own cow size
    static constexpr float HEIGHT = 1.4f;
    static constexpr float EYE_HEIGHT = 1.3f;

    // `seed` makes its wandering its own, not in step with every other cow.
    Cow(Vector3 feet_position, float yaw_degrees, uint32_t seed);

    const char* type_id() const override { return TYPE_ID; }
    const EntityModel& model() const override;
    const ai::PathSettings& path_settings() const override;
    float walk_speed() const override;
};
