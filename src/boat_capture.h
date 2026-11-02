#ifndef PROTEUS_BOAT_CAPTURE_H
#define PROTEUS_BOAT_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>

// G0w bounded live capture: a rolling window of completed frames, owned by the application.
// This unit only samples and summarizes; file I/O lives in the application/tool exporter.
// The default window is the most recent 30 seconds at 60 Hz. A slower display simply covers a
// longer interval; the recorded first/last timestamps and total count say what was kept.
enum { BOAT_CAPTURE_SAMPLES = 1800, BOAT_CAPTURE_PASSES = 3, BOAT_CAPTURE_GROUPS = 3 };

typedef struct {
    double time; // Seconds from the application's monotonic clock.
    unsigned long long serial; // Monotonic completed-frame identity, used to match GPU queries.
    float interval_ms; // Current frame start minus the previous frame start.
    float frame_ms, simulation_ms, physics_ms, audio_ms, render_ms, present_ms;
    float gpu_ms; // Asynchronous GL_TIME_ELAPSED result, matched to its originating frame.
    float discarded_ms;
    unsigned ticks;
    int map; // BoatLocationId at sample time.
    int preset; // Application presentation preset index; changes start a new segment.
    unsigned segment; // Presentation/settings epoch; never averaged across a change.
    float x, z, heading, camera_heading;
    float speed;
    bool focused, active, paused;
    bool has_gpu, gpu_pending;
    unsigned app_submissions;
    size_t scene_triangles;
    unsigned pass_submissions[BOAT_CAPTURE_PASSES];
    size_t pass_triangles[BOAT_CAPTURE_PASSES];
    unsigned group_submissions[BOAT_CAPTURE_GROUPS];
    size_t group_triangles[BOAT_CAPTURE_GROUPS];
    unsigned wake_live, wake_peak_live, wake_emitters, traffic_wake_live;
} BoatCaptureSample;

typedef struct {
    int samples;
    float mean, p50, p95, p99, maximum;
} BoatCaptureStats;

typedef struct {
    int samples;
    double duration_seconds;
    unsigned segments;
    unsigned hitches; // Frames whose interval exceeded the recorded target.
    float target_interval_ms;
    unsigned gpu_samples, gpu_unavailable;
    BoatCaptureStats frame_ms, interval_ms, simulation_ms, render_ms, present_ms, gpu_ms;
} BoatCaptureSummary;

typedef struct {
    BoatCaptureSample samples[BOAT_CAPTURE_SAMPLES];
    unsigned next, count, total, segment;
    double first_time, last_time;
} BoatCapture;

// Zero initializes. Adds one completed frame, evicting the oldest when the ring is full.
void boat_capture_add(BoatCapture *capture, const BoatCaptureSample *sample);
// Marks a settings/map/presentation change so later samples never average across epochs.
void boat_capture_segment(BoatCapture *capture);
// Summarizes the retained window; target_interval_ms drives the hitch count. No wall clock,
// no GPU query and no allocation: a report snapshot never waits on pending work.
BoatCaptureSummary boat_capture_summary(const BoatCapture *capture, float target_interval_ms);
// True when the retained samples cover less than the intended window (startup or overflow).
bool boat_capture_truncated(const BoatCapture *capture);
// The retained sample with SERIAL, or NULL after that frame has left the ring.
BoatCaptureSample *boat_capture_sample(BoatCapture *capture, unsigned long long serial);

#endif
