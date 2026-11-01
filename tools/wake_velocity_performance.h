#ifndef PROTEUS_WAKE_VELOCITY_PERFORMANCE_H
#define PROTEUS_WAKE_VELOCITY_PERFORMANCE_H

#include "boat_capture.h"

#include <stdbool.h>
#include <stddef.h>

// G0w live/scripted performance report. The retained samples are summarized and written as a
// portable bundle: manifest.json, summary.json, samples.csv, summary.html and build.txt. File
// I/O and GL metadata stay in this application owner, outside the simulator, network and the
// sampling helper (boat_capture.c).
typedef struct {
    const char *capture_kind; // "live" for normal play, "scripted" for a driven run.
    const char *map_name;
    const char *preset_name;
    bool vsync, aa, fullscreen;
    bool waves, wake_objects, lighting, shadows, reflections;
    int window_width, window_height;
    int render_width, render_height;
    int scene_width, scene_height;
    int reflection_width, reflection_height;
    int monitor_refresh_hz;
} WakePerformanceInfo;

// Writes a new bundle to DESTINATION, which must not exist. A ".zip" destination leaves only
// the archive; any other path becomes a directory containing the files. Returns false with
// ERROR set (and never overwrites an older bundle). WRITTEN receives the actual path.
bool wake_performance_export(const BoatCapture *capture, const WakePerformanceInfo *info,
    const char *destination, char *written, size_t written_size, char *error, size_t error_size);

// Per-user report directory. False when the platform exposes no writable home.
bool wake_performance_default_directory(char *out, size_t size);
// A timestamped bundle name inside DIRECTORY, e.g. performance-20261101-153000.
bool wake_performance_default_name(
    const char *directory, char *out, size_t size, bool zip);

#endif
