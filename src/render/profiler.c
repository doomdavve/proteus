#include "profiler.h"

#include <math.h>
#include <stdlib.h>

static int compare_floats(const void *left, const void *right)
{
    float a = *(const float *)left;
    float b = *(const float *)right;
    return (a > b) - (a < b);
}

static ProfileStats profile_stats(const float *samples, int next, int count)
{
    ProfileStats stats = { 0 };
    if (count == 0)
        return stats;

    float sorted[PROFILER_HISTORY_COUNT];
    stats.minimum = samples[0];
    stats.maximum = samples[0];
    for (int i = 0; i < count; ++i) {
        float sample = samples[i];
        sorted[i] = sample;
        stats.minimum = fminf(stats.minimum, sample);
        stats.maximum = fmaxf(stats.maximum, sample);
        stats.average += sample;
    }
    stats.average /= (float)count;
    qsort(sorted, (size_t)count, sizeof(sorted[0]), compare_floats);
    stats.percentile_95 = sorted[(count - 1) * 95 / 100];
    int latest = (next + PROFILER_HISTORY_COUNT - 1) % PROFILER_HISTORY_COUNT;
    stats.latest = samples[latest];
    return stats;
}

void profiler_add(Profiler *profiler, float frame_ms, float scene_ms, float present_ms)
{
    profiler->frame_ms[profiler->next] = frame_ms;
    profiler->scene_ms[profiler->next] = scene_ms;
    profiler->present_ms[profiler->next] = present_ms;
    profiler->next = (profiler->next + 1) % PROFILER_HISTORY_COUNT;
    if (profiler->count < PROFILER_HISTORY_COUNT)
        ++profiler->count;
}

ProfilerSnapshot profiler_snapshot(
    const Profiler *profiler, float scene_time, VirtualSize size, Rectangle destination)
{
    ProfilerSnapshot snapshot = {
        .frame = profile_stats(profiler->frame_ms, profiler->next, profiler->count),
        .scene_cost = profile_stats(profiler->scene_ms, profiler->next, profiler->count),
        .present = profile_stats(profiler->present_ms, profiler->next, profiler->count),
        .scene_time = scene_time,
        .renderer_label = "CPU",
        .texture_upload_kib = size.width * size.height * (int)sizeof(Color) / 1024,
        .raylib_fps = GetFPS(),
        .monitor_refresh_rate = GetMonitorRefreshRate(GetCurrentMonitor()),
        .sample_count = profiler->count,
        .window_width = GetScreenWidth(),
        .window_height = GetScreenHeight(),
        .render_width = GetRenderWidth(),
        .render_height = GetRenderHeight(),
        .scale = size.width > 0 ? (int)destination.width / size.width : 0,
        .framebuffer_kib = size.width * size.height * (int)sizeof(Color) / 1024,
    };
    snapshot.observed_fps = snapshot.frame.average > 0.0f ? 1000.0f / snapshot.frame.average : 0.0f;
    return snapshot;
}

static float clamp01(float value) { return fmaxf(0.0f, fminf(value, 1.0f)); }

static float graph_y(Rectangle bounds, float milliseconds, float maximum)
{
    float amount = clamp01(milliseconds / maximum);
    return bounds.y + bounds.height - amount * bounds.height;
}

static void draw_profiler_graph(const Profiler *profiler, Rectangle bounds)
{
    DrawRectangleRec(bounds, (Color) { 4, 8, 14, 235 });
    DrawRectangleLinesEx(bounds, 1.0f, (Color) { 105, 125, 145, 255 });
    if (profiler->count == 0)
        return;

    float maximum = 35.0f;
    for (int i = 0; i < profiler->count; ++i)
        maximum = fmaxf(maximum, profiler->frame_ms[i] * 1.1f);

    const float budgets[] = { 1000.0f / 60.0f, 1000.0f / 30.0f };
    const Color budget_colors[] = {
        { 75, 105, 125, 210 },
        { 125, 75, 75, 210 },
    };
    for (int i = 0; i < 2; ++i) {
        float y = graph_y(bounds, budgets[i], maximum);
        DrawLine((int)bounds.x, (int)y, (int)(bounds.x + bounds.width), (int)y, budget_colors[i]);
        DrawText(TextFormat("%d", i == 0 ? 60 : 30), (int)bounds.x + 3, (int)y - 11, 10,
            budget_colors[i]);
    }

    int first = profiler->count == PROFILER_HISTORY_COUNT ? profiler->next : 0;
    Vector2 previous_frame = { 0 };
    Vector2 previous_scene = { 0 };
    Vector2 previous_present = { 0 };
    for (int i = 0; i < profiler->count; ++i) {
        int index = (first + i) % PROFILER_HISTORY_COUNT;
        float x = bounds.x + bounds.width
            - (float)(profiler->count - 1 - i) * (bounds.width - 1.0f)
                / (float)(PROFILER_HISTORY_COUNT - 1);
        Vector2 frame = { x, graph_y(bounds, profiler->frame_ms[index], maximum) };
        Vector2 scene = { x, graph_y(bounds, profiler->scene_ms[index], maximum) };
        Vector2 present = { x, graph_y(bounds, profiler->present_ms[index], maximum) };
        if (i > 0) {
            DrawLineV(previous_frame, frame, (Color) { 255, 210, 70, 255 });
            DrawLineV(previous_scene, scene, (Color) { 80, 245, 135, 255 });
            DrawLineV(previous_present, present, (Color) { 80, 190, 255, 255 });
        }
        previous_frame = frame;
        previous_scene = scene;
        previous_present = present;
    }

    DrawText(TextFormat("%.0f ms", maximum), (int)(bounds.x + bounds.width) - 47, (int)bounds.y + 3,
        10, GRAY);
}

