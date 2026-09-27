#include "entities/Sheep.hpp"
#include "entities/ai/MobAI.hpp"
#include "model/ModelLibrary.hpp"

namespace {
    constexpr const char* MODEL_NAME = "sheep";
    constexpr const char* WOOL_LAYER = "sheep_fur";
    constexpr float WALK_SPEED = 0.06f;     // blocks/tick, same stroll as a cow
    constexpr float SHEAR_BY_HAND = 0.2f;   // chance a bare-handed hit knocks wool off
}

Sheep::Sheep(Vector3 feet_position, float yaw_degrees, uint32_t seed)
    : Mob(feet_position, yaw_degrees, seed, {WIDTH, HEIGHT, EYE_HEIGHT})
{
    ai::AnimalAiSettings settings;
    settings.tempt_items = {ItemType::Wheat};
    settings.eats_grass = true;
    ai::add_animal_goals(goals, settings);

    // Shearing by hand: a punch now and then pulls 1-3 wool off a woolly
    // sheep, leaving it sheared until it eats grass (on_ate_grass()).
    add_interaction(on_hit().with_empty_hand().unless(SHEARED).chance(SHEAR_BY_HAND)
                        .drops(BlockType::WhiteWool, 1, 3).then_set(SHEARED));
}

const EntityModel& Sheep::model() const
{
    return entity_model(MODEL_NAME);
}

const ai::PathSettings& Sheep::path_settings() const
{
    static const ai::PathSettings settings{/*height_cells*/ 2, /*max_drop*/ 3, /*avoid_water*/ true};
    return settings;
}

float Sheep::walk_speed() const
{
    return WALK_SPEED;
}

void Sheep::on_ate_grass()
{
    set_state(SHEARED, false); // the wool grows back
}

bool Sheep::shows_layer(const std::string& layer) const
{
    return layer != WOOL_LAYER || !has_state(SHEARED);
}
