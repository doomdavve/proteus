#include "wake_velocity_performance.h"

#include "boat_version.h"
#include "external/glad.h"
#include "raylib.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#define make_dir(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#define make_dir(path) mkdir(path, 0777)
#endif

#ifndef WAKE_PERFORMANCE_REVISION
#define WAKE_PERFORMANCE_REVISION "unknown"
#endif
#ifndef WAKE_PERFORMANCE_BUILD_CONFIG
#define WAKE_PERFORMANCE_BUILD_CONFIG "unknown"
#endif
#ifndef WAKE_PERFORMANCE_PLATFORM
#define WAKE_PERFORMANCE_PLATFORM "unknown/unknown"
#endif
#ifndef WAKE_PERFORMANCE_COMPILER
#define WAKE_PERFORMANCE_COMPILER "unknown"
#endif

#define PERFORMANCE_FORMAT_REVISION 1
#define PERFORMANCE_FILE_COUNT 5

static const char *const performance_files[PERFORMANCE_FILE_COUNT]
    = { "manifest.json", "summary.json", "samples.csv", "summary.html", "build.txt" };

static void copy_text(char *out, size_t size, const char *text)
{
    if (!size)
        return;
    snprintf(out, size, "%s", text ? text : "");
}

static void json_string(FILE *out, const char *text)
{
    fputc('"', out);
    for (; text && *text; ++text) {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\')
            fprintf(out, "\\%c", c);
        else if (c == '\n')
            fputs("\\n", out);
        else if (c < 0x20)
            fprintf(out, "\\u%04x", c);
        else
            fputc(c, out);
    }
    fputc('"', out);
}

static void json_stats(FILE *out, const char *name, BoatCaptureStats stats)
{
    fprintf(out,
        "  \"%s\":{\"samples\":%d,\"mean\":%.6g,\"p50\":%.6g,\"p95\":%.6g,\"p99\":%.6g,"
        "\"max\":%.6g}",
        name, stats.samples, (double)stats.mean, (double)stats.p50, (double)stats.p95,
        (double)stats.p99, (double)stats.maximum);
}

#if defined(__APPLE__)
#include <sys/sysctl.h>
static const char *cpu_label(void)
{
    static char label[256];
    size_t size = sizeof(label);
    if (sysctlbyname("machdep.cpu.brand_string", label, &size, NULL, 0) == 0)
        return label;
    return "unknown";
}
#elif defined(_WIN32)
static const char *cpu_label(void)
{
    static char label[256];
    DWORD size = GetEnvironmentVariableA("PROCESSOR_IDENTIFIER", label, sizeof(label));
    return size && size < sizeof(label) ? label : "unknown";
}
#else
static const char *cpu_label(void)
{
    static struct utsname name;
    if (uname(&name) == 0) {
        static char label[256];
        snprintf(label, sizeof(label), "%s %s", name.machine, name.release);
        return label;
    }
    return "unknown";
}
#endif

static const char *build_config_label(void)
{
#if defined(NDEBUG)
    static const char *const config = "release";
#else
    static const char *const config = "debug";
#endif
    static char label[128];
    snprintf(label, sizeof(label), "%s/%s", WAKE_PERFORMANCE_BUILD_CONFIG, config);
    return label;
}

static void timestamp_utc(char *out, size_t size)
{
    time_t now = time(NULL);
    struct tm utc;
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    strftime(out, size, "%Y-%m-%dT%H:%M:%SZ", &utc);
}

