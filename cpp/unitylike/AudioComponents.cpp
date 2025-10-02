#include "Scene.h"
#include <flecs.h>
#include <cstring>

extern "C" {
#include "ame/audio.h"
#include "ame/audio_ray.h"
#include "ame/acoustics.h"
}

namespace unitylike {

// Static member for main AudioListener
AudioListener* AudioListener::main_listener_ = nullptr;

// AudioSource implementation
void AudioSource::Play() {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->source.playing = true;
    asd->is_playing = true;
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

void AudioSource::Stop() {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->source.playing = false;
    asd->is_playing = false;
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

void AudioSource::Pause() {
    Stop(); // For now, pause is same as stop
}

void AudioSource::UnPause() {
    Play(); // For now, unpause is same as play
}

float AudioSource::volume() const {
    if (!owner_.scene() || !owner_.id()) return 1.0f;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioSourceData* asd = (const AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return 1.0f;
    
    return asd->volume;
}

void AudioSource::volume(float v) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->volume = v;
    asd->source.gain = v; // Update the underlying audio source gain
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

float AudioSource::pitch() const {
    if (!owner_.scene() || !owner_.id()) return 1.0f;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioSourceData* asd = (const AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return 1.0f;
    
    return asd->pitch;
}

void AudioSource::pitch(float p) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->pitch = p;
    asd->dirty = 1;
    // Note: Pitch adjustment would need to be implemented in the audio engine
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

bool AudioSource::mute() const {
    if (!owner_.scene() || !owner_.id()) return false;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioSourceData* asd = (const AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return false;
    
    return asd->mute;
}

void AudioSource::mute(bool m) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->mute = m;
    asd->source.playing = asd->is_playing && !m; // Update playing state based on mute
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

bool AudioSource::loop() const {
    if (!owner_.scene() || !owner_.id()) return false;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioSourceData* asd = (const AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return false;
    
    return asd->loop;
}

void AudioSource::loop(bool l) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->loop = l;
    // For OPUS sources, set the loop flag
    if (asd->source.type == AME_AUDIO_SOURCE_OPUS) {
        asd->source.u.pcm.loop = l;
    }
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

bool AudioSource::playOnAwake() const {
    if (!owner_.scene() || !owner_.id()) return false;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioSourceData* asd = (const AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return false;
    
    return asd->play_on_awake;
}

void AudioSource::playOnAwake(bool p) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->play_on_awake = p;
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

bool AudioSource::isPlaying() const {
    if (!owner_.scene() || !owner_.id()) return false;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioSourceData* asd = (const AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return false;
    
    return asd->is_playing && asd->source.playing;
}

bool AudioSource::LoadOpusFile(const char* filepath, bool loop_audio) {
    if (!owner_.scene() || !owner_.id() || !filepath) return false;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return false;
    
    bool success = ame_audio_source_load_opus_file(&asd->source, filepath, loop_audio);
    if (success) {
        asd->loop = loop_audio;
        asd->dirty = 1;
        ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    }
    return success;
}

void AudioSource::InitSigmoidOsc(float freq_hz, float shape_k, float gain) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    ame_audio_source_init_sigmoid(&asd->source, freq_hz, shape_k, gain);
    asd->volume = gain;
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

void AudioSource::InitSawWork(float base_freq_hz, float drive, float noise_mix, float lfo_rate_hz, float gain) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    ame_audio_source_init_saw_work(&asd->source, base_freq_hz, drive, noise_mix, lfo_rate_hz, gain);
    asd->volume = gain;
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

void AudioSource::InitSawCut(float freq_hz, float drive, float noise_mix, float duration_sec, float gain) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    ame_audio_source_init_saw_cut(&asd->source, freq_hz, drive, noise_mix, duration_sec, gain);
    asd->volume = gain;
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

float AudioSource::pan() const {
    if (!owner_.scene() || !owner_.id()) return 0.0f;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioSourceData* asd = (const AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return 0.0f;
    
    return asd->source.pan;
}

void AudioSource::pan(float p) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->source.pan = p;
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

// Spatial audio methods
bool AudioSource::spatialAudio() const {
    if (!owner_.scene() || !owner_.id()) return false;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioSourceData* asd = (const AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return false;
    
    return asd->spatial_audio;
}

void AudioSource::spatialAudio(bool spatial) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->spatial_audio = spatial;
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

float AudioSource::minDistance() const {
    if (!owner_.scene() || !owner_.id()) return 1.0f;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioSourceData* asd = (const AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return 1.0f;
    
    return asd->min_distance;
}

void AudioSource::minDistance(float distance) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->min_distance = distance > 0.0f ? distance : 0.1f;
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

float AudioSource::maxDistance() const {
    if (!owner_.scene() || !owner_.id()) return 500.0f;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioSourceData* asd = (const AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return 500.0f;
    
    return asd->max_distance;
}

void AudioSource::maxDistance(float distance) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->max_distance = distance;
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

float AudioSource::occlusionDb() const {
    if (!owner_.scene() || !owner_.id()) return 6.0f;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioSourceData* asd = (const AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return 6.0f;
    
    return asd->occlusion_db;
}

void AudioSource::occlusionDb(float db) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->occlusion_db = db;
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

float AudioSource::airAbsorption() const {
    if (!owner_.scene() || !owner_.id()) return 0.02f;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioSourceData* asd = (const AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return 0.02f;
    
    return asd->air_absorption_db_per_meter;
}

void AudioSource::airAbsorption(float db_per_meter) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioSourceData* asd = (AudioSourceData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
    if (!asd) return;
    
    asd->air_absorption_db_per_meter = db_per_meter;
    asd->dirty = 1;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_source);
}

// AudioListener implementation
float AudioListener::volume() const {
    if (!owner_.scene() || !owner_.id()) return 1.0f;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioListenerData* ald = (const AudioListenerData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_listener);
    if (!ald) return 1.0f;
    
    return ald->volume;
}

void AudioListener::volume(float v) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioListenerData* ald = (AudioListenerData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_listener);
    if (!ald) return;
    
    ald->volume = v;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_listener);
}

bool AudioListener::mute() const {
    if (!owner_.scene() || !owner_.id()) return false;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    const AudioListenerData* ald = (const AudioListenerData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_listener);
    if (!ald) return false;
    
    return ald->mute;
}

void AudioListener::mute(bool m) {
    if (!owner_.scene() || !owner_.id()) return;
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    
    AudioListenerData* ald = (AudioListenerData*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_listener);
    if (!ald) return;
    
    ald->mute = m;
    ecs_modified_id(w, (ecs_entity_t)owner_.id(), g_comp.audio_listener);
}

AudioListener* AudioListener::main() {
    return main_listener_;
}

void AudioListener::SetMain(AudioListener* listener) {
    main_listener_ = listener;
}

} // namespace unitylike