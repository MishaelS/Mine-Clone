#include "audio/AudioSystem.hpp"
#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {
    constexpr const char* CONFIG_PATH = ASSETS_PATH "sounds/audio/sounds.json";

    const char* group_name(BlockSoundGroup group) {
        switch (group) {
            case BlockSoundGroup::Grass  : return "grass";
            case BlockSoundGroup::Dirt   : return "dirt";
            case BlockSoundGroup::Gravel : return "gravel";
            case BlockSoundGroup::Stone  : return "stone";
            case BlockSoundGroup::Wood   : return "wood";
            case BlockSoundGroup::Sand   : return "sand";
            case BlockSoundGroup::Snow   : return "snow";
            case BlockSoundGroup::Glass  : return "glass";
            case BlockSoundGroup::Cloth  : return "cloth";
            case BlockSoundGroup::Foliage: return "foliage";
            case BlockSoundGroup::Metal  : return "metal";
            default:
                return "none";
        }
    }

    std::string read_file(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    std::string asset_path(const std::string& relative) {
        return std::string(ASSETS_PATH) + relative;
    }

    void load_sound_array(const Json& array, std::vector<Sound>& output) {
        if (array.get_type() != Json::Type::Array) return;
        for (const Json& value : array.as_array()) {
            std::string path = asset_path(value.as_string());
            if (!FileExists(path.c_str())) {
                TraceLog(LOG_WARNING, "Audio file is missing: %s", path.c_str());
                continue;
            }
            Sound sound = LoadSound(path.c_str());
            if (IsSoundValid(sound)) output.push_back(sound);
        }
    }

    float random_range(std::mt19937& random, float low, float high) {
        return std::uniform_real_distribution<float>(low, high)(random);
    }
}

AudioSystem::~AudioSystem()
{
    shutdown();
}

void AudioSystem::initialize()
{
    if (initialized) return;
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_WARNING, "Audio device could not be initialized");
        return;
    }
    initialized = true;
    load_config(CONFIG_PATH);
}

void AudioSystem::load_config(const std::string& path)
{
    std::string text = read_file(path);
    if (text.empty()) {
        TraceLog(LOG_WARNING, "Audio config is missing: %s", path.c_str());
        return;
    }

    try {
        Json root = Json::parse(text);
        for (size_t i = 1; i < GROUP_COUNT; ++i) {
            BlockSoundGroup group = static_cast<BlockSoundGroup>(i);
            const Json& entry = root["block_groups"][group_name(group)];
            load_sound_array(entry["step"] , block_sounds[i].step);
            load_sound_array(entry["hit"]  , block_sounds[i].hit);
            load_sound_array(entry["break"], block_sounds[i].breaking);
        }
        load_sound_array(root["ambient"]         , ambient_sounds);
        load_sound_array(root["ui"]["click"]     , ui_click_sounds);
        load_sound_array(root["ui"]["hover"]     , ui_hover_sounds);
        load_sound_array(root["water"]["swim"]   , swim_sounds);
        load_sound_array(root["water"]["ambient"], water_ambient_sounds);
        load_sound_array(root["drop"]["pick up"] , item_pickup_sounds);

        const Json& music = root["music"];
        if (music.get_type() == Json::Type::Array) {
            for (const Json& value : music.as_array()) {
                std::string music_path = asset_path(value.as_string());
                if (!FileExists(music_path.c_str())) {
                    TraceLog(LOG_WARNING, "Music file is missing: %s", music_path.c_str());
                    continue;
                }
                Music track = LoadMusicStream(music_path.c_str());
                if (IsMusicValid(track)) {
                    track.looping = false;
                    music_tracks.push_back(track);
                }
            }
        }
    } catch (const std::exception& error) {
        TraceLog(LOG_WARNING, "Could not parse audio/sounds.json: %s", error.what());
    }
}

void AudioSystem::shutdown()
{
    if (!initialized) return;
    for (auto& group : block_sounds) {
        for (Sound sound : group.step)     UnloadSound(sound);
        for (Sound sound : group.hit)      UnloadSound(sound);
        for (Sound sound : group.breaking) UnloadSound(sound);
        group = {};
    }
    for (Sound sound : ambient_sounds) UnloadSound(sound);
    ambient_sounds.clear();
    for (Sound sound : ui_click_sounds) UnloadSound(sound);
    for (Sound sound : ui_hover_sounds) UnloadSound(sound);
    for (Sound sound : item_pickup_sounds) UnloadSound(sound);
    for (Sound sound : swim_sounds) UnloadSound(sound);
    for (Sound sound : water_ambient_sounds) UnloadSound(sound);
    ui_click_sounds.clear();
    ui_hover_sounds.clear();
    item_pickup_sounds.clear();
    swim_sounds.clear();
    water_ambient_sounds.clear();
    for (Music music : music_tracks) UnloadMusicStream(music);
    music_tracks.clear();
    current_music = -1;
    CloseAudioDevice();
    initialized = false;
}

