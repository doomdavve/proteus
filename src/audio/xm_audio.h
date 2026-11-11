#ifndef PROTEUS_XM_AUDIO_H
#define PROTEUS_XM_AUDIO_H

#include "raylib.h"

#include <stdbool.h>

// Recreate a stopped XM stream's decoder from its source, then advance to the position.
// Replaces ctxData on success; leaves the original decoder intact on failure.
bool xm_music_start_at(Music *music, const char *path, float position);

#endif
