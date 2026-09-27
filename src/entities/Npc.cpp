#include "entities/Npc.hpp"
#include "entities/ai/NpcAI.hpp"
#include "model/ModelLibrary.hpp"

namespace {
    constexpr const char* MODEL_NAME = "player";
    constexpr float MODEL_PIXEL = 1.8f / 32.0f; // 32-pixel-tall model to 1.8 blocks, as PlayerRenderer draws it
    constexpr float WALK_SPEED = 0.1f;          // blocks/tick at speed modifier 1
}

Npc::Npc(Vector3 feet_position, float yaw_degrees, uint32_t seed)
    : Mob(feet_position, yaw_degrees, seed, {WIDTH, HEIGHT, EYE_HEIGHT})
{
    ai::NpcAiSettings settings;
    settings.home = feet_position;
    ai::add_npc_goals(goals, settings);
}

const EntityModel& Npc::model() const
{
    return entity_model(MODEL_NAME);
}

const ai::PathSettings& Npc::path_settings() const
{
    static const ai::PathSettings settings{/*height_cells*/ 2, /*max_drop*/ 3, /*avoid_water*/ true};
    return settings;
}

float Npc::walk_speed() const
{
    return WALK_SPEED;
}

float Npc::model_scale() const
{
    return MODEL_PIXEL;
}
