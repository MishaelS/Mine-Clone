#include "ui/LoadingScreen.hpp"
#include "ui/Widgets.hpp"

#include <algorithm>
#include <cmath>

namespace {
    constexpr float BAR_WIDTH = 400.0f;   // unscaled pixels (ui::scaled())
    constexpr float BAR_HEIGHT = 12.0f;
    constexpr float BAR_BORDER = 2.0f;
    constexpr float FILL_EASE = 3.0f;     // per second - how fast the bar catches up to real progress
    constexpr float MAX_FILL_SPEED = 0.8f; // whole bars per second - a full bar takes at least ~1.25s to fill

    constexpr Color BAR_BORDER_COLOR = {0, 0, 0, 255};
    constexpr Color BAR_EMPTY_COLOR = {40, 40, 40, 255};
    constexpr Color FILL_DARK = {60, 170, 60, 255};
    constexpr Color FILL_LIGHT = {128, 255, 128, 255}; // vanilla's own loading-bar green

    // Sparks: a steady trickle so a stalled bar still looks alive, plus a
    // burst proportional to how fast it's currently filling.
    constexpr float IDLE_SPARKS_PER_SECOND = 30.0f;
    constexpr float SPARKS_PER_BAR_PER_SECOND = 900.0f; // at a fill speed of one whole bar per second
    constexpr size_t MAX_SPARKS = 500;
    constexpr float SPARK_GRAVITY = 420.0f;            // unscaled pixels / s^2

    Color spark_color(int roll)
    {
        switch (roll) {
            case 0:  return {255, 255, 255, 255};
            case 1:  return {220, 255, 140, 255};
            default: return {140, 255, 140, 255};
        }
    }
}

void LoadingScreen::reset()
{
    sparks.clear();
    target_progress = 0.0f;
    shown_progress = 0.0f;
    spawn_budget = 0.0f;
    last_time = -1.0;
}

bool LoadingScreen::finished() const
{
    return target_progress >= 1.0f && shown_progress >= 1.0f;
}

void LoadingScreen::update_sparks(float dt, Rectangle fill, float fill_speed)
{
    const float scale = ui::scaled(1.0f);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Spawn from the fill's leading edge (only once there's any fill).
    if (fill.width > 0.0f && shown_progress < 0.999f) {
        spawn_budget += dt * (IDLE_SPARKS_PER_SECOND + SPARKS_PER_BAR_PER_SECOND * fill_speed);
        while (spawn_budget >= 1.0f && sparks.size() < MAX_SPARKS) {
            spawn_budget -= 1.0f;
            Spark spark;
            spark.position = {fill.x + fill.width, fill.y + unit(rng) * fill.height};
            // Mostly backward (trailing the fill) and upward, some forward.
            spark.velocity = {(-140.0f + unit(rng) * 200.0f) * scale, (-60.0f - unit(rng) * 170.0f) * scale};
            spark.max_life = spark.life = 0.35f + unit(rng) * 0.6f;
            spark.size = std::max(1.0f, std::round((1.0f + unit(rng) * 2.0f) * scale));
            spark.color = spark_color(static_cast<int>(unit(rng) * 3.0f));
            sparks.push_back(spark);
        }
        spawn_budget = std::min(spawn_budget, 1.0f);
    }

    for (Spark& spark : sparks) {
        spark.velocity.y += SPARK_GRAVITY * scale * dt;
        spark.position.x += spark.velocity.x * dt;
        spark.position.y += spark.velocity.y * dt;
        spark.life -= dt;
    }
    sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& s) { return s.life <= 0.0f; }),
                 sparks.end());
}