// A per-segment summary keeps settings epochs separate. The capture helper is cheap to copy
// into a temporary ring; the retained samples alone are grouped.
static void write_segment_summaries(FILE *out, const BoatCapture *capture, float target)
{
    fputs("  \"segments\":[", out);
    bool first_segment = true;
    for (unsigned i = 0; i < capture->count; ++i) {
        unsigned index
            = (capture->next + BOAT_CAPTURE_SAMPLES - capture->count + i) % BOAT_CAPTURE_SAMPLES;
        unsigned segment = capture->samples[index].segment;
        bool seen = false;
        for (unsigned j = 0; j < i && !seen; ++j) {
            unsigned earlier = (capture->next + BOAT_CAPTURE_SAMPLES - capture->count + j)
                % BOAT_CAPTURE_SAMPLES;
            seen = capture->samples[earlier].segment == segment;
        }
        if (seen)
            continue;
        BoatCapture *group = calloc(1, sizeof(*group));
        if (!group)
            continue;
        for (unsigned j = 0; j < capture->count; ++j) {
            unsigned sample = (capture->next + BOAT_CAPTURE_SAMPLES - capture->count + j)
                % BOAT_CAPTURE_SAMPLES;
            if (capture->samples[sample].segment == segment)
                boat_capture_add(group, &capture->samples[sample]);
        }
        BoatCaptureSummary summary = boat_capture_summary(group, target);
        fprintf(out,
            "%s\n    {\"id\":%u,\"map\":%d,\"preset\":%d,\"samples\":%d,"
            "\"duration_s\":%.6g,\"hitches\":%u,\"frame_ms\":{\"p50\":%.6g,"
            "\"p95\":%.6g,\"p99\":%.6g},\"interval_ms\":{\"p50\":%.6g,\"p95\":%.6g,"
            "\"p99\":%.6g}}",
            first_segment ? "" : ",", segment, capture->samples[index].map,
            capture->samples[index].preset, summary.samples, summary.duration_seconds,
            summary.hitches, (double)summary.frame_ms.p50, (double)summary.frame_ms.p95,
            (double)summary.frame_ms.p99, (double)summary.interval_ms.p50,
            (double)summary.interval_ms.p95, (double)summary.interval_ms.p99);
        first_segment = false;
        free(group);
    }
    fputs("\n  ],\n", out);
}

