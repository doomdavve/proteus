#include "boat_capture.h"

#include <stdlib.h>

static int compare_float(const void *left, const void *right)
{
    float a = *(const float *)left, b = *(const float *)right;
    return (a > b) - (a < b);
}

void boat_capture_add(BoatCapture *capture, const BoatCaptureSample *sample)
{
    capture->samples[capture->next] = *sample;
    capture->next = (capture->next + 1) % BOAT_CAPTURE_SAMPLES;
    if (capture->count < BOAT_CAPTURE_SAMPLES)
        ++capture->count;
    ++capture->total;
    // The window's span follows the oldest retained frame, not the first frame ever seen.
    unsigned oldest
        = (capture->next + BOAT_CAPTURE_SAMPLES - capture->count) % BOAT_CAPTURE_SAMPLES;
    capture->first_time = capture->samples[oldest].time;
    capture->last_time = sample->time;
}

void boat_capture_segment(BoatCapture *capture) { ++capture->segment; }

static unsigned retained(const BoatCapture *capture, unsigned i)
{
    return (capture->next + BOAT_CAPTURE_SAMPLES - capture->count + i) % BOAT_CAPTURE_SAMPLES;
}

// Percentiles use the same floor((count - 1) * percentile / 100) convention as the benchmark.
static BoatCaptureStats summarize(
    const BoatCapture *capture, float (*select)(const BoatCaptureSample *), bool skip_first)
{
    float scratch[BOAT_CAPTURE_SAMPLES];
    BoatCaptureStats stats = { 0 };
    float total = 0;
    unsigned first = skip_first ? 1 : 0;
    for (unsigned i = first; i < capture->count; ++i) {
        float value = select(&capture->samples[retained(capture, i)]);
        scratch[i - first] = value;
        total += value;
        if (i == first || value > stats.maximum)
            stats.maximum = value;
        ++stats.samples;
    }
    if (!stats.samples) {
        stats.maximum = 0;
        return stats;
    }
    stats.mean = total / (float)stats.samples;
    qsort(scratch, (size_t)stats.samples, sizeof(scratch[0]), compare_float);
    stats.p50 = scratch[(stats.samples - 1) * 50 / 100];
    stats.p95 = scratch[(stats.samples - 1) * 95 / 100];
    stats.p99 = scratch[(stats.samples - 1) * 99 / 100];
    return stats;
}

static float frame_of(const BoatCaptureSample *sample) { return sample->frame_ms; }
static float interval_of(const BoatCaptureSample *sample) { return sample->interval_ms; }
static float simulation_of(const BoatCaptureSample *sample) { return sample->simulation_ms; }
static float render_of(const BoatCaptureSample *sample) { return sample->render_ms; }
static float present_of(const BoatCaptureSample *sample) { return sample->present_ms; }

BoatCaptureSummary boat_capture_summary(const BoatCapture *capture, float target_interval_ms)
{
    BoatCaptureSummary summary = { 0 };
    summary.samples = (int)capture->count;
    summary.duration_seconds = capture->count ? capture->last_time - capture->first_time : 0;
    summary.target_interval_ms = target_interval_ms;
    summary.frame_ms = summarize(capture, frame_of, false);
    summary.interval_ms = summarize(capture, interval_of, true);
    summary.simulation_ms = summarize(capture, simulation_of, false);
    summary.render_ms = summarize(capture, render_of, false);
    summary.present_ms = summarize(capture, present_of, false);
    unsigned segments = 0, previous = 0;
    for (unsigned i = 0; i < capture->count; ++i) {
        const BoatCaptureSample *sample = &capture->samples[retained(capture, i)];
        segments += i == 0 || sample->segment != previous;
        previous = sample->segment;
        summary.gpu_samples += sample->has_gpu;
        summary.gpu_unavailable += !sample->has_gpu;
        summary.hitches += i && sample->interval_ms > target_interval_ms;
    }
    summary.segments = segments;
    // Missing GPU queries stay absent rather than contributing a zero sample.
    float scratch[BOAT_CAPTURE_SAMPLES];
    unsigned gpu_count = 0;
    for (unsigned i = 0; i < capture->count; ++i) {
        const BoatCaptureSample *sample = &capture->samples[retained(capture, i)];
        if (!sample->has_gpu)
            continue;
        scratch[gpu_count++] = sample->gpu_ms;
        if (gpu_count == 1 || sample->gpu_ms > summary.gpu_ms.maximum)
            summary.gpu_ms.maximum = sample->gpu_ms;
    }
    summary.gpu_ms.samples = (int)gpu_count;
    if (gpu_count) {
        float total = 0;
        for (unsigned i = 0; i < gpu_count; ++i)
            total += scratch[i];
        summary.gpu_ms.mean = total / (float)gpu_count;
        qsort(scratch, gpu_count, sizeof(scratch[0]), compare_float);
        summary.gpu_ms.p50 = scratch[(gpu_count - 1) * 50 / 100];
        summary.gpu_ms.p95 = scratch[(gpu_count - 1) * 95 / 100];
        summary.gpu_ms.p99 = scratch[(gpu_count - 1) * 99 / 100];
    } else {
        summary.gpu_ms.maximum = 0;
    }
    return summary;
}

bool boat_capture_truncated(const BoatCapture *capture)
{
    return capture->count < BOAT_CAPTURE_SAMPLES;
}

BoatCaptureSample *boat_capture_sample(BoatCapture *capture, unsigned long long serial)
{
    for (unsigned i = 0; i < capture->count; ++i) {
        BoatCaptureSample *sample = &capture->samples[retained(capture, i)];
        if (sample->serial == serial)
            return sample;
    }
    return NULL;
}
