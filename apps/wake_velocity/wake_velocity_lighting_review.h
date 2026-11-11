#ifndef PROTEUS_WAKE_VELOCITY_LIGHTING_REVIEW_H
#define PROTEUS_WAKE_VELOCITY_LIGHTING_REVIEW_H

#include "boat_capture.h"
#include "renderer3d.h"
#include "renderer_gpu.h"

#include <stdbool.h>
#include <stdint.h>

// Developer-only, opt-in audit of a running game's lighting cycle. The default game must not
// call this unless an explicit developer flag asks for it.
//
// The review keeps a bounded 18000-row buffer allocated once by begin(), records completed
// frames without allocating, and writes one CSV at end(). A GPU scene also installs temporary
// native GLAD spies for the sky's sun-direction upload and the initial sky-mask texture writes;
// they are restored by end() (or by a failed begin()). CPU rows carry the actual renderer light
// direction and no GPU observations.
//
// Zero-initialize the handle before use. A NULL handle makes begin() fail; an inactive handle makes
// frame()/gpu() no-ops and end() succeed without doing anything. Native only: the browser returns
// false from begin() and writes nothing.
typedef struct {
    void *state;
} WakeLightingReview;

// Opens CSV_PATH and, for a non-NULL GPU scene, validates the scene's sky shader/mask and runs a
// 4x4 positive control before installing the real spies. False on invalid arguments, a file
// failure, an unusable sky scene, or a failed spy control. SCENE is NULL for CPU captures.
bool wake_lighting_review_begin(
    WakeLightingReview *review, const char *csv_path, WaterfrontGpuScene *scene);

// Adds one completed frame. SAMPLE->serial matches the later GPU callback. HOURS is the unwrapped
// presentation hour, PERIOD/RATE the clock tuning, and CYCLE_ENABLED the current cycle state. CPU
// is the CPU renderer when recording CPU frames, otherwise NULL. After 18000 rows recording stops
// with one stderr warning; the caller continues normally. In the CSV, sun_seen/sun_* are an actual
// GL sky-uniform upload observed since the previous recorded frame; clock_glitter_seconds is the
// separate presentation-clock diagnostic from waterfront_glitter_clock, not a sun observation.
void wake_lighting_review_frame(WakeLightingReview *review, const BoatCaptureSample *sample,
    uint64_t tick, double hours, double period, float rate, bool cycle_enabled,
    const Renderer3D *cpu);

// Fills the already-recorded row with SERIAL from an asynchronous GPU timer result. Out-of-range
// serials and non-finite durations are ignored; a missing result leaves the CSV cell blank with
// has_gpu=0.
void wake_lighting_review_gpu(WakeLightingReview *review, unsigned long long serial, double ms);

// Restores any installed GL function pointers/bindings, writes the buffered CSV, and frees the
// state. Call after the last asynchronous GPU result has been delivered. False if the file could
// not be written completely.
bool wake_lighting_review_end(WakeLightingReview *review);

#endif
