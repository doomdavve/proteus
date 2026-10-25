#include "boat_audio.h"

#include <math.h>

#define ENGINE_RATE 48000
#define ENGINE_BUFFER 2048

static float random_state(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return (float)(*state >> 8) / 8388608.0f - 1;
}

void boat_audio_reset(BoatAudio *audio, BoatEngineVoice voice)
{
    if (!isfinite(voice.idle_rpm))
        voice.idle_rpm = 750;
    voice.idle_rpm = fmaxf(300, fminf(voice.idle_rpm, 3000));
    if (!isfinite(voice.maximum_rpm))
        voice.maximum_rpm = 4800;
    voice.maximum_rpm = fmaxf(voice.idle_rpm, fminf(voice.maximum_rpm, 12000));
    if (!isfinite(voice.firing_per_revolution))
        voice.firing_per_revolution = 3;
    voice.firing_per_revolution = fmaxf(.5f, fminf(voice.firing_per_revolution, 6));
    if (!isfinite(voice.exhaust) || voice.exhaust < 0)
        voice.exhaust = 0;
    if (!isfinite(voice.propeller) || voice.propeller < 0)
        voice.propeller = 0;
    if (!isfinite(voice.bass) || voice.bass < 0)
        voice.bass = 0;
    voice.exhaust = fminf(voice.exhaust, 1);
    voice.propeller = fminf(voice.propeller, 1);
    voice.bass = fminf(voice.bass, 1);
    *audio = (BoatAudio) { .voice = voice,
        .random = 0x71ab39u,
        .firing_jitter = 1,
        .water_random = 0x38ac17u,
        .splash_random = 0x5d2f61u,
        .active_gain = 1,
        .active_target = 1 };
}

void boat_audio_init(BoatAudio *audio, BoatEngineVoice voice)
{
    boat_audio_reset(audio, voice);
    InitAudioDevice();
    if (!IsAudioDeviceReady())
        return;
    SetAudioStreamBufferSizeDefault(ENGINE_BUFFER);
    audio->stream = LoadAudioStream(ENGINE_RATE, 32, 1);
    audio->ready = IsAudioStreamValid(audio->stream);
    if (audio->ready)
        PlayAudioStream(audio->stream);
}

static float bounded(float value, float maximum)
{
    return isfinite(value) ? fmaxf(0, fminf(maximum, value)) : 0;
}