static void write_manifest(FILE *out, const BoatCapture *capture, const WakePerformanceInfo *info,
    const BoatCaptureSummary *summary, const char *timestamp)
{
    const char *renderer = glGetString ? (const char *)glGetString(GL_RENDERER) : NULL;
    const char *version = glGetString ? (const char *)glGetString(GL_VERSION) : NULL;
    const char *vendor = glGetString ? (const char *)glGetString(GL_VENDOR) : NULL;
    Vector2 dpi = IsWindowReady() ? GetWindowScaleDPI() : (Vector2) { 1, 1 };
    fprintf(out, "{\n  \"format\":\"wake-velocity-performance\",\"revision\":%d,\n",
        PERFORMANCE_FORMAT_REVISION);
    fprintf(out, "  \"capture_kind\":\"%s\",\"captured_at\":\"%s\",\n",
        info->capture_kind ? info->capture_kind : "live", timestamp);
    fputs("  \"tool\":{\"name\":\"wake_velocity\",\"version\":", out);
    json_string(out, PROTEUS_VERSION);
    fputs(",\n    \"commit\":", out);
    json_string(out, WAKE_PERFORMANCE_REVISION);
    fputs(",\n    \"build_configuration\":", out);
    json_string(out, build_config_label());
    fputs(",\n    \"platform\":", out);
    json_string(out, WAKE_PERFORMANCE_PLATFORM);
    fputs(",\n    \"compiler\":", out);
    json_string(out, WAKE_PERFORMANCE_COMPILER);
    fputs("},\n  \"system\":{\"cpu\":", out);
    json_string(out, cpu_label());
    fputs(",\n    \"memory_bytes\":null,\n    \"gl_vendor\":", out);
    json_string(out, vendor ? vendor : "unknown");
    fputs(",\n    \"gl_renderer\":", out);
    json_string(out, renderer ? renderer : "unavailable");
    fputs(",\n    \"gl_version\":", out);
    json_string(out, version ? version : "unavailable");
    fprintf(out, ",\n    \"monitor_refresh_hz\":%d,\"dpi\":[%.6g,%.6g]},\n",
        info->monitor_refresh_hz, (double)dpi.x, (double)dpi.y);
    fprintf(out,
        "  \"session\":{\"map\":\"%s\",\"preset\":\"%s\",\"vsync\":%s,\"aa\":%s,"
        "\"fullscreen\":%s,\n"
        "    \"window\":[%d,%d],\"framebuffer\":[%d,%d],\"scene\":[%d,%d],"
        "\"reflection\":[%d,%d],\n"
        "    \"effects\":{\"waves\":%s,\"wake_objects\":%s,\"lighting\":%s,\"shadows\":%s,"
        "\"reflections\":%s}},\n",
        info->map_name ? info->map_name : "unknown", info->preset_name ? info->preset_name : "low",
        info->vsync ? "true" : "false", info->aa ? "true" : "false",
        info->fullscreen ? "true" : "false", info->window_width, info->window_height,
        info->render_width, info->render_height, info->scene_width, info->scene_height,
        info->reflection_width, info->reflection_height, info->waves ? "true" : "false",
        info->wake_objects ? "true" : "false", info->lighting ? "true" : "false",
        info->shadows ? "true" : "false", info->reflections ? "true" : "false");
    fprintf(out,
        "  \"sampling\":{\"capacity\":%d,\"samples\":%d,\"total_frames\":%u,"
        "\"duration_s\":%.6g,\"truncated\":%s,\n"
        "    \"scope\":\"completed frames; frame wall includes simulation, render submission "
        "and present/wait; export work is outside the samples\",\n",
        BOAT_CAPTURE_SAMPLES, summary->samples, capture->total, summary->duration_seconds,
        boat_capture_truncated(capture) ? "true" : "false");
    fprintf(out,
        "    \"gpu\":{\"samples\":%u,\"missing\":%u,"
        "\"scope\":\"asynchronous GL_TIME_ELAPSED scene draw; not scan-out or driver cost\"}},\n",
        summary->gpu_samples, summary->gpu_unavailable);
    fprintf(out, "  \"target_interval_ms\":%.6g,\"hitch_count\":%u,\n",
        (double)summary->target_interval_ms, summary->hitches);
    fputs("  \"summary\":{\n", out);
    json_stats(out, "frame_ms", summary->frame_ms);
    fputs(",\n", out);
    json_stats(out, "interval_ms", summary->interval_ms);
    fputs(",\n", out);
    json_stats(out, "simulation_ms", summary->simulation_ms);
    fputs(",\n", out);
    json_stats(out, "render_ms", summary->render_ms);
    fputs(",\n", out);
    json_stats(out, "present_ms", summary->present_ms);
    fputs(",\n", out);
    json_stats(out, "gpu_ms", summary->gpu_ms);
    fputs("\n  },\n", out);
    write_segment_summaries(out, capture, summary->target_interval_ms);
    fputs("  \"limitations\":\"timings describe measured CPU submission, asynchronous GPU scene "
          "work and presentation wait; they are not scan-out latency, driver call-stack cost or a "
          "cross-machine benchmark for live capture\"\n}\n",
        out);
}

static void write_summary_json(
    FILE *out, const BoatCaptureSummary *summary, const WakePerformanceInfo *info)
{
    fprintf(out,
        "{\"format\":\"wake-velocity-performance-summary\",\"revision\":%d,"
        "\"capture_kind\":\"%s\",\"samples\":%d,\"duration_s\":%.6g,\"segments\":%u,"
        "\"target_interval_ms\":%.6g,\"hitches\":%u,\n",
        PERFORMANCE_FORMAT_REVISION, info->capture_kind ? info->capture_kind : "live",
        summary->samples, summary->duration_seconds, summary->segments,
        (double)summary->target_interval_ms, summary->hitches);
    json_stats(out, "frame_ms", summary->frame_ms);
    fputs(",\n", out);
    json_stats(out, "interval_ms", summary->interval_ms);
    fputs(",\n", out);
    json_stats(out, "simulation_ms", summary->simulation_ms);
    fputs(",\n", out);
    json_stats(out, "render_ms", summary->render_ms);
    fputs(",\n", out);
    json_stats(out, "present_ms", summary->present_ms);
    fputs(",\n", out);
    json_stats(out, "gpu_ms", summary->gpu_ms);
    fprintf(out, ",\n\"gpu_samples\":%u,\"gpu_missing\":%u}\n", summary->gpu_samples,
        summary->gpu_unavailable);
}

