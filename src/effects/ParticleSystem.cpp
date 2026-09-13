#include "effects/ParticleSystem.hpp"
#include "world/World.hpp"
#include "rendering/EntityLighting.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr std::size_t MAX_PARTICLES = 768;
    constexpr float FRAGMENT_PIXELS = 4.0f;

    BlockFace face_from_normal(Vector3 normal) {
        if (normal.y >  0.5f) return BlockFace::Top;
        if (normal.y < -0.5f) return BlockFace::Bottom;
        if (normal.z < -0.5f) return BlockFace::North;
        if (normal.z >  0.5f) return BlockFace::South;
        if (normal.x >  0.5f) return BlockFace::East;
        return BlockFace::West;
    }
}

float ParticleSystem::random(float minimum, float maximum)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    float unit = static_cast<float>(random_state & 0x00ffffffu) /
                 static_cast<float>(0x01000000u);
    return minimum + (maximum - minimum) * unit;
}

Rectangle ParticleSystem::texture_fragment(BlockType type, BlockFace face)
{
    const Texture2D& atlas = get_block_atlas_texture();
    Rectangle uv = get_block_properties(type).texture_uvs[static_cast<int>(face)];
    Rectangle tile = {uv.x * atlas.width, uv.y * atlas.height,
                      uv.width * atlas.width, uv.height * atlas.height};
    float width = std::min(FRAGMENT_PIXELS, tile.width);
    float height = std::min(FRAGMENT_PIXELS, tile.height);
    float x = tile.x + random(0.0f, std::max(0.0f, tile.width - width));
    float y = tile.y + random(0.0f, std::max(0.0f, tile.height - height));
    // DrawBillboardRec uses pixel coordinates but the sampler can still be
    // bilinear, so keep its rectangle on texel centers as well.
    return {x + 0.5f, y + 0.5f,
            std::max(1.0f, width - 1.0f), std::max(1.0f, height - 1.0f)};
}

void ParticleSystem::add(Particle particle)
{
    if (particles.size() >= MAX_PARTICLES) particles.erase(particles.begin());
    particles.push_back(particle);
}

void ParticleSystem::spawn_hit(BlockType type, Vector3 surface_position, Vector3 surface_normal)
{
    const BlockFace face = face_from_normal(surface_normal);
    const Color tint = get_block_properties(type).texture_tints[static_cast<int>(face)];
    for (int i = 0; i < 6; ++i) {
        Vector3 jitter = {random(-0.18f, 0.18f), random(-0.18f, 0.18f), random(-0.18f, 0.18f)};
        Particle particle;
        particle.position = Vector3Add(surface_position, jitter);
        particle.velocity = Vector3Add(Vector3Scale(surface_normal, random(0.7f, 1.5f)),
                                       Vector3{random(-0.65f, 0.65f), random(0.15f, 1.0f), random(-0.65f, 0.65f)});
        particle.texture_source = texture_fragment(type, face);
        particle.tint = tint;
        particle.lifetime = random(0.35f, 0.65f);
        particle.size = random(0.055f, 0.10f);
        particle.gravity = 7.0f;
        add(particle);
    }
}

void ParticleSystem::spawn_destroy(BlockType type, Vector3 block_center)
{
    const BlockProperties& properties = get_block_properties(type);
    for (int i = 0; i < 24; ++i) {
        BlockFace face = static_cast<BlockFace>(static_cast<int>(random(0.0f, 5.999f)));
        Vector3 offset = {random(-0.42f, 0.42f), random(-0.42f, 0.42f), random(-0.42f, 0.42f)};
        Particle particle;
        particle.position = Vector3Add(block_center, offset);
        particle.velocity = {offset.x * random(1.6f, 3.2f) + random(-0.35f, 0.35f),
                             random(1.1f, 3.2f),
                             offset.z * random(1.6f, 3.2f) + random(-0.35f, 0.35f)};
        particle.texture_source = texture_fragment(type, face);
        particle.tint = properties.texture_tints[static_cast<int>(face)];
        particle.lifetime = random(0.6f, 1.15f);
        particle.size = random(0.07f, 0.14f);
        particle.gravity = 8.5f;
        add(particle);
    }
}

void ParticleSystem::spawn_footstep(BlockType type, Vector3 ground_position)
{
    const Color tint = get_block_properties(type).texture_tints[static_cast<int>(BlockFace::Top)];
    for (int i = 0; i < 3; ++i) {
        Particle particle;
        particle.position = Vector3Add(ground_position,
            Vector3{random(-0.28f, 0.28f), random(0.01f, 0.04f), random(-0.28f, 0.28f)});
        particle.velocity = {random(-0.3f, 0.3f), random(0.2f, 0.55f), random(-0.3f, 0.3f)};
        particle.texture_source = texture_fragment(type, BlockFace::Top);
        particle.tint = tint;
        particle.lifetime = random(0.28f, 0.48f);
        particle.size = random(0.035f, 0.065f);
        particle.gravity = 1.8f;
        add(particle);
    }
}

void ParticleSystem::update(float delta_time, const World* world)
{
    auto solid_at = [world](Vector3 position) {
        return world && get_block_properties(world->get_block(
            static_cast<int>(std::floor(position.x)),
            static_cast<int>(std::floor(position.y)),
            static_cast<int>(std::floor(position.z)))).solid;
    };

    for (Particle& particle : particles) {
        particle.age += delta_time;
        particle.velocity.y -= particle.gravity * delta_time;
        particle.velocity.x *= std::pow(0.35f, delta_time);
        particle.velocity.z *= std::pow(0.35f, delta_time);

        Vector3 next = Vector3Add(particle.position, Vector3Scale(particle.velocity, delta_time));
        // Ground: rest on top of whatever it just fell into instead of
        // sinking through it.
        if (particle.velocity.y <= 0.0f && solid_at({next.x, next.y, next.z})) {
            next.y = std::floor(next.y) + 1.0f;
            particle.velocity.y = 0.0f;
        }
        // Walls: stop dead on whichever horizontal axis actually hit
        // something, checked independently so sliding along one still
        // works right next to a wall on the other axis.
        if (solid_at({next.x, particle.position.y, particle.position.z})) {
            next.x = particle.position.x;
            particle.velocity.x = 0.0f;
        }
        if (solid_at({particle.position.x, particle.position.y, next.z})) {
            next.z = particle.position.z;
            particle.velocity.z = 0.0f;
        }
        particle.position = next;
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(),
        [](const Particle& particle) { return particle.age >= particle.lifetime; }), particles.end());
}

void ParticleSystem::draw(const Camera3D& camera, const World* world) const
{
    const Texture2D& atlas = get_block_atlas_texture();
    for (const Particle& particle : particles) {
        float remaining = 1.0f - particle.age / particle.lifetime;
        Color tint = particle.tint;
        if (world) tint = multiply_tint(tint, entity_environment_tint(*world, particle.position));
        tint.a = static_cast<unsigned char>(static_cast<float>(tint.a) * Clamp(remaining * 1.6f, 0.0f, 1.0f));
        DrawBillboardRec(camera, atlas, particle.texture_source, particle.position,
                         Vector2{particle.size, particle.size}, tint);
    }
}

void ParticleSystem::clear()
{
    particles.clear();
}
