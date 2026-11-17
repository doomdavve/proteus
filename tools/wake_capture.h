#ifndef PROTEUS_WAKE_CAPTURE_H
#define PROTEUS_WAKE_CAPTURE_H

#include "wake_geometry.h"

// R2b private provenance experiment. Never linked to production or serialized on the wire.
typedef struct {
    Vector2 start, end;
    uint64_t started, ended, serial;
} R2Span;
typedef struct {
    R2Span span, predecessor;
    uint64_t identity, epoch;
    float curvature;
    bool paired, eligible;
} R2Descriptor;
typedef struct {
    R2Span previous;
    uint64_t identity, epoch;
    bool active, has_previous, span_eligible;
} R2CaptureSource;
typedef struct {
    R2Field field;
    R2Descriptor descriptors[BOAT_OBJECT_CAPACITY];
    R2CaptureSource sources[BOAT_OBJECT_BOATS];
} R2Capture;
typedef struct {
    R2Capture capture;
    uint64_t checksum;
    unsigned version;
} R2CaptureSnapshot;

void r2_capture_init(R2Capture *capture);
bool r2_capture_advance(R2Capture *capture);
// Identity is a nonzero caller-owned hull/session generation, never a recyclable ring slot.
bool r2_capture_track(R2Capture *capture, unsigned emitter, uint64_t identity, R2Source source);
// Explicit teleport/reset: retain old waves but break the source epoch before the next track.
bool r2_capture_restart(R2Capture *capture, unsigned emitter);
bool r2_capture_valid(const R2Capture *capture);
// Same-build, in-memory research snapshots only; checksum is corruption detection, not security.
bool r2_capture_snapshot(const R2Capture *capture, R2CaptureSnapshot *snapshot);
bool r2_capture_restore(R2Capture *capture, const R2CaptureSnapshot *snapshot);

#endif