static void write_csv(FILE *out, const BoatCapture *capture)
{
    fputs("sample,serial,time,segment,interval_ms,frame_ms,simulation_ms,physics_ms,audio_ms,"
          "render_submit_ms,present_ms,discarded_ms,gpu_ms,has_gpu,ticks,map,preset,x,z,heading,"
          "camera_heading,speed,focused,active,paused,app_submissions,scene_triangles,"
          "shadow_submissions,reflection_submissions,main_submissions,"
          "shadow_triangles,reflection_triangles,main_triangles,"
          "map_submissions,water_submissions,object_submissions,"
          "map_triangles,water_triangles,object_triangles,"
          "wake_live,wake_peak_live,wake_emitters,traffic_wake_live\n",
        out);
    for (unsigned i = 0; i < capture->count; ++i) {
        unsigned index
            = (capture->next + BOAT_CAPTURE_SAMPLES - capture->count + i) % BOAT_CAPTURE_SAMPLES;
        const BoatCaptureSample *s = &capture->samples[index];
        fprintf(out, "%u,%llu,%.9g,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,", i, s->serial,
            s->time, s->segment, (double)s->interval_ms, (double)s->frame_ms,
            (double)s->simulation_ms, (double)s->physics_ms, (double)s->audio_ms,
            (double)s->render_ms, (double)s->present_ms, (double)s->discarded_ms);
        if (s->has_gpu)
            fprintf(out, "%.9g", (double)s->gpu_ms);
        fprintf(out, ",%d,%u,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%d,%d,%u,%zu", s->has_gpu, s->ticks,
            s->map, s->preset, (double)s->x, (double)s->z, (double)s->heading,
            (double)s->camera_heading, (double)s->speed, s->focused, s->active, s->paused,
            s->app_submissions, s->scene_triangles);
        for (int pass = 0; pass < BOAT_CAPTURE_PASSES; ++pass)
            fprintf(out, ",%u", s->pass_submissions[pass]);
        for (int pass = 0; pass < BOAT_CAPTURE_PASSES; ++pass)
            fprintf(out, ",%zu", s->pass_triangles[pass]);
        for (int group = 0; group < BOAT_CAPTURE_GROUPS; ++group)
            fprintf(out, ",%u", s->group_submissions[group]);
        for (int group = 0; group < BOAT_CAPTURE_GROUPS; ++group)
            fprintf(out, ",%zu", s->group_triangles[group]);
        fprintf(out, ",%u,%u,%u,%u\n", s->wake_live, s->wake_peak_live, s->wake_emitters,
            s->traffic_wake_live);
    }
}

