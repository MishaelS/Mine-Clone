#include "model/ModelLibrary.hpp"

#include "raylib.h"

#include <unordered_map>

const EntityModel& entity_model(const std::string& name)
{
    static std::unordered_map<std::string, EntityModel> models;
    auto found = models.find(name);
    if (found != models.end()) return found->second;

    std::optional<EntityModel> loaded = load_entity_model(std::string(ASSETS_PATH) + "models/" + name + ".json");
    if (!loaded) TraceLog(LOG_WARNING, "entity model '%s' could not be loaded", name.c_str());
    return models.emplace(name, loaded.value_or(EntityModel{})).first->second;
}
