#ifndef PROTEUS_BOAT_PROFILE_H
#define PROTEUS_BOAT_PROFILE_H

#include <stdbool.h>

enum { BOAT_PROFILE_HISTORY = 240 };

// Inclusive timings: simulation includes physics ticks and in-loop course/replay work;
// course/replay also measures input and ghost evaluation outside simulation. Frame wall includes
// every measured section, including present/wait. Never add parent and child totals together.
typedef struct {
    float frame_ms, simulation_ms, physics_ms, physics_max_ms;
    float course_replay_ms, audio_ms, snapshot_render_ms, present_ms;
    float discarded_active_ms;
    unsigned int ticks;
    bool paused;
} BoatFrameMetrics;

typedef struct {
    BoatFrameMetrics frames[BOAT_PROFILE_HISTORY];
    int next, count;
} BoatProfile;

typedef struct {
    float latest, mean, p50, p95, p99, maximum;
} BoatMetricStats;

enum { BOAT_PROFILE_ROWS = 9 };
typedef struct {
    BoatMetricStats rows[BOAT_PROFILE_ROWS];
} BoatProfileSnapshot;

void boat_profile_add(BoatProfile *profile, BoatFrameMetrics frame);
BoatMetricStats boat_profile_stats(const BoatProfile *profile, float (*select)(BoatFrameMetrics));
BoatMetricStats boat_metric_stats(const float *values, int count);
// Whole-run reports: sorts VALUES in place, with no history bound. Percentiles use sorted index
// floor((count - 1) * percentile / 100); latest is the last value before sorting.
BoatMetricStats boat_metric_stats_sort(float *values, int count);
float boat_metric_frame(BoatFrameMetrics frame);
float boat_metric_simulation(BoatFrameMetrics frame);
float boat_metric_physics(BoatFrameMetrics frame);
float boat_metric_physics_max(BoatFrameMetrics frame);
float boat_metric_course_replay(BoatFrameMetrics frame);
float boat_metric_audio(BoatFrameMetrics frame);
float boat_metric_snapshot_render(BoatFrameMetrics frame);
float boat_metric_present(BoatFrameMetrics frame);
float boat_metric_discarded(BoatFrameMetrics frame);
float boat_metric_ticks(BoatFrameMetrics frame);
float boat_profile_discarded_seconds(double raw_delta, bool active);
// With a live raylib window, pass true to use GetTime(). Otherwise use a platform monotonic clock.
double boat_profile_time(bool window_ready);
BoatProfileSnapshot boat_profile_snapshot(const BoatProfile *profile);
void boat_profile_draw(const BoatProfileSnapshot *snapshot, int x, int y);

#endif
