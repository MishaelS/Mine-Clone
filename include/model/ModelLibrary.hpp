#pragma once

#include "model/EntityModel.hpp"

#include <string>

// Every entity model the game draws, loaded once from
// assets/models/<name>.json (made in the model editor) and kept for the
// rest of the session. A missing or broken file gives an empty model -
// nothing is drawn - plus a warning in the log, rather than a crash.
const EntityModel& entity_model(const std::string& name);