void AudioSystem::update(float delta_time, const Settings& settings, bool world_active)
{
    if (!initialized) return;
    master_volume  = std::clamp(settings.master_volume, 0, 100);
    effects_volume = std::clamp(settings.effects_volume, 0, 100);
    ambient_volume = std::clamp(settings.ambient_volume, 0, 100);
    music_volume  = std::clamp(settings.music_volume, 0, 100);
    SetMasterVolume(master_volume / 100.0f);

    if (current_music >= 0) {
        Music& track = music_tracks[static_cast<size_t>(current_music)];
        SetMusicVolume(track, music_volume / 100.0f);
        UpdateMusicStream(track);
        if (!IsMusicStreamPlaying(track)) {
            StopMusicStream(track);
            current_music = -1;
            music_wait = random_range(random, 20.0f, 90.0f);
        }
    } else if (!music_tracks.empty() && music_volume > 0) {
        music_wait -= delta_time;
        if (music_wait <= 0.0f) {
            current_music = std::uniform_int_distribution<int>(0, static_cast<int>(music_tracks.size()) - 1)(random);
            SetMusicVolume(music_tracks[static_cast<size_t>(current_music)], music_volume / 100.0f);
            PlayMusicStream(music_tracks[static_cast<size_t>(current_music)]);
        }
    }

    bool ambient_playing = std::any_of(ambient_sounds.begin(), ambient_sounds.end(), IsSoundPlaying);
    if (!world_active && ambient_playing) {
        for (Sound sound : ambient_sounds) StopSound(sound);
        ambient_playing = false;
    }
    if (world_active && !ambient_playing && !ambient_sounds.empty() && ambient_volume > 0) {
        ambient_wait -= delta_time;
        if (ambient_wait <= 0.0f) {
            size_t index = std::uniform_int_distribution<size_t>(0, ambient_sounds.size() - 1)(random);
            SetSoundVolume(ambient_sounds[index], ambient_volume / 100.0f);
            PlaySound(ambient_sounds[index]);
            ambient_wait = random_range(random, 15.0f, 45.0f);
        }
    }
}

void AudioSystem::play_spatial(std::vector<Sound>& variants, Vector3 source, Vector3 listener, float base_volume)
{
    if (!initialized || variants.empty() || effects_volume <= 0) return;
    float dx = source.x - listener.x;
    float dy = source.y - listener.y;
    float dz = source.z - listener.z;
    float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    float attenuation = std::clamp(1.0f - distance / 32.0f, 0.0f, 1.0f);
    if (attenuation <= 0.0f) return;

    size_t index = std::uniform_int_distribution<size_t>(0, variants.size() - 1)(random);
    Sound sound = variants[index];
    SetSoundPitch(sound, random_range(random, 0.9f, 1.1f));
    SetSoundVolume(sound, base_volume * attenuation * effects_volume / 100.0f);
    PlaySound(sound);
}

void AudioSystem::play_step(BlockType block, Vector3 source, Vector3 listener)
{
    size_t group = static_cast<size_t>(get_block_properties(block).sound_group);
    play_spatial(block_sounds[group].step, source, listener, 0.55f);
}

void AudioSystem::play_hit(BlockType block, Vector3 source, Vector3 listener)
{
    size_t group = static_cast<size_t>(get_block_properties(block).sound_group);
    play_spatial(block_sounds[group].hit, source, listener, 0.75f);
}

void AudioSystem::play_break(BlockType block, Vector3 source, Vector3 listener)
{
    size_t group = static_cast<size_t>(get_block_properties(block).sound_group);
    play_spatial(block_sounds[group].breaking, source, listener, 1.0f);
}

void AudioSystem::play_nonspatial(std::vector<Sound>& variants, float volume, float pitch_min, float pitch_max)
{
    if (!initialized || variants.empty() || volume <= 0.0f) return;
    size_t index = std::uniform_int_distribution<size_t>(0, variants.size() - 1)(random);
    Sound sound = variants[index];
    SetSoundPitch(sound, random_range(random, pitch_min, pitch_max));
    SetSoundVolume(sound, volume);
    PlaySound(sound);
}

void AudioSystem::play_ui_click()
{
    play_nonspatial(ui_click_sounds, effects_volume / 100.0f, 0.98f, 1.02f);
}

void AudioSystem::play_ui_hover()
{
    play_nonspatial(ui_hover_sounds, effects_volume / 100.0f * 0.55f, 0.98f, 1.02f);
}

void AudioSystem::play_item_pickup()
{
    if (!initialized || item_pickup_sounds.empty() || effects_volume <= 0) return;

    // Four deliberately small, repeatable pitch variants make a single
    // source sample less mechanical without requiring four duplicate files.
    constexpr float PITCH_VARIANTS[4] = {0.92f, 0.98f, 1.04f, 1.10f};
    const size_t sound_index = std::uniform_int_distribution<size_t>(
        0, item_pickup_sounds.size() - 1)(random);
    const int pitch_index = std::uniform_int_distribution<int>(0, 3)(random);
    Sound sound = item_pickup_sounds[sound_index];
    SetSoundPitch(sound, PITCH_VARIANTS[pitch_index]);
    SetSoundVolume(sound, effects_volume / 100.0f * 0.8f);
    PlaySound(sound);
}

void AudioSystem::update_water(float delta_time, bool in_water, bool moving)
{
    if (!initialized) return;
    if (!in_water) {
        swim_wait = 0.0f;
        water_ambient_wait = 1.0f;
        return;
    }

    swim_wait -= delta_time;
    if (moving && swim_wait <= 0.0f) {
        play_nonspatial(swim_sounds, effects_volume / 100.0f * 0.65f, 0.9f, 1.1f);
        swim_wait = random_range(random, 0.35f, 0.55f);
    }

    water_ambient_wait -= delta_time;
    if (water_ambient_wait <= 0.0f) {
        play_nonspatial(water_ambient_sounds, ambient_volume / 100.0f * 0.55f);
        water_ambient_wait = random_range(random, 4.0f, 9.0f);
    }
}
