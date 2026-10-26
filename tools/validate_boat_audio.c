#include "boat_audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000
#define SECONDS 12

static bool validate_water(void)
{
    BoatEngineVoice quiet = BOAT_ENGINE_DEFAULT;
    quiet.exhaust = quiet.bass = quiet.propeller = 0;
    BoatAudio audio;
    boat_audio_reset(&audio, quiet);
    float samples[RATE];
    double energy[3] = { 0 };
    // Rest, coasting on flat water, then repeated bow entry. No engine contribution.
    for (int stage = 0; stage < 3; ++stage) {
        BoatAudioInput input = { .speed = stage ? 10 : 0, .wave_entry = stage == 2 ? .8f : 0 };
        boat_audio_render(&audio, input, samples, RATE);
        boat_audio_render(&audio, input, samples, RATE);
        for (int i = 0; i < RATE; ++i) {
            if (!isfinite(samples[i]) || fabsf(samples[i]) >= .99f)
                return false;
            energy[stage] += samples[i] * samples[i];
        }
    }
    printf("water RMS: rest %.4f, coast %.4f, wave entry %.4f\n", sqrt(energy[0] / RATE),
        sqrt(energy[1] / RATE), sqrt(energy[2] / RATE));
    audio.active_target = 0;
    boat_audio_render(&audio, (BoatAudioInput) { .speed = 10 }, samples, RATE);
    return energy[0] == 0 && energy[1] > .001 && energy[2] > 2 * energy[1]
        && fabsf(samples[RATE - 1]) < .00001f;
}

int main(int argc, char **argv)
{
    size_t count = RATE * SECONDS;
    float *samples = malloc(count * sizeof(*samples));
    if (!samples)
        return 1;
    BoatAudio audio;
    boat_audio_reset(&audio, BOAT_ENGINE_DEFAULT);
    double idle_energy = 0, loaded_energy = 0, mean = 0;
    float peak = 0;
    for (int second = 0; second < SECONDS; ++second) {
        float thrust = second < 3 ? 0 : second < 7 ? 1 : second < 10 ? -1 : 0;
        boat_audio_render(
            &audio, (BoatAudioInput) { .thrust = thrust }, samples + second * RATE, RATE);
        for (int i = 0; i < RATE; ++i) {
            float value = samples[second * RATE + i];
            if (!isfinite(value) || fabsf(value) >= .99f) {
                fprintf(stderr, "Non-finite or clipping sample at second %d\n", second);
                free(samples);
                return 1;
            }
            peak = fmaxf(peak, fabsf(value));
            mean += value;
            if (second == 2)
                idle_energy += value * value;
            if (second == 6)
                loaded_energy += value * value;
        }
    }
    bool valid = fabs(mean / (double)count) < .01 && idle_energy > .001
        && loaded_energy > idle_energy * 1.2;
    // Identical synthesis across stream block boundaries, including a higher-revving voice.
    BoatEngineVoice fast = BOAT_ENGINE_DEFAULT;
    fast.maximum_rpm = 8500;
    BoatAudio whole, split;
    boat_audio_reset(&whole, fast);
    boat_audio_reset(&split, fast);
    float a[4096], b[4096];
    for (int block = 0; block < 100; ++block) {
        boat_audio_render(&whole, (BoatAudioInput) { 1, 10, .5f, .7f, 0, 0, 0, 0 }, a, 4096);
        boat_audio_render(&split, (BoatAudioInput) { 1, 10, .5f, .7f, 0, 0, 0, 0 }, b, 137);
        boat_audio_render(
            &split, (BoatAudioInput) { 1, 10, .5f, .7f, 0, 0, 0, 0 }, b + 137, 4096 - 137);
        valid = valid && memcmp(a, b, sizeof(a)) == 0;
        for (int i = 0; i < 4096; ++i)
            valid = valid && isfinite(a[i]) && fabsf(a[i]) < .99f;
    }
    valid = validate_water() && valid;
    if (argc > 1) {
        boat_audio_reset(&audio, BOAT_ENGINE_DEFAULT);
        for (size_t offset = 0; offset < count; offset += 960) {
            float time = (float)offset / RATE;
            float encounter = time >= 5 && time < 10
                ? .65f * fmaxf(0, sinf((time - 5) * 6.2831853f / 1.6f))
                : 0;
            BoatAudioInput input = { .thrust = time < 2 ? 0
                    : time < 8                          ? .45f
                                                        : 0,
                .speed = time < 2 ? 0
                    : time < 5    ? 6
                    : time < 10   ? 10
                                  : 3,
                .wave_entry = encounter };
            boat_audio_render(&audio, input, samples + offset, 960);
        }
        Wave wave = { .frameCount = (unsigned int)count,
            .sampleRate = RATE,
            .sampleSize = 32,
            .channels = 1,
            .data = samples };
        valid = ExportWave(wave, argv[1]) && valid;
    }
    printf("engine audio: peak %.3f, mean %.6f, idle RMS %.3f, thrust RMS %.3f\n", peak,
        mean / (double)count, sqrt(idle_energy / RATE), sqrt(loaded_energy / RATE));
    free(samples);
    return valid ? 0 : 1;
}