void boat_audio_render(BoatAudio *audio, BoatAudioInput input, float *samples, size_t count)
{
    float target = bounded(fabsf(input.thrust), 1);
    float travel = bounded((input.speed - .3f) / 12, 1);
    float planing = bounded(input.planing, 1);
    float entry = bounded(input.wave_entry, 1);
    static const float cycle[] = { .08f, -.05f, .03f, -.07f, .05f, -.04f };
    if (input.landing_serial != audio->landing_serial) {
        audio->landing_serial = input.landing_serial;
        float landing = bounded(input.landing, 1);
        audio->slap = audio->splash = landing;
    }
    for (size_t i = 0; i < count; ++i) {
        audio->load += (target - audio->load) * .00012f;
        float load = audio->load;
        float rpm = audio->voice.idle_rpm
            + (audio->voice.maximum_rpm - audio->voice.idle_rpm) * sqrtf(load);
        audio->phase
            += rpm * audio->voice.firing_per_revolution * audio->firing_jitter / (60 * ENGINE_RATE);
        if (audio->phase >= 1) {
            audio->phase -= 1; // Keep overshoot and state across stream buffers.
            float amplitude = 1 + cycle[audio->cylinder++ % 6]
                + random_state(&audio->random) * (.10f - .06f * load);
            audio->pulse_fast += amplitude;
            audio->pulse_slow += amplitude;
            audio->firing_jitter = 1 + random_state(&audio->random) * (.012f - .008f * load);
        }
        // Fixed 48 kHz coefficients: 0.35 ms attack and 2.5 ms decay.
        audio->pulse_fast *= .9422131f;
        audio->pulse_slow *= .9917013f;
        float pulse = audio->pulse_slow - audio->pulse_fast;
        float noise = random_state(&audio->random);
        // Roughly 100–2200 Hz turbulence, shaped by each combustion event.
        audio->noise_low += .25f * (noise - audio->noise_low);
        audio->noise_body += .013f * (audio->noise_low - audio->noise_body);
        float turbulence = (audio->noise_low - audio->noise_body) * (.12f + .65f * pulse);
        float source = pulse + .08f * turbulence;
        // Subdued acoustic voice: dark exhaust and body, with no bright combustion hiss.
        audio->exhaust_low += (.031f + .010f * load) * (source - audio->exhaust_low);
        audio->exhaust_body += .041f * (audio->exhaust_low - audio->exhaust_body);
        audio->wash += .08f * (noise - audio->wash);
        float engine = .65f * audio->voice.exhaust * (.25f + .55f * load) * audio->exhaust_body
            + .45f * audio->voice.bass * (.25f + .55f * load) * audio->exhaust_body
            + .2f * audio->voice.propeller * load * audio->wash;
        audio->water_speed += .000139f * (travel - audio->water_speed);
        audio->water_planing += .000139f * (planing - audio->water_planing);
        float entry_rate = entry > audio->wave_entry ? .000347f : .0000694f;
        audio->wave_entry += entry_rate * (entry - audio->wave_entry);
        float water_noise = random_state(&audio->water_random);
        audio->water_modulation += .00007f * (water_noise - audio->water_modulation);
        audio->water_low
            += (.10f + .015f * audio->water_planing) * (water_noise - audio->water_low);
        audio->water_splash += .111f * (audio->water_low - audio->water_splash);
        audio->water_body += .0117f * (audio->water_splash - audio->water_body);
        float swell = fmaxf(.8f, fminf(1.2f, 1 + 8 * audio->water_modulation));
        float water = (audio->water_splash - audio->water_body) * swell * audio->water_speed
            * (.32f + .55f * audio->wave_entry);
        // Landing: a dull 90 ms hull slap (noise, roughly 80-450 Hz, no tone) under a
        // 350 ms splash wash.
        float splash_noise = random_state(&audio->splash_random);
        audio->slap_low += .06f * (splash_noise - audio->slap_low);
        audio->slap_body += .011f * (audio->slap_low - audio->slap_body);
        float slap = audio->slap * (audio->slap_low - audio->slap_body);
        audio->slap *= .99977f;
        audio->splash_low += .2f * (splash_noise - audio->splash_low);
        audio->splash_body += .02f * (audio->splash_low - audio->splash_body);
        float splash = audio->splash * (audio->splash_low - audio->splash_body);
        audio->splash *= .99994f;
        float value = engine + water + .9f * slap + .5f * splash;
        // DC removal (~25 Hz), then soft limiting with ample stream headroom.
        float centered = value - audio->dc_input + .99673f * audio->dc_output;
        audio->dc_input = value;
        audio->dc_output = centered;
        float fade = audio->active_target > audio->active_gain ? .000521f : .000208f;
        audio->active_gain += fade * (audio->active_target - audio->active_gain);
        samples[i] = audio->active_gain * .75f * centered / (1 + fabsf(centered));
    }
}

void boat_audio_update(BoatAudio *audio, BoatAudioInput input, bool active)
{
    if (!audio->ready)
        return;
    audio->active_target = active ? 1 : 0;
    while (IsAudioStreamProcessed(audio->stream)) {
        float samples[ENGINE_BUFFER];
        boat_audio_render(audio, input, samples, ENGINE_BUFFER);
        UpdateAudioStream(audio->stream, samples, ENGINE_BUFFER);
    }
}

void boat_audio_destroy(BoatAudio *audio)
{
    if (audio->ready) {
        StopAudioStream(audio->stream);
        UnloadAudioStream(audio->stream);
    }
    if (IsAudioDeviceReady())
        CloseAudioDevice();
    *audio = (BoatAudio) { 0 };
}