// A standalone HTML summary with an inline frame-time plot; no scripts or external assets.
static void write_html(FILE *out, const BoatCapture *capture, const BoatCaptureSummary *summary,
    const WakePerformanceInfo *info, const char *timestamp)
{
    fprintf(out,
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<title>Wake Velocity performance report</title><style>"
        "body{font:14px system-ui,sans-serif;background:#101820;color:#e8eef4;margin:24px}"
        "table{border-collapse:collapse;margin:16px 0}td,th{padding:4px 10px;border-bottom:1px "
        "solid #334}code{color:#9fd}</style></head><body><h1>Wake Velocity performance report</h1>"
        "<p>%s &middot; %s &middot; %s &middot; %s</p>"
        "<p><strong>%d</strong> samples over %.2f s; %u segments; %u frames over %.1f ms.</p>",
        timestamp, info->capture_kind ? info->capture_kind : "live",
        info->map_name ? info->map_name : "unknown", info->preset_name ? info->preset_name : "low",
        summary->samples, summary->duration_seconds, summary->segments, summary->hitches,
        (double)summary->target_interval_ms);
    const BoatCaptureStats rows[] = { summary->frame_ms, summary->interval_ms,
        summary->simulation_ms, summary->render_ms, summary->present_ms, summary->gpu_ms };
    const char *names[] = { "frame wall", "frame interval", "simulation", "render submission",
        "present/wait", "GPU scene (async)" };
    fputs("<table><tr><th>Measure</th><th>p50</th><th>p95</th><th>p99</th><th>max</th>"
          "<th>samples</th></tr>",
        out);
    for (int i = 0; i < 6; ++i)
        fprintf(out,
            "<tr><td>%s</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td>"
            "<td>%d</td></tr>",
            names[i], (double)rows[i].p50, (double)rows[i].p95, (double)rows[i].p99,
            (double)rows[i].maximum, rows[i].samples);
    fputs("</table>", out);
    // Frame/interval plot over the retained window, scaled to 1000 x 240.
    double maximum = 1;
    for (unsigned i = 0; i < capture->count; ++i) {
        unsigned index
            = (capture->next + BOAT_CAPTURE_SAMPLES - capture->count + i) % BOAT_CAPTURE_SAMPLES;
        if (capture->samples[index].frame_ms > maximum)
            maximum = capture->samples[index].frame_ms;
    }
    fprintf(out,
        "<svg viewBox=\"0 0 1000 240\" width=\"100%%\" height=\"240\" "
        "style=\"background:#0b1218\"><polyline fill=\"none\" stroke=\"#ffd246\" "
        "stroke-width=\"1\" points=\"");
    for (unsigned i = 0; i < capture->count; ++i) {
        unsigned index
            = (capture->next + BOAT_CAPTURE_SAMPLES - capture->count + i) % BOAT_CAPTURE_SAMPLES;
        double x = capture->count > 1 ? 1000.0 * i / (capture->count - 1) : 0;
        double y = 235 - 230 * capture->samples[index].frame_ms / maximum;
        fprintf(out, "%.1f,%.1f ", x, y);
    }
    double target_y = 235 - 230 * summary->target_interval_ms / maximum;
    fprintf(out,
        "\"/><line x1=\"0\" y1=\"%.1f\" x2=\"1000\" y2=\"%.1f\" stroke=\"#69a\"/>"
        "</svg><p>Frame wall (yellow) and the %.1f ms target (blue). Maximum %.2f ms.</p>",
        target_y, target_y, (double)summary->target_interval_ms, maximum);
    if (summary->gpu_unavailable)
        fprintf(out, "<p>%u frames have no GPU timer sample; they stay missing, not zero.</p>",
            summary->gpu_unavailable);
    fputs("<p><code>summary.json</code>, <code>manifest.json</code> and <code>samples.csv</code> "
          "hold the machine-readable report.</p></body></html>\n",
        out);
}

static void write_build(FILE *out, const WakePerformanceInfo *info)
{
    fprintf(out,
        "Version: %s\nCommit: %s\nConfiguration: %s\nPlatform: %s\nCompiler: %s\n"
        "Capture: %s; map %s; preset %s; vsync %s; AA %s\n",
        PROTEUS_VERSION, WAKE_PERFORMANCE_REVISION, build_config_label(), WAKE_PERFORMANCE_PLATFORM,
        WAKE_PERFORMANCE_COMPILER, info->capture_kind ? info->capture_kind : "live",
        info->map_name ? info->map_name : "unknown", info->preset_name ? info->preset_name : "low",
        info->vsync ? "on" : "off", info->aa ? "on" : "off");
}

static uint32_t crc_table[256];
static bool crc_ready;

static uint32_t crc32_bytes(const unsigned char *data, size_t size)
{
    if (!crc_ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit)
                value = value & 1 ? 0xedb88320u ^ (value >> 1) : value >> 1;
            crc_table[i] = value;
        }
        crc_ready = true;
    }
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i)
        crc = crc_table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

static void put16(FILE *out, uint32_t value)
{
    fputc(value & 0xff, out);
    fputc((value >> 8) & 0xff, out);
}

