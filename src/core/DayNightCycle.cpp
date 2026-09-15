#include "core/DayNightCycle.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace DayNightCycle {

    float time_of_day(uint64_t game_tick) {
        return static_cast<float>(game_tick % DAY_LENGTH_TICKS) / static_cast<float>(DAY_LENGTH_TICKS);
    }

    Vector3 sun_direction(uint64_t game_tick) {
        float angle = time_of_day(game_tick) * 2.0f * PI;
        // Rises due east (+X) at dawn (angle 0), zenith (+Y) at noon (PI/2),
        // sets due west (-X) at dusk (PI), nadir (-Y) at midnight (3*PI/2) -
        // world +Z stays exactly 0 throughout, which is what lets
        // Skybox::draw_celestial_bodies() build each billboard's own "up" axis
        // as a plain cross product with +Z without a degenerate case to guard.
        return {std::cos(angle), std::sin(angle), 0.0f};
    }

    Vector3 moon_direction(uint64_t game_tick) {
        return Vector3Negate(sun_direction(game_tick));
    }

    float sky_light_factor(uint64_t game_tick) {
        float daylight = std::clamp(sun_direction(game_tick).y, 0.0f, 1.0f);
        return MIN_NIGHT_SKY_LIGHT_FACTOR + (1.0f - MIN_NIGHT_SKY_LIGHT_FACTOR) * daylight;
    }

}
