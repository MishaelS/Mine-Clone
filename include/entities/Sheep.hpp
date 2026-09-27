#pragma once

#include "entities/Mob.hpp"

// A sheep: an animal like the cow (see ai/MobAI.hpp) - wanders, follows
// wheat, panics when hit - wearing its wool as a separate model layer
// (assets/models/sheep_fur.json, its own Sheep_Fur.png skin). Hitting it
// bare-handed now and then knocks some wool off - shearing it (see its
// interaction rule in the .cpp): the wool layer goes until it eats grass
// again.
class Sheep : public Mob {
public:
    static constexpr const char* TYPE_ID = "sheep";
    static constexpr const char* SHEARED = "sheared"; // Mob state
    static constexpr float WIDTH = 0.9f; // blocks, Minecraft's own sheep size
    static constexpr float HEIGHT = 1.3f;
    static constexpr float EYE_HEIGHT = 1.235f;

    Sheep(Vector3 feet_position, float yaw_degrees, uint32_t seed);

    const char* type_id() const override { return TYPE_ID; }
    const EntityModel& model() const override;
    const ai::PathSettings& path_settings() const override;
    float walk_speed() const override;
    void on_ate_grass() override;

protected:
    bool shows_layer(const std::string& layer) const override;
};
