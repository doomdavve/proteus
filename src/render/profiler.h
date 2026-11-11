#ifndef PROTEUS_PROFILER_H
#define PROTEUS_PROFILER_H

#include "raylib.h"

enum {
    PROFILER_HISTORY_COUNT = 240,
};

typedef struct {
    int width;
    int height;
} VirtualSize;

typedef struct {
    float frame_ms[PROFILER_HISTORY_COUNT];
    float scene_ms[PROFILER_HISTORY_COUNT];
    float present_ms[PROFILER_HISTORY_COUNT];
    int next;
    int count;
} Profiler;

typedef struct {
    float latest;
    float minimum;
    float average;
    float percentile_95;
    float maximum;
} ProfileStats;

typedef struct {
    ProfileStats frame;
    ProfileStats scene_cost;
    ProfileStats present;
    float scene_time;
    const char *scene_label;
    const char *renderer_label;
    int texture_upload_kib;
    float observed_fps;
    int raylib_fps;
    int monitor_refresh_rate;
    int sample_count;
    int window_width;
    int window_height;
    int render_width;
    int render_height;
    int scale;
    int framebuffer_kib;
} ProfilerSnapshot;

void profiler_add(Profiler *profiler, float frame_ms, float scene_ms, float present_ms);
ProfilerSnapshot profiler_snapshot(
    const Profiler *profiler, float scene_time, VirtualSize size, Rectangle destination);
void profiler_draw(const Profiler *profiler, const ProfilerSnapshot *snapshot, VirtualSize size);

#endif
