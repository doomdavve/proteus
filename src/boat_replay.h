#ifndef PROTEUS_BOAT_REPLAY_H
#define PROTEUS_BOAT_REPLAY_H

#include "boat_presentation.h"
#include "boat_view_state.h"

#define BOAT_REPLAY_MAX_SECONDS 600
// Recorded world time is absolute session time, so a course may start long after launch.
#define BOAT_REPLAY_MAX_WORLD_SECONDS (100.0 * 365 * 86400)
#define BOAT_REPLAY_MAX_SAMPLES (BOAT_REPLAY_MAX_SECONDS * BOAT_TICK_HZ + 2)

typedef struct {
    double at;
    Vector2 position, velocity;
    float heading, height, pitch, roll, engine_angle;
    float wheel, throttle, engine_response, camera_heading;
    double world_time;
} BoatReplaySample;

// Zero-initialize; owns samples. A recording contains only START through FINISH.
typedef struct {
    BoatReplaySample *samples;
    size_t count, capacity;
    double splits[BOAT_COURSE_GATES];
    bool waves, complete, failed;
} BoatReplay;

// Physical pose at world TIME plus the presentation's camera follow, recorded as a view value.
BoatReplaySample boat_replay_pose(
    const Boat *boat, const BoatPresentation *presentation, double time);
// Called after each course tick with the poses immediately before and after it and the
// course after it. WAVES is the recording world's displaced water.
void boat_replay_record(BoatReplay *run, BoatReplaySample before, BoatReplaySample after,
    const BoatCourse *course, bool waves);
// Water a recording was made in. View a sample in its own world, never the live one.
BoatWater boat_replay_water(const BoatReplay *run);
// The view at AT, with recorded world time and course progress. PRESENTATION, when given, is
// reset to the recorded camera follow; its cloth comes from boat_replay_animate_flag().
bool boat_replay_sample(
    const BoatReplay *run, double at, BoatViewState *view, BoatPresentation *presentation);
// Caller-owned visual history, separate for playback and ghost. Zero-initialize.
// Continuous playback advances at fixed ticks; jumps reconstruct at most one second.
// Seek cloth is an approximation, not additional recorded physics or a file-format change.
typedef struct {
    const BoatReplay *run;
    const BoatReplaySample *samples;
    double start_time;
    BoatFlag flag;
    int tick;
} BoatReplayFlag;
void boat_replay_animate_flag(
    const BoatReplay *run, const BoatViewState *view, BoatReplayFlag *cache, BoatFlag *flag);
bool boat_replay_copy(BoatReplay *destination, const BoatReplay *source);
void boat_replay_destroy(BoatReplay *run);
// Versioned portable text; load preserves destination on failure, save atomically replaces path.
bool boat_replay_load(BoatReplay *run, const char *path);
bool boat_replay_save(const BoatReplay *run, const char *path);
bool boat_replay_data_directory(char *path, size_t size);
// PD1: the saved best path. The standard course keeps its established
// "course-{calm|waves}-physicsN.run" name, so existing bests stay valid; any other course adds
// its digest. False when the path does not fit.
bool boat_replay_best_path(
    char *path, size_t size, const char *directory, bool waves, uint64_t course_digest);

// Race-session state survives boat resets; opponent is pinned until the next attempt.
// A finish only updates memory and marks one pending save/export of best/last; the
// owner writes it with boat_replay_runs_flush() outside the tick loop. Only a reset
// replaces last or best, and it flushes first, so a pending request cannot lose its run.
typedef struct {
    BoatReplay recording, last, best, opponent, reference;
    const BoatReplay *playing;
    double replay_time;
    int replay_speed_index; // 0..5: quarter, half, normal, double, quadruple, octuple speed.
    bool paused, ghost_visible, finished;
    bool practice; // Latched for the attempt after altered live time; no best/save/export.
    bool save_best, export_last, report_finish; // The pending finish request.
    char best_path[2048];
    const char *export_path;
    const char *notice;
} BoatReplayRuns;
// Transport controls change playback only; recorded samples and personal bests are untouched.
bool boat_replay_play(BoatReplayRuns *runs, const BoatReplay *run);
void boat_replay_seek(BoatReplayRuns *runs, double at);
float boat_replay_speed(const BoatReplayRuns *runs);
void boat_replay_speed_step(BoatReplayRuns *runs, int direction);
// Frame delta is capped at 0.1 seconds, as in live simulation. Finish pauses playback.
void boat_replay_advance(BoatReplayRuns *runs, double seconds, bool focused);
// Releases recordings without writing; flush first to keep a pending finish.
void boat_replay_runs_destroy(BoatReplayRuns *runs);
// Flushes a pending finish, then starts a new attempt against the pinned opponent.
void boat_replay_runs_reset(BoatReplayRuns *runs);
// Record one course tick (see boat_replay_record) and settle a finish in memory. No file I/O.
void boat_replay_runs_step(BoatReplayRuns *runs, BoatReplaySample before, BoatReplaySample after,
    const BoatCourse *course, bool waves);
// Write the pending best/export and report the finish notice; nothing when none is pending.
void boat_replay_runs_flush(BoatReplayRuns *runs);
// Replace best and best_path with those of COURSE_DIGEST; flush first. False, with a notice, when
// that course's saved best is damaged or for other water. The opponent changes at the next reset.
bool boat_replay_runs_select_best(
    BoatReplayRuns *runs, const char *directory, bool waves, uint64_t course_digest);

#endif
