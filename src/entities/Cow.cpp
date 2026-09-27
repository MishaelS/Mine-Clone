#include "entities/Cow.hpp"
#include "entities/ai/MobAI.hpp"
#include "model/ModelLibrary.hpp"

namespace {
    constexpr const char* MODEL_NAME = "cow";
    constexpr float WALK_SPEED = 0.06f; // blocks/tick - an unhurried ~1.2 blocks/second
}

Cow::Cow(Vector3 feet_position, float yaw_degrees, uint32_t seed)
    : Mob(feet_position, yaw_degrees, seed, {WIDTH, HEIGHT, EYE_HEIGHT})
{
    ai::AnimalAiSettings settings;
    settings.tempt_items = {ItemType::Wheat};
    ai::add_animal_goals(goals, settings);

    // Milking: right click holding an empty bucket - one of them fills.
    add_interaction(on_use().holding(ItemType::Bucket).gives_in_hand(ItemType::MilkBucket));
}

const EntityModel& Cow::model() const
{
    return entity_model(MODEL_NAME);
}

const ai::PathSettings& Cow::path_settings() const
{
    static const ai::PathSettings settings{/*height_cells*/ 2, /*max_drop*/ 3, /*avoid_water*/ true};
    return settings;
}

float Cow::walk_speed() const
{
    return WALK_SPEED;
}
