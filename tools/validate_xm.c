#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xm_audio.h"

#define JAR_XM_IMPLEMENTATION
#include "jar_xm.h"

// Reproduce raylib's LoadMusicStream lifecycle: its length scan mutates the decoder,
// then its partial reset leaves pending pattern jumps and channel/effect state behind.
static bool check_loaded_start(const char *path)
{
    jar_xm_context_t *xm = NULL;
    if (jar_xm_create_context_from_file(&xm, 48000, path) != 0)
        return false;
    jar_xm_get_remaining_samples(xm);
    jar_xm_reset(xm);
    Music music = { .stream.sampleRate = 48000, .ctxData = xm };
    if (!xm_music_start_at(&music, path, 0)) {
        jar_xm_free_context(xm);
        return false;
    }
    xm = music.ctxData;
    float frames[48000 * 2];
    double late_energy = 0;
    uint8_t order = 0;
    for (int second = 0; second < 5; ++second) {
        jar_xm_generate_samples(xm, frames, 48000);
        if (second >= 2)
            for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i)
                late_energy += (double)frames[i] * frames[i];
    }
    jar_xm_get_position(xm, &order, NULL, NULL, NULL);
    jar_xm_free_context(xm);
    if (order != 0 || late_energy < .001) {
        fprintf(stderr, "Loaded music jumped away from intro or went silent: order=%u energy=%g\n",
            order, late_energy);
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    bool background = argc == 3 && strcmp(argv[2], "--background") == 0;
    bool acid = argc == 3 && strcmp(argv[2], "--acid") == 0;
    bool cozy = argc == 3 && strcmp(argv[2], "--cozy") == 0;
    bool looping = background || acid || cozy;
    if (argc != 2 && !looping) {
        fprintf(stderr, "usage: %s module.xm [--background|--acid|--cozy]\n", argv[0]);
        return 2;
    }
    if (looping && !check_loaded_start(argv[1]))
        return 1;
    jar_xm_context_t *xm = NULL;
    int result = jar_xm_create_context_from_file(&xm, 48000, argv[1]);
    if (result != 0) {
        fprintf(stderr, "invalid XM (jar_xm error %d)\n", result);
        return 1;
    }
    printf("%s: %u channels, %u patterns, %u instruments\n", jar_xm_get_module_name(xm),
        jar_xm_get_number_of_channels(xm), jar_xm_get_number_of_patterns(xm),
        jar_xm_get_number_of_instruments(xm));
    const float seek_time = 48.58f;
    Music music = {
        .stream.sampleRate = 48000,
        .ctxData = xm,
    };
    if (!xm_music_start_at(&music, argv[1], seek_time)) {
        fprintf(stderr, "unable to seek XM decoder\n");
        jar_xm_free_context(xm);
        return 1;
    }
    xm = music.ctxData;
    uint8_t pattern_index = 0;
    uint64_t generated_frames = 0;
    jar_xm_get_position(xm, &pattern_index, NULL, NULL, &generated_frames);
    uint64_t expected_frames = (uint64_t)((double)seek_time * music.stream.sampleRate);
    if (pattern_index != (cozy ? 3 : background ? 4 : 6) || generated_frames != expected_frames) {
        fprintf(stderr,
            "XM seek mismatch: pattern %u, frames %llu (expected timed pattern, frames %llu)\n",
            pattern_index, (unsigned long long)generated_frames,
            (unsigned long long)expected_frames);
        jar_xm_free_context(xm);
        return 1;
    }
    printf("seek %.2f seconds: pattern %u, frame %llu\n", seek_time, pattern_index,
        (unsigned long long)generated_frames);

    // Seek backward on the same stream before rendering the complete song.
    if (!xm_music_start_at(&music, argv[1], 0)) {
        jar_xm_free_context(xm);
        return 1;
    }
    xm = music.ctxData;
    float frames[48000 * 2];
    double energy = 0, loop_energy = 0;
    float peak = 0;
    for (int second = 0; second < (cozy ? 205 : acid ? 235 : background ? 185 : 1); ++second) {
        jar_xm_generate_samples(xm, frames, 48000);
        for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i) {
            if (!isfinite(frames[i]) || fabsf(frames[i]) > 1) {
                fprintf(stderr, "Nonfinite or clipped audio at second %d\n", second);
                jar_xm_free_context(xm);
                return 1;
            }
            energy += (double)frames[i] * frames[i];
            if (looping && second >= (cozy ? 102 : acid ? 117 : 92))
                loop_energy += (double)frames[i] * frames[i];
            peak = fmaxf(peak, fabsf(frames[i]));
        }
    }
    if (looping && jar_xm_get_loop_count(xm) != 2) {
        fprintf(stderr, "Background music did not complete two loops\n");
        jar_xm_free_context(xm);
        return 1;
    }
    if (energy <= 0 || (looping && loop_energy <= .001)) {
        fprintf(stderr, "Empty audio\n");
        jar_xm_free_context(xm);
        return 1;
    }
    printf("rendered_audio_peak=%.6f\n", peak);
    jar_xm_free_context(xm);
    return 0;
}
