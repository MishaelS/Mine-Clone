#pragma once

#include "entities/Mob.hpp"

// A person living around one spot - its home, where it was summoned or
// spawned (see ai/NpcAI.hpp): strolls about near it, stops to face a player
// who walks up. Drawn with the player's own model and skin for now.
class Npc : public Mob {
public:
    static constexpr const char* TYPE_ID = "npc";
    static constexpr float WIDTH = 0.6f; // same size as the player
    static constexpr float HEIGHT = 1.8f;
    static constexpr float EYE_HEIGHT = 1.62f;

    Npc(Vector3 feet_position, float yaw_degrees, uint32_t seed);

    const char* type_id() const override { return TYPE_ID; }
    const EntityModel& model() const override;
    const ai::PathSettings& path_settings() const override;
    float walk_speed() const override;

protected:
    float model_scale() const override;
};