static void put32(FILE *out, uint32_t value)
{
    put16(out, value);
    put16(out, value >> 16);
}

typedef struct {
    char name[64];
    unsigned char *data;
    size_t size;
    uint32_t crc, offset;
} ZipEntry;

static bool read_file(const char *path, ZipEntry *entry)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    if (fseek(file, 0, SEEK_END) != 0 || (entry->size = (size_t)ftell(file)) == (size_t)-1) {
        fclose(file);
        return false;
    }
    rewind(file);
    entry->data = malloc(entry->size ? entry->size : 1);
    if (!entry->data || fread(entry->data, 1, entry->size, file) != entry->size) {
        fclose(file);
        free(entry->data);
        entry->data = NULL;
        return false;
    }
    fclose(file);
    entry->crc = crc32_bytes(entry->data, entry->size);
    return true;
}

static bool zip_files(const char *source_directory, const char *destination)
{
    ZipEntry entries[PERFORMANCE_FILE_COUNT] = { 0 };
    bool ok = true;
    for (int i = 0; i < PERFORMANCE_FILE_COUNT; ++i) {
        snprintf(entries[i].name, sizeof(entries[i].name), "%s", performance_files[i]);
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", source_directory, entries[i].name);
        if (!read_file(path, &entries[i])) {
            ok = false;
            break;
        }
    }
    FILE *out = ok ? fopen(destination, "wb") : NULL;
    ok = out != NULL;
    uint32_t offset = 0;
    for (int i = 0; ok && i < PERFORMANCE_FILE_COUNT; ++i) {
        ZipEntry *entry = &entries[i];
        entry->offset = offset;
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0);
        put16(out, 0); // Stored: the bundle is small and portability matters more than size.
        put16(out, 0);
        put16(out, 0);
        put32(out, entry->crc);
        put32(out, (uint32_t)entry->size);
        put32(out, (uint32_t)entry->size);
        put16(out, (uint32_t)strlen(entry->name));
        put16(out, 0);
        fwrite(entry->name, 1, strlen(entry->name), out);
        fwrite(entry->data, 1, entry->size, out);
        offset += 30 + (uint32_t)strlen(entry->name) + (uint32_t)entry->size;
    }
    uint32_t central = offset;
    for (int i = 0; ok && i < PERFORMANCE_FILE_COUNT; ++i) {
        ZipEntry *entry = &entries[i];
        put32(out, 0x02014b50);
        put16(out, 20);
        put16(out, 20);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put32(out, entry->crc);
        put32(out, (uint32_t)entry->size);
        put32(out, (uint32_t)entry->size);
        put16(out, (uint32_t)strlen(entry->name));
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put32(out, 0);
        put32(out, entry->offset);
        fwrite(entry->name, 1, strlen(entry->name), out);
        offset += 46 + (uint32_t)strlen(entry->name);
    }
    if (ok) {
        put32(out, 0x06054b50);
        put16(out, 0);
        put16(out, 0);
        put16(out, PERFORMANCE_FILE_COUNT);
        put16(out, PERFORMANCE_FILE_COUNT);
        put32(out, offset - central);
        put32(out, central);
        put16(out, 0);
    }
    if (out) {
        ok = !ferror(out);
        ok = fclose(out) == 0 && ok;
    }
    for (int i = 0; i < PERFORMANCE_FILE_COUNT; ++i)
        free(entries[i].data);
    return ok;
}

// Creates every missing component of PATH. An existing final directory is fine; the export
// rejects an existing destination before calling this.
static bool make_directories(const char *path)
{
    char partial[4096];
    snprintf(partial, sizeof(partial), "%s", path);
    for (char *slash = partial + 1; *slash; ++slash) {
        if (*slash != '/')
            continue;
        *slash = 0;
        if (make_dir(partial) != 0 && errno != EEXIST)
            return false;
        *slash = '/';
    }
    return make_dir(partial) == 0 || errno == EEXIST;
}

