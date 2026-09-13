#pragma once

#include "core/Block.hpp"
#include "core/Settings.hpp"
#include "raylib.h"

#include <array>
#include <random>
#include <string>
#include <vector>

// Data-driven audio service. Block definitions select a material group;
// sounds.json supplies any number of step/hit/break variants for it.
class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    void initialize();
    void shutdown();
    void update(float delta_time, const Settings& settings, bool world_active);

    void play_step(BlockType block, Vector3 source, Vector3 listener);
    void play_hit(BlockType block, Vector3 source, Vector3 listener);
    void play_break(BlockType block, Vector3 source, Vector3 listener);
    void play_ui_click();
    void play_ui_hover();
    void play_item_pickup();
    void update_water(float delta_time, bool in_water, bool moving);

private:
    struct BlockSounds {
        std::vector<Sound> step;
        std::vector<Sound> hit;
        std::vector<Sound> breaking;
    };

    static constexpr size_t GROUP_COUNT = static_cast<size_t>(BlockSoundGroup::Metal) + 1;
    std::array<BlockSounds, GROUP_COUNT> block_sounds;
    std::vector<Sound> ambient_sounds;
    std::vector<Sound> ui_click_sounds;
    std::vector<Sound> ui_hover_sounds;
    std::vector<Sound> item_pickup_sounds;
    std::vector<Sound> swim_sounds;
    std::vector<Sound> water_ambient_sounds;
    std::vector<Music> music_tracks;
    std::mt19937 random{std::random_device{}()};

    bool initialized = false;
    int master_volume = 100;
    int effects_volume = 100;
    int ambient_volume = 70;
    int music_volume = 60;
    float ambient_wait = 8.0f;
    float music_wait = 2.0f;
    int current_music = -1;
    float swim_wait = 0.0f;
    float water_ambient_wait = 1.0f;

    void load_config(const std::string& path);
    void play_spatial(std::vector<Sound>& variants, Vector3 source, Vector3 listener, float base_volume);
    void play_nonspatial(std::vector<Sound>& variants, float volume, float pitch_min = 0.95f, float pitch_max = 1.05f);
};
