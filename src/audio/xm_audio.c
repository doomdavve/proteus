#include "xm_audio.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

// raylib 6.0 exposes Music.ctxData but does not expose the bundled jar_xm API.
// These declarations match the pinned decoder and let us advance its state before playback.
typedef struct jar_xm_context_s jar_xm_context_t;
void jar_xm_generate_samples(jar_xm_context_t *context, float *output, size_t frame_count);
int jar_xm_create_context_from_file(
    jar_xm_context_t **context, uint32_t sample_rate, const char *path);
void jar_xm_free_context(jar_xm_context_t *context);

bool xm_music_start_at(Music *music, const char *path, float position)
{
    if (music == NULL || music->ctxData == NULL || music->stream.sampleRate == 0 || path == NULL
        || !isfinite(position) || position < 0.0f)
        return false;

    // LoadMusicStream scans the whole song for its duration. jar_xm_reset does not
    // clear the resulting pending pattern jump, tick remainder, or channel effects.
    // Recreate the decoder from the source instead of inheriting that ending state.
    // The stream must be stopped; retain its buffer and metadata while replacing ctxData.
    jar_xm_context_t *context = NULL;
    if (jar_xm_create_context_from_file(&context, music->stream.sampleRate, path) != 0)
        return false;

    uint64_t frames_remaining = (uint64_t)((double)position * music->stream.sampleRate);
    enum {
        DISCARD_FRAME_COUNT = 4096,
    };
    float discard[DISCARD_FRAME_COUNT * 2];
    while (frames_remaining > 0) {
        size_t frame_count = frames_remaining < DISCARD_FRAME_COUNT ? (size_t)frames_remaining
                                                                    : DISCARD_FRAME_COUNT;
        jar_xm_generate_samples(context, discard, frame_count);
        frames_remaining -= frame_count;
    }
    jar_xm_free_context(music->ctxData);
    music->ctxData = context;
    return true;
}
