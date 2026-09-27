#include "effects/BlockParticles.hpp"

#include <algorithm>
#include <cmath>

namespace block_particles {

    namespace {
        // Minecraft's particles slow by 4% a tick (20 ticks a second).
        const float DRAG_PER_SECOND = std::pow(0.96f, 20.0f);

        float lifetime_ticks(const Random& random)
        {
            return 8.0f / (random(0.0f, 1.0f) * 0.8f + 0.2f); // Minecraft's: 8..40 ticks, mostly short
        }
    }

    Vector3 emit_point(const BlockParticleEmitter& emitter, Vector3 unit, const BlockStateModel& model,
                       const BlockInstanceState& state, bool directional)
    {
        const Vector3 p = {emitter.at.x + emitter.spread.x * unit.x, emitter.at.y + emitter.spread.y * unit.y,
                           emitter.at.z + emitter.spread.z * unit.z};
        if (state_model_moves(model)) return place_model_point(model, state.attachment, p);
        if (directional) return turn_from_south(p, state.facing);
        return p;
    }

    Particle make(BlockParticleKind kind, Vector3 position, Color color, Rectangle tile, const Random& random)
    {
        Particle particle;
        particle.kind = kind;
        particle.position = position;
        particle.tint = color;
        switch (kind) {
            case BlockParticleKind::Flame:
                // Hangs where it's born, flickering smaller as it burns out.
                particle.velocity = {random(-0.02f, 0.02f), random(0.0f, 0.03f), random(-0.02f, 0.02f)};
                particle.lifetime = (lifetime_ticks(random) + 4.0f) / 20.0f;
                particle.size = random(0.1f, 0.16f);
                break;
            case BlockParticleKind::Smoke: {
                // Dark grey, rising as it shrinks away.
                const float grey = random(0.12f, 0.36f);
                particle.tint = {static_cast<unsigned char>(color.r * grey), static_cast<unsigned char>(color.g * grey),
                                 static_cast<unsigned char>(color.b * grey), color.a};
                particle.velocity = {random(-0.05f, 0.05f), random(0.0f, 0.1f), random(-0.05f, 0.05f)};
                particle.lifetime = lifetime_ticks(random) / 20.0f;
                particle.size = random(0.12f, 0.22f);
                break;
            }
            case BlockParticleKind::Dust: {
                // Its color, a little brighter or darker each time.
                const float shade = random(0.6f, 1.0f);
                particle.tint = {static_cast<unsigned char>(color.r * shade), static_cast<unsigned char>(color.g * shade),
                                 static_cast<unsigned char>(color.b * shade), color.a};
                particle.velocity = {random(-0.04f, 0.04f), random(0.02f, 0.12f), random(-0.04f, 0.04f)};
                particle.lifetime = lifetime_ticks(random) / 20.0f;
                particle.size = random(0.08f, 0.13f);
                break;
            }
            case BlockParticleKind::Leaf: {
                // A 2..3 px bit of its block's tile, fluttering down.
                const float bit = std::floor(random(2.0f, 4.0f));
                particle.atlas_source = {tile.x + std::floor(random(0.0f, tile.width - bit)), tile.y + std::floor(random(0.0f, tile.height - bit)),
                                         bit, bit};
                particle.velocity = {0.0f, -random(0.05f, 0.15f), 0.0f};
                particle.lifetime = random(3.5f, 6.0f);
                particle.size = bit / 16.0f * random(1.1f, 1.5f);
                particle.phase = random(0.0f, 6.2832f);
                break;
            }
        }
        return particle;
    }

    void step(Particle& particle, float dt)
    {
        particle.age += dt;
        const float drag = std::pow(DRAG_PER_SECOND, dt);
        switch (particle.kind) {
            case BlockParticleKind::Flame:
            case BlockParticleKind::Dust:
                particle.velocity = Vector3{particle.velocity.x * drag, particle.velocity.y * drag, particle.velocity.z * drag};
                break;
            case BlockParticleKind::Smoke:
                particle.velocity.y += 1.6f * dt; // Minecraft's 0.004 a tick, per tick
                particle.velocity = Vector3{particle.velocity.x * drag, particle.velocity.y * drag, particle.velocity.z * drag};
                break;
            case BlockParticleKind::Leaf: {
                // Drifts side to side as it sinks, never faster than a feather.
                particle.velocity.y = std::max(particle.velocity.y - 0.8f * dt, -0.45f);
                const float t = particle.age + particle.phase;
                particle.velocity.x = std::cos(t * 2.3f) * 0.3f;
                particle.velocity.z = std::sin(t * 1.7f) * 0.2f;
                break;
            }
        }
        particle.position = Vector3{particle.position.x + particle.velocity.x * dt, particle.position.y + particle.velocity.y * dt,
                                    particle.position.z + particle.velocity.z * dt};
    }

    bool from_sheet(const Particle& particle)
    {
        return particle.kind != BlockParticleKind::Leaf;
    }

    bool glows(const Particle& particle)
    {
        return particle.kind == BlockParticleKind::Flame || particle.kind == BlockParticleKind::Dust;
    }

    bool falls(const Particle& particle)
    {
        return particle.kind == BlockParticleKind::Leaf;
    }

    Rectangle source(const Particle& particle)
    {
        if (particle.kind == BlockParticleKind::Leaf) return particle.atlas_source;
        if (particle.kind == BlockParticleKind::Flame) return {0.0f, 8.0f, 8.0f, 8.0f};
        // Smoke and dust: the dot, from its biggest down to one pixel.
        const float left = 1.0f - std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
        const int frame = std::clamp(static_cast<int>(left * 8.0f), 0, 7);
        return {frame * 8.0f, 0.0f, 8.0f, 8.0f};
    }

    float draw_size(const Particle& particle)
    {
        if (particle.kind != BlockParticleKind::Flame) return particle.size;
        const float t = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
        return particle.size * (1.0f - t * t * 0.5f);
    }

    Color draw_color(const Particle& particle)
    {
        Color color = particle.tint;
        if (particle.kind == BlockParticleKind::Leaf) {
            // Fades out over its last second.
            const float left = particle.lifetime - particle.age;
            color.a = static_cast<unsigned char>(color.a * std::clamp(left, 0.0f, 1.0f));
        }
        return color;
    }

} // namespace block_particles