static bool path_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file) {
        fclose(file);
        return true;
    }
    return errno != ENOENT;
}

bool wake_performance_export(const BoatCapture *capture, const WakePerformanceInfo *info,
    const char *destination, char *written, size_t written_size, char *error, size_t error_size)
{
    if (!capture || !info || !destination || !*destination) {
        copy_text(error, error_size, "no report destination");
        return false;
    }
    if (path_exists(destination)) {
        copy_text(
            error, error_size, "report destination already exists; no bundle was overwritten");
        return false;
    }
    size_t length = strlen(destination);
    bool zip = length > 4 && strcmp(destination + length - 4, ".zip") == 0;
    char directory[4096];
    if (zip) {
        if (length + 5 >= sizeof(directory)) {
            copy_text(error, error_size, "report path is too long");
            return false;
        }
        snprintf(directory, sizeof(directory), "%s.tmp", destination);
        if (path_exists(directory)) {
            copy_text(error, error_size, "temporary report directory already exists");
            return false;
        }
    } else {
        snprintf(directory, sizeof(directory), "%s", destination);
    }
    if (!make_directories(directory)) {
        copy_text(error, error_size, "cannot create the report directory");
        return false;
    }
    char timestamp[64];
    timestamp_utc(timestamp, sizeof(timestamp));
    BoatCaptureSummary summary = boat_capture_summary(capture,
        info->monitor_refresh_hz > 0 ? 1000.0f / (float)info->monitor_refresh_hz : 1000.0f / 60.0f);
    bool ok = true;
    for (int file_index = 0; ok && file_index < PERFORMANCE_FILE_COUNT; ++file_index) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", directory, performance_files[file_index]);
        FILE *file = fopen(path, "wb");
        if (!file) {
            ok = false;
            break;
        }
        switch (file_index) {
        case 0:
            write_manifest(file, capture, info, &summary, timestamp);
            break;
        case 1:
            write_summary_json(file, &summary, info);
            break;
        case 2:
            write_csv(file, capture);
            break;
        case 3:
            write_html(file, capture, &summary, info, timestamp);
            break;
        default:
            write_build(file, info);
            break;
        }
        ok = !ferror(file);
        ok = fclose(file) == 0 && ok;
    }
    if (!ok) {
        copy_text(error, error_size,
            "cannot write the performance report; the partial folder "
            "was left in place");
        copy_text(written, written_size, directory);
        return false;
    }
    if (!zip) {
        copy_text(written, written_size, directory);
        copy_text(error, error_size, "");
        return true;
    }
    bool archived = zip_files(directory, destination);
    if (!archived) {
        copy_text(error, error_size,
            "cannot archive the report; the uncompressed folder was "
            "kept for manual copying");
        copy_text(written, written_size, directory);
        return false;
    }
    for (int i = 0; i < PERFORMANCE_FILE_COUNT; ++i) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", directory, performance_files[i]);
        remove(path);
    }
    remove(directory);
    copy_text(written, written_size, destination);
    copy_text(error, error_size, "");
    return true;
}

bool wake_performance_default_directory(char *out, size_t size)
{
#if defined(_WIN32)
    const char *home = getenv("USERPROFILE");
    if (!home || !*home)
        return false;
    snprintf(out, size, "%s/Documents/Wake Velocity/performance", home);
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (!home || !*home)
        return false;
    snprintf(out, size, "%s/Library/Application Support/Wake Velocity/performance", home);
#else
    const char *data = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (data && *data)
        snprintf(out, size, "%s/wake-velocity/performance", data);
    else if (home && *home)
        snprintf(out, size, "%s/.local/share/wake-velocity/performance", home);
    else
        return false;
#endif
    // Create the user directories lazily; failure is reported by the export that needs them.
    return true;
}

bool wake_performance_default_name(const char *directory, char *out, size_t size, bool zip)
{
    time_t now = time(NULL);
    struct tm local;
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    snprintf(out, size, "%s/performance-%s%s", directory, stamp, zip ? ".zip" : "");
    return true;
}