void profiler_draw(const Profiler *profiler, const ProfilerSnapshot *snapshot, VirtualSize size)
{
    const int x = 12;
    const int y = 12;
    const int width = 680;
    const int height = 318;
    DrawRectangle(x, y, width, height, (Color) { 2, 5, 10, 225 });
    DrawRectangleLines(x, y, width, height, (Color) { 150, 175, 195, 255 });
    DrawText("PROFILER [F3]", x + 10, y + 8, 18, RAYWHITE);
    DrawText(TextFormat("scene %s  simulation %.1f s  renderer %s", snapshot->scene_label,
                 snapshot->scene_time, snapshot->renderer_label),
        x + 10, y + 31, 14, RAYWHITE);
    DrawText(TextFormat("frame ms    now %6.1f  avg %6.1f  p95 %6.1f  min %6.1f  max %6.1f",
                 snapshot->frame.latest, snapshot->frame.average, snapshot->frame.percentile_95,
                 snapshot->frame.minimum, snapshot->frame.maximum),
        x + 10, y + 51, 14, (Color) { 255, 210, 70, 255 });
    DrawText(TextFormat("scene ms    now %6.1f  avg %6.1f  p95 %6.1f  min %6.1f  max %6.1f",
                 snapshot->scene_cost.latest, snapshot->scene_cost.average,
                 snapshot->scene_cost.percentile_95, snapshot->scene_cost.minimum,
                 snapshot->scene_cost.maximum),
        x + 10, y + 70, 14, (Color) { 80, 245, 135, 255 });
    DrawText(
        TextFormat("present ms  now %6.1f  avg %6.1f  p95 %6.1f  min %6.1f  max %6.1f",
            snapshot->present.latest, snapshot->present.average, snapshot->present.percentile_95,
            snapshot->present.minimum, snapshot->present.maximum),
        x + 10, y + 89, 14, (Color) { 80, 190, 255, 255 });
    DrawText(TextFormat("FPS observed %5.1f  raylib %3d  monitor %d Hz  samples %d/%d",
                 snapshot->observed_fps, snapshot->raylib_fps, snapshot->monitor_refresh_rate,
                 snapshot->sample_count, PROFILER_HISTORY_COUNT),
        x + 10, y + 109, 14, RAYWHITE);
    DrawText(TextFormat("virtual %dx%d  window %dx%d  render %dx%d  integer scale %dx", size.width,
                 size.height, snapshot->window_width, snapshot->window_height,
                 snapshot->render_width, snapshot->render_height, snapshot->scale),
        x + 10, y + 128, 14, RAYWHITE);
    DrawText(TextFormat("framebuffer %d KiB  texture upload %d KiB/frame",
                 snapshot->framebuffer_kib, snapshot->texture_upload_kib),
        x + 10, y + 147, 14, RAYWHITE);
    DrawText("rolling 240 frames:  frame wall     scene submit+upload  present/wait", x + 10,
        y + 168, 13, LIGHTGRAY);
    DrawRectangle(x + 111, y + 172, 12, 2, (Color) { 255, 210, 70, 255 });
    DrawRectangle(x + 241, y + 172, 12, 2, (Color) { 80, 245, 135, 255 });
    DrawRectangle(x + 392, y + 172, 12, 2, (Color) { 80, 190, 255, 255 });
    draw_profiler_graph(profiler, (Rectangle) { x + 10, y + 187, width - 20, 120 });
}
