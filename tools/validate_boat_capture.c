/* G0w capture and export contract: bounded ring ordering, percentiles, segmentation, serial
 * lookup after eviction, missing GPU samples, and portable directory/ZIP bundles. */
#include "boat_capture.h"
#include "wake_velocity_performance.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#define test_mkdir(path) _mkdir(path)
#define test_rmdir(path) _rmdir(path)
#define test_getpid() _getpid()
#else
#include <unistd.h>
#define test_mkdir(path) mkdir(path, 0777)
#define test_rmdir(path) rmdir(path)
#define test_getpid() getpid()
#endif

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static BoatCaptureSample sample(unsigned i)
{
    return (BoatCaptureSample) { .time = i * .01,
        .serial = i,
        .interval_ms = (float)(i % 7 == 0 ? 20 : 10),
        .frame_ms = (float)(10 + i % 5),
        .simulation_ms = 1,
        .render_ms = 2,
        .present_ms = 3,
        .has_gpu = i % 3 != 0,
        .gpu_ms = (float)(i % 3 ? 4 : 0),
        .segment = i > 900 };
}

static int ring(void)
{
    static BoatCapture capture;
    memset(&capture, 0, sizeof(capture));
    for (unsigned i = 0; i < BOAT_CAPTURE_SAMPLES + 50; ++i) {
        BoatCaptureSample s = sample(i);
        boat_capture_add(&capture, &s);
    }
    CHECK(capture.count == BOAT_CAPTURE_SAMPLES && capture.total == BOAT_CAPTURE_SAMPLES + 50);
    CHECK(!boat_capture_truncated(&capture));
    CHECK(capture.first_time == 50 * .01);
    CHECK(boat_capture_sample(&capture, 49) == NULL);
    CHECK(boat_capture_sample(&capture, 50) && boat_capture_sample(&capture, 50)->serial == 50);
    CHECK(boat_capture_sample(&capture, BOAT_CAPTURE_SAMPLES + 49)
        && boat_capture_sample(&capture, BOAT_CAPTURE_SAMPLES + 49)->serial
            == BOAT_CAPTURE_SAMPLES + 49);
    BoatCaptureSummary summary = boat_capture_summary(&capture, 10);
    CHECK(summary.samples == BOAT_CAPTURE_SAMPLES);
    CHECK((unsigned)summary.interval_ms.samples == BOAT_CAPTURE_SAMPLES - 1u);
    CHECK(summary.gpu_samples + summary.gpu_unavailable == (unsigned)BOAT_CAPTURE_SAMPLES);
    CHECK((unsigned)summary.gpu_ms.samples == summary.gpu_samples);
    CHECK(summary.hitches > 0);
    CHECK(summary.segments == 2);
    CHECK(summary.duration_seconds > 0);
    // p50 of a sorted [10..14] frame distribution sits on its middle element.
    CHECK(summary.frame_ms.p50 >= 11 && summary.frame_ms.p50 <= 13);
    CHECK(summary.frame_ms.p95 <= 14 && summary.frame_ms.maximum == 14);
    boat_capture_segment(&capture);
    boat_capture_segment(&capture);
    BoatCaptureSample later = sample(BOAT_CAPTURE_SAMPLES + 50);
    later.segment = capture.segment;
    boat_capture_add(&capture, &later);
    CHECK(boat_capture_summary(&capture, 10).segments == 3);
    return 0;
}

static int startup(void)
{
    static BoatCapture capture;
    memset(&capture, 0, sizeof(capture));
    BoatCaptureSample s = sample(0);
    s.interval_ms = 0;
    boat_capture_add(&capture, &s);
    CHECK(boat_capture_truncated(&capture));
    BoatCaptureSummary summary = boat_capture_summary(&capture, 10);
    CHECK(summary.samples == 1 && summary.interval_ms.samples == 0);
    CHECK(summary.hitches == 0 && summary.gpu_unavailable == 1);
    BoatCaptureSummary empty = boat_capture_summary(&capture, 10);
    (void)empty;
    return 0;
}

static int bundle(const char *base)
{
    char directory[1024];
    snprintf(directory, sizeof(directory), "%s/report", base);
    static BoatCapture capture;
    memset(&capture, 0, sizeof(capture));
    for (unsigned i = 0; i < 40; ++i) {
        BoatCaptureSample s = sample(i);
        boat_capture_add(&capture, &s);
    }
    WakePerformanceInfo info = { .capture_kind = "scripted",
        .map_name = "river",
        .preset_name = "default",
        .vsync = true,
        .window_width = 960,
        .window_height = 600,
        .render_width = 960,
        .render_height = 600,
        .scene_width = 320,
        .scene_height = 200,
        .reflection_width = 160,
        .reflection_height = 120,
        .monitor_refresh_hz = 60 };
    char written[1024], error[256];
    CHECK(wake_performance_export(
        &capture, &info, directory, written, sizeof(written), error, sizeof(error)));
    CHECK(strcmp(written, directory) == 0);
    const char *files[]
        = { "manifest.json", "summary.json", "samples.csv", "summary.html", "build.txt" };
    for (int i = 0; i < 5; ++i) {
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", directory, files[i]);
        FILE *file = fopen(path, "rb");
        CHECK(file != NULL);
        fseek(file, 0, SEEK_END);
        CHECK(ftell(file) > 0);
        fclose(file);
    }
    // A second export must not overwrite the first bundle.
    CHECK(!wake_performance_export(
        &capture, &info, directory, written, sizeof(written), error, sizeof(error)));
    CHECK(strstr(error, "already exists"));
    char archive[1024];
    snprintf(archive, sizeof(archive), "%s/report.zip", base);
    CHECK(wake_performance_export(
        &capture, &info, archive, written, sizeof(written), error, sizeof(error)));
    CHECK(strcmp(written, archive) == 0);
    FILE *zip = fopen(archive, "rb");
    CHECK(zip != NULL);
    unsigned char header[4] = { 0 };
    CHECK(fread(header, 1, 4, zip) == 4);
    fclose(zip);
    CHECK(header[0] == 'P' && header[1] == 'K' && header[2] == 3 && header[3] == 4);
    // Clean up so repeated local runs start fresh.
    for (int i = 0; i < 5; ++i) {
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", directory, files[i]);
        remove(path);
    }
    test_rmdir(directory);
    remove(archive);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    CHECK(ring() == 0);
    CHECK(startup() == 0);
    char base[1024];
    snprintf(base, sizeof(base), "%s/capture-%ld", argv[1], (long)test_getpid());
    if (test_mkdir(base) != 0 && strcmp(strerror(errno), "File exists") != 0)
        return 1;
    CHECK(bundle(base) == 0);
    test_rmdir(base);
    puts("boat_capture=ok ring, segments, missing GPU samples and portable bundles");
    return 0;
}