void LoadingScreen::draw(const std::string& title, const std::string& stage, float progress)
{
    const double now = GetTime();
    const float dt = last_time < 0.0 ? 0.0f : static_cast<float>(std::min(now - last_time, 0.1));
    last_time = now;

    // Never backwards (a second spawn-search pass restarts its own
    // progress at 0), eased so a sudden jump still fills smoothly.
    target_progress = std::max(target_progress, std::clamp(progress, 0.0f, 1.0f));
    const float before = shown_progress;
    const float step = (target_progress - shown_progress) * std::min(1.0f, dt * FILL_EASE);
    shown_progress += std::min(step, MAX_FILL_SPEED * dt);
    if (target_progress - shown_progress < 0.001f) shown_progress = target_progress;
    const float fill_speed = dt > 0.0f ? (shown_progress - before) / dt : 0.0f;

    ui::menu_background();

    const float screen_w = static_cast<float>(GetScreenWidth());
    const float screen_h = static_cast<float>(GetScreenHeight());
    const float bar_w = std::min(ui::scaled(BAR_WIDTH), screen_w * 0.7f);
    const float bar_h = ui::scaled(BAR_HEIGHT);
    const float border = std::max(1.0f, std::round(ui::scaled(BAR_BORDER)));
    const Rectangle bar = {std::round((screen_w - bar_w) * 0.5f), std::round(screen_h * 0.5f), std::round(bar_w), std::round(bar_h)};
    const Rectangle inner = {bar.x + border, bar.y + border, bar.width - border * 2.0f, bar.height - border * 2.0f};
    const Rectangle fill = {inner.x, inner.y, std::round(inner.width * shown_progress), inner.height};

    const float line_h = ui::scaled(28.0f);
    ui::label({0.0f, bar.y - line_h * 2.0f, screen_w, line_h}, title);

    DrawRectangleRec(bar, BAR_BORDER_COLOR);
    DrawRectangleRec(inner, BAR_EMPTY_COLOR);
    if (fill.width > 0.0f) {
        DrawRectangleGradientH(static_cast<int>(fill.x), static_cast<int>(fill.y),
                               static_cast<int>(fill.width), static_cast<int>(fill.height), FILL_DARK, FILL_LIGHT);
        // A soft highlight band sweeping along the fill.
        const float band_w = std::max(4.0f, inner.width * 0.12f);
        const float sweep = std::fmod(static_cast<float>(now) * inner.width * 0.6f, fill.width + band_w) - band_w;
        const float band_x = std::max(fill.x, fill.x + sweep);
        const float band_end = std::min(fill.x + fill.width, fill.x + sweep + band_w);
        if (band_end > band_x) {
            DrawRectangle(static_cast<int>(band_x), static_cast<int>(fill.y),
                          static_cast<int>(band_end - band_x), static_cast<int>(fill.height), Color{255, 255, 255, 60});
        }
        // Top-edge shine.
        DrawRectangle(static_cast<int>(fill.x), static_cast<int>(fill.y), static_cast<int>(fill.width),
                      std::max(1, static_cast<int>(fill.height / 4.0f)), Color{255, 255, 255, 50});
    }

    update_sparks(dt, fill, fill_speed);

    BeginBlendMode(BLEND_ADDITIVE);
    if (fill.width > 0.0f && shown_progress < 0.999f) {
        // Glow where the sparks come from.
        const float pulse = 0.75f + 0.25f * std::sin(static_cast<float>(now) * 12.0f);
        DrawCircleGradient({fill.x + fill.width, fill.y + fill.height * 0.5f},
                           fill.height * 1.6f * pulse, Color{160, 255, 160, 170}, Color{0, 0, 0, 0});
    }
    for (const Spark& spark : sparks) {
        Color color = spark.color;
        color.a = static_cast<unsigned char>(255.0f * std::clamp(spark.life / spark.max_life, 0.0f, 1.0f));
        DrawRectangle(static_cast<int>(spark.position.x), static_cast<int>(spark.position.y),
                      static_cast<int>(spark.size), static_cast<int>(spark.size), color);
    }
    EndBlendMode();

    const int percent = static_cast<int>(std::round(shown_progress * 100.0f));
    ui::label({0.0f, bar.y + bar.height + ui::scaled(10.0f), screen_w, line_h},
              stage + "  " + std::to_string(percent) + "%", LIGHTGRAY);
}
