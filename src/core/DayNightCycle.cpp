#include "core/DayNightCycle.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace DayNightCycle {

    float time_of_day(uint64_t game_tick) {
        return static_cast<float>(game_tick % DAY_LENGTH_TICKS) / static_cast<float>(DAY_LENGTH_TICKS);
    }

    float celestial_angle(uint64_t game_tick) {
        float angle = time_of_day(game_tick) - 0.25f;
        if (angle < 0.0f) angle += 1.0f;
        return angle;
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
        return MIN_NIGHT_SKY_LIGHT_FACTOR + (1.0f - MIN_NIGHT_SKY_LIGHT_FACTOR) * daylight(game_tick);
    }

    float daylight(uint64_t game_tick) {
        const uint64_t tick = game_tick % DAY_LENGTH_TICKS;
        // 0..1 progress through a transition, eased so it starts and ends gently.
        auto ease = [](uint64_t elapsed, uint64_t length) {
            float t = std::clamp(static_cast<float>(elapsed) / static_cast<float>(length), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };

        float value;
        if (tick >= DAY_START_TICK && tick < DUSK_START_TICK) {
            value = 1.0f;
        } else if (tick >= DUSK_START_TICK && tick < NIGHT_START_TICK) {
            value = 1.0f - ease(tick - DUSK_START_TICK, NIGHT_START_TICK - DUSK_START_TICK);
        } else if (tick >= NIGHT_START_TICK && tick < DAWN_START_TICK) {
            value = 0.0f;
        } else {
            // Dawn runs from DAWN_START_TICK across midnight's wrap (tick
            // 24000 == 0) up to DAY_START_TICK.
            const uint64_t since_dawn = tick >= DAWN_START_TICK ? tick - DAWN_START_TICK
                                                                 : tick + DAY_LENGTH_TICKS - DAWN_START_TICK;
            value = ease(since_dawn, DAY_LENGTH_TICKS - DAWN_START_TICK + DAY_START_TICK);
        }
        return value;
    }

}
