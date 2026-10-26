#ifndef PROTEUS_BOAT_AUDIO_H
#define PROTEUS_BOAT_AUDIO_H

#include "raylib.h"
#include <stddef.h>
#include <stdint.h>

// Sound tuning is independent of physics: future craft can use a different rev range/voice.
typedef struct {
    float idle_rpm, maximum_rpm;
    float firing_per_revolution, exhaust, propeller, bass;
} BoatEngineVoice;

#define BOAT_ENGINE_DEFAULT ((BoatEngineVoice) { 750, 4800, 1, .10f, .025f, .18f })

typedef struct {
    float thrust; // Actual signed propulsion, -1..1.
    float speed; // Hull speed through still water, world units/second.
    float planing, wave_entry; // Live hull state, 0..1; optional for old replays.
    // Last landing strength, 0..1; each new serial starts one hull slap and splash.
    float landing;
    unsigned landing_serial;
    uint64_t gun_tick, grenade_tick; // Event high-water marks, deduplicated across corrections.
} BoatAudioInput;

typedef struct {
    AudioStream stream;
    BoatEngineVoice voice;
    double phase;
    float load, noise_low;
    float pulse_fast, pulse_slow;
    float exhaust_low, exhaust_body, dc_input, dc_output;
    float firing_jitter, noise_body, wash;
    unsigned int cylinder;
    uint32_t random;
    float water_speed, water_planing, wave_entry;
    float water_low, water_body, water_modulation, water_splash;
    uint32_t water_random;
    unsigned landing_serial;
    float slap, slap_low, slap_body, splash, splash_low, splash_body;
    uint32_t splash_random;
    float active_gain, active_target;
    uint64_t gun_tick, grenade_tick;
    float gun_pulse, grenade_pulse, weapon_low;
    bool ready;
} BoatAudio;

// Device-free synthesis, also used by offline verification and WAV previews.
void boat_audio_reset(BoatAudio *audio, BoatEngineVoice voice);
void boat_audio_render(BoatAudio *audio, BoatAudioInput input, float *samples, size_t count);

void boat_audio_init(BoatAudio *audio, BoatEngineVoice voice);
// Actual propulsion and hull motion; inactive gently fades the whole mix.
void boat_audio_update(BoatAudio *audio, BoatAudioInput input, bool active);
void boat_audio_destroy(BoatAudio *audio);

#endif
