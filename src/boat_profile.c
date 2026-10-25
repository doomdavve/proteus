#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L // clock_gettime under strict C17 (glibc).
#endif
#include "boat_profile.h"

#include "raylib.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

static int compare_float(const void *a, const void *b)
{
    float left = *(const float *)a, right = *(const float *)b;
    return (left > right) - (left < right);
}

BoatMetricStats boat_metric_stats_sort(float *values, int count)
{
    BoatMetricStats result = { 0 };
    if (!values || count <= 0)
        return result;
    result.latest = values[count - 1];
    for (int i = 0; i < count; ++i)
        result.mean += values[i];
    result.mean /= count;
    qsort(values, (size_t)count, sizeof(values[0]), compare_float);
    // 64-bit products keep the index exact for any int count.
    result.p50 = values[(long long)(count - 1) * 50 / 100];
    result.p95 = values[(long long)(count - 1) * 95 / 100];
    result.p99 = values[(long long)(count - 1) * 99 / 100];
    result.maximum = values[count - 1];
    return result;
}

BoatMetricStats boat_metric_stats(const float *values, int count)
{
    if (!values || count <= 0)
        return (BoatMetricStats) { 0 };
    if (count > BOAT_PROFILE_HISTORY)
        count = BOAT_PROFILE_HISTORY;
    float sorted[BOAT_PROFILE_HISTORY];
    for (int i = 0; i < count; ++i)
        sorted[i] = values[i];
    return boat_metric_stats_sort(sorted, count);
}

void boat_profile_add(BoatProfile *profile, BoatFrameMetrics frame)
{
    profile->frames[profile->next] = frame;
    profile->next = (profile->next + 1) % BOAT_PROFILE_HISTORY;
    if (profile->count < BOAT_PROFILE_HISTORY)
        ++profile->count;
}

BoatMetricStats boat_profile_stats(const BoatProfile *profile, float (*select)(BoatFrameMetrics))
{
    float values[BOAT_PROFILE_HISTORY];
    if (!profile || !select || profile->count == 0)
        return (BoatMetricStats) { 0 };
    int first = profile->count == BOAT_PROFILE_HISTORY ? profile->next : 0;
    for (int i = 0; i < profile->count; ++i)
        values[i] = select(profile->frames[(first + i) % BOAT_PROFILE_HISTORY]);
    return boat_metric_stats(values, profile->count);
}

#define FIELD_SELECT(name, field)                                                                  \
    float boat_metric_##name(BoatFrameMetrics frame) { return frame.field; }
FIELD_SELECT(frame, frame_ms)
FIELD_SELECT(simulation, simulation_ms)
FIELD_SELECT(physics, physics_ms)
FIELD_SELECT(physics_max, physics_max_ms)
FIELD_SELECT(course_replay, course_replay_ms)
FIELD_SELECT(audio, audio_ms)
FIELD_SELECT(snapshot_render, snapshot_render_ms)
FIELD_SELECT(present, present_ms)
FIELD_SELECT(discarded, discarded_active_ms)
float boat_metric_ticks(BoatFrameMetrics frame) { return (float)frame.ticks; }

float boat_profile_discarded_seconds(double raw_delta, bool active)
{
    return active ? (float)fmax(raw_delta - 0.1, 0.0) : 0.0f;
}

double boat_profile_time(bool window_ready)
{
    if (window_ready)
        return GetTime();
#ifdef _WIN32
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec stamp;
    clock_gettime(CLOCK_MONOTONIC, &stamp);
    return (double)stamp.tv_sec + (double)stamp.tv_nsec / 1000000000.0;
#endif
}

static const struct {
    const char *label;
    float (*select)(BoatFrameMetrics);
} profile_rows[BOAT_PROFILE_ROWS] = { { "sim total", boat_metric_simulation },
    { "physics total", boat_metric_physics }, { "physics max/tick", boat_metric_physics_max },
    { "course/replay", boat_metric_course_replay }, { "audio", boat_metric_audio },
    { "snapshot/render", boat_metric_snapshot_render }, { "present/wait", boat_metric_present },
    { "discarded active", boat_metric_discarded }, { "ticks/frame", boat_metric_ticks } };

BoatProfileSnapshot boat_profile_snapshot(const BoatProfile *profile)
{
    BoatProfileSnapshot result = { 0 };
    for (int i = 0; i < BOAT_PROFILE_ROWS; ++i)
        result.rows[i] = boat_profile_stats(profile, profile_rows[i].select);
    return result;
}

void boat_profile_draw(const BoatProfileSnapshot *snapshot, int x, int y)
{
    DrawRectangle(x, y, 680, 197, (Color) { 2, 5, 10, 225 });
    DrawRectangleLines(x, y, 680, 197, (Color) { 150, 175, 195, 255 });
    DrawText("BOATING  now / mean / p95 / max (ms, except ticks)", x + 10, y + 7, 14, RAYWHITE);
    for (int i = 0; i < BOAT_PROFILE_ROWS; ++i) {
        BoatMetricStats stats = snapshot->rows[i];
        DrawText(TextFormat("%-17s %7.2f %7.2f %7.2f %7.2f", profile_rows[i].label, stats.latest,
                     stats.mean, stats.p95, stats.maximum),
            x + 10, y + 27 + i * 17, 13, LIGHTGRAY);
    }
    DrawText("nested totals overlap; frame includes all sections", x + 10, y + 182, 11, GRAY);
}
