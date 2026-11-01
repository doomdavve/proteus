#include "boat_replay.h"
#include "boat_pose.h"
#include "boat_time_control.h"
#include "raymath.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

// Bump these when the file format or course changes. Handling/water revision lives in the header
// so automatic best filenames and recording compatibility use the same value.
#define REPLAY_VERSION 2
#define COURSE_REVISION 1

BoatReplaySample boat_replay_pose(const Boat *b, const BoatPresentation *presentation, double time)
{
    return (BoatReplaySample) { .position = b->position,
        .velocity = b->velocity,
        .heading = b->heading,
        .height = b->height,
        .pitch = b->pitch,
        .roll = b->roll,
        .engine_angle = b->engine_angle,
        .wheel = b->wheel,
        .throttle = b->throttle,
        .engine_response = b->engine_response,
        .camera_heading = presentation->camera_heading,
        .world_time = time };
}

static float angle(float a, float b, float t) { return a + remainderf(b - a, 2 * PI) * t; }

static BoatReplaySample interpolate(BoatReplaySample a, BoatReplaySample b, double at, float t)
{
    BoatReplaySample result = { .at = at };
#define BLEND(field) result.field = a.field + (b.field - a.field) * t
    BLEND(position.x);
    BLEND(position.y);
    BLEND(velocity.x);
    BLEND(velocity.y);
    BLEND(height);
    BLEND(pitch);
    BLEND(roll);
    BLEND(engine_angle);
    BLEND(wheel);
    BLEND(throttle);
    BLEND(engine_response);
    BLEND(world_time);
#undef BLEND
    result.heading = angle(a.heading, b.heading, t);
    result.camera_heading = angle(a.camera_heading, b.camera_heading, t);
    return result;
}

static bool append(BoatReplay *run, BoatReplaySample sample)
{
    if (run->count >= BOAT_REPLAY_MAX_SAMPLES || sample.at > BOAT_REPLAY_MAX_SECONDS)
        return false;
    if (run->count == run->capacity) {
        size_t capacity = run->capacity ? run->capacity * 2 : 1024;
        if (capacity > BOAT_REPLAY_MAX_SAMPLES)
            capacity = BOAT_REPLAY_MAX_SAMPLES;
        BoatReplaySample *samples = realloc(run->samples, capacity * sizeof(*samples));
        if (!samples)
            return false;
        run->samples = samples;
        run->capacity = capacity;
    }
    run->samples[run->count++] = sample;
    return true;
}

void boat_replay_record(BoatReplay *run, BoatReplaySample before, BoatReplaySample after,
    const BoatCourse *c, bool waves)
{
    if (!c->enabled || run->complete || run->failed || c->next_gate == 0)
        return;
    if (!run->count) {
        if (c->crossing[0] < 0)
            return;
        run->waves = waves;
        if (!append(run, interpolate(before, after, 0, (float)c->crossing[0])))
            run->failed = true;
    }
    if (run->failed)
        return;
    bool finish = c->next_gate == BOAT_COURSE_GATES;
    BoatReplaySample sample = finish
        ? interpolate(before, after, c->elapsed, (float)c->crossing[BOAT_COURSE_GATES - 1])
        : after;
    sample.at = c->elapsed;
    // An exact tick-boundary start already has this pose.
    if (sample.at > run->samples[run->count - 1].at && !append(run, sample))
        run->failed = true;
    memcpy(run->splits, c->splits, sizeof(run->splits));
    run->complete = finish && !run->failed && run->count >= 2;
}

BoatWater boat_replay_water(const BoatReplay *run)
{
    return run->waves ? boat_water_gentle() : (BoatWater) { 0 };
}

bool boat_replay_sample(
    const BoatReplay *run, double at, BoatViewState *view, BoatPresentation *presentation)
{
    if (!run->count || !isfinite(at))
        return false;
    size_t lo = 0, hi = run->count - 1;
    at = fmax(0, fmin(at, run->samples[hi].at));
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (run->samples[mid].at <= at)
            lo = mid;
        else
            hi = mid;
    }
    double span = run->samples[hi].at - run->samples[lo].at;
    BoatReplaySample s = interpolate(run->samples[lo], run->samples[hi], at,
        span > 0 ? (float)((at - run->samples[lo].at) / span) : 0);
    *view = (BoatViewState) { .boat = { .position = s.position,
                                  .velocity = s.velocity,
                                  .heading = s.heading,
                                  .height = s.height,
                                  .pitch = s.pitch,
                                  .roll = s.roll,
                                  .engine_angle = s.engine_angle,
                                  .wheel = s.wheel,
                                  .throttle = s.throttle,
                                  .engine_response = s.engine_response },
        .time = s.world_time,
        .course = { .enabled = true, .elapsed = at, .next_gate = 1 } };
    memcpy(view->course.splits, run->splits, sizeof(run->splits));
    for (int i = 1; i < BOAT_COURSE_GATES; ++i)
        if (at >= run->splits[i])
            view->course.next_gate = i + 1;
    if (presentation) {
        boat_presentation_reset(presentation);
        presentation->camera_heading = s.camera_heading;
    }
    return true;
}

void boat_replay_animate_flag(
    const BoatReplay *run, const BoatViewState *view, BoatReplayFlag *cache, BoatFlag *flag)
{
    if (!run->count)
        return;
    double end = view->course.elapsed;
    int last = (int)floor(end / BOAT_TICK_SECONDS);
    bool restart = cache->run != run || cache->samples != run->samples
        || cache->start_time != run->samples[0].world_time || last < cache->tick
        || last - cache->tick > 24 || !cache->flag.initialized;
    int first = cache->tick + 1;
    if (restart) {
        cache->flag = (BoatFlag) { 0 };
        first = last > 120 ? last - 120 : 0;
    }
    BoatViewState historical;
    for (int tick = first; tick <= last; ++tick) {
        boat_replay_sample(run, tick * BOAT_TICK_SECONDS, &historical, NULL);
        boat_presentation_step_flag(&cache->flag, &historical.boat, historical.time);
    }
    cache->run = run;
    cache->samples = run->samples;
    cache->start_time = run->samples[0].world_time;
    cache->tick = last;
    *flag = cache->flag;
    // Presentation may lie between simulation ticks. Translate cloth to the exact current
    // hoist, then pin each row to the current attitude; retain tick state in the cache.
    Transform3D hull = boat_pose_parts(&view->boat).hull;
    Vector3 offset = Vector3Subtract(boat_flag_attachment(hull, 0), flag->surface.points[0]);
    for (int i = 0; i < BOAT_FLAG_POINTS; ++i)
        flag->surface.points[i] = Vector3Add(flag->surface.points[i], offset);
    for (int row = 0; row < BOAT_FLAG_ROWS; ++row)
        flag->surface.points[row * BOAT_FLAG_COLS] = boat_flag_attachment(hull, row);
}

void boat_replay_destroy(BoatReplay *run)
{
    free(run->samples);
    *run = (BoatReplay) { 0 };
}

bool boat_replay_copy(BoatReplay *destination, const BoatReplay *source)
{
    if (destination == source)
        return true;
    BoatReplay copy = *source;
    copy.samples = NULL;
    copy.capacity = copy.count;
    if (copy.count) {
        copy.samples = malloc(copy.count * sizeof(*copy.samples));
        if (!copy.samples)
            return false;
        memcpy(copy.samples, source->samples, copy.count * sizeof(*copy.samples));
    }
    boat_replay_destroy(destination);
    *destination = copy;
    return true;
}

static bool valid(const BoatReplay *run)
{
    if (!run->complete || run->count < 2 || run->count > BOAT_REPLAY_MAX_SAMPLES
        || run->samples[0].at != 0 || run->splits[0] != 0)
        return false;
    double duration = run->samples[run->count - 1].at;
    if (!isfinite(duration) || duration <= 0 || duration > BOAT_REPLAY_MAX_SECONDS
        || run->splits[BOAT_COURSE_GATES - 1] != duration)
        return false;
    for (int i = 1; i < BOAT_COURSE_GATES; ++i)
        if (!isfinite(run->splits[i]) || run->splits[i] <= run->splits[i - 1])
            return false;
    for (size_t i = 0; i < run->count; ++i) {
        BoatReplaySample s = run->samples[i];
        const float values[] = { s.position.x, s.position.y, s.velocity.x, s.velocity.y, s.heading,
            s.height, s.pitch, s.roll, s.engine_angle, s.wheel, s.throttle, s.engine_response,
            s.camera_heading };
        if (!isfinite(s.world_time) || s.world_time < 0
            || s.world_time > BOAT_REPLAY_MAX_WORLD_SECONDS)
            return false;
        for (size_t j = 0; j < sizeof(values) / sizeof(values[0]); ++j)
            if (!isfinite(values[j]) || fabsf(values[j]) > 100000)
                return false;
        // Pose recordings allow stress excursions, but not inverted/arbitrary attitudes.
        if (fabsf(s.roll) > PI / 2 || fabsf(s.pitch) > PI / 2)
            return false;
        if (!isfinite(s.at)
            || (i
                && (s.at <= run->samples[i - 1].at
                    || s.at - run->samples[i - 1].at > BOAT_TICK_SECONDS * 1.01)))
            return false;
    }
    return true;
}

bool boat_replay_load(BoatReplay *run, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return false;
    BoatReplay loaded = { 0 };
    int version, course, physics, waves;
    double step;
    size_t count;
    bool ok = fscanf(file, "PROTEUS_RUN %d %d %d %lf %d %zu", &version, &course, &physics, &step,
                  &waves, &count)
            == 6
        && version == REPLAY_VERSION && course == COURSE_REVISION
        && physics == BOAT_PHYSICS_REVISION && step == BOAT_TICK_SECONDS
        && (waves == 0 || waves == 1) && count >= 2 && count <= BOAT_REPLAY_MAX_SAMPLES;
    if (ok) {
        loaded.waves = waves != 0;
        loaded.complete = true;
        for (int i = 0; i < BOAT_COURSE_GATES; ++i)
            if (fscanf(file, "%lf", &loaded.splits[i]) != 1)
                ok = false;
    }
    for (size_t i = 0; ok && i < count; ++i) {
        BoatReplaySample s = { 0 };
        ok = fscanf(file, "%lf %f %f %f %f %f %f %f %f %f %f %f %f %f %lf", &s.at, &s.position.x,
                 &s.position.y, &s.velocity.x, &s.velocity.y, &s.heading, &s.height, &s.pitch,
                 &s.roll, &s.engine_angle, &s.wheel, &s.throttle, &s.engine_response,
                 &s.camera_heading, &s.world_time)
                == 15
            && append(&loaded, s);
    }
    int trailing;
    while (ok && (trailing = fgetc(file)) != EOF)
        if (!isspace((unsigned char)trailing))
            ok = false;
    ok = ok && !ferror(file) && valid(&loaded);
    fclose(file);
    if (ok) {
        boat_replay_destroy(run);
        *run = loaded;
    } else
        boat_replay_destroy(&loaded);
    return ok;
}

bool boat_replay_save(const BoatReplay *run, const char *path)
{
    if (!valid(run))
        return false;
    size_t length = strlen(path) + 5;
    char *temporary = malloc(length);
    if (!temporary)
        return false;
    snprintf(temporary, length, "%s.tmp", path);
    FILE *file = fopen(temporary, "w");
    bool ok = file != NULL;
    if (file) {
        ok = fprintf(file, "PROTEUS_RUN %d %d %d %.17g %d %zu\n", REPLAY_VERSION, COURSE_REVISION,
                 BOAT_PHYSICS_REVISION, BOAT_TICK_SECONDS, run->waves, run->count)
            > 0;
        for (int i = 0; i < BOAT_COURSE_GATES; ++i)
            if (fprintf(file, "%.17g%c", run->splits[i], i == BOAT_COURSE_GATES - 1 ? '\n' : ' ')
                < 0)
                ok = false;
        for (size_t i = 0; ok && i < run->count; ++i) {
            BoatReplaySample s = run->samples[i];
            ok = fprintf(file,
                     "%.17g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g "
                     "%.17g\n",
                     s.at, s.position.x, s.position.y, s.velocity.x, s.velocity.y, s.heading,
                     s.height, s.pitch, s.roll, s.engine_angle, s.wheel, s.throttle,
                     s.engine_response, s.camera_heading, s.world_time)
                > 0;
        }
        if (fclose(file) != 0)
            ok = false;
    }
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = rename(temporary, path) == 0;
#endif
    }
    if (!ok)
        remove(temporary);
    free(temporary);
    return ok;
}

bool boat_replay_data_directory(char *path, size_t size)
{
    const char *base = NULL, *suffix = NULL;
#ifdef _WIN32
    base = getenv("LOCALAPPDATA");
    suffix = "/Proteus";
#elif defined(__APPLE__)
    base = getenv("HOME");
    suffix = "/Library/Application Support/Proteus";
#else
    base = getenv("XDG_DATA_HOME");
    suffix = "/proteus";
    if (!base || !base[0]) {
        base = getenv("HOME");
        suffix = "/.local/share/proteus";
    }
#endif
    if (!base || !base[0])
        return false;
    int n = snprintf(path, size, "%s%s", base, suffix);
    return n >= 0 && (size_t)n < size;
}

bool boat_replay_best_path(
    char *path, size_t size, const char *directory, bool waves, uint64_t course_digest)
{
    int n;
    if (course_digest == BOAT_COURSE_STANDARD_DIGEST)
        n = snprintf(path, size, "%s/course-%s-physics%d.run", directory, waves ? "waves" : "calm",
            BOAT_PHYSICS_REVISION);
    else
        n = snprintf(path, size, "%s/course-%s-physics%d-%016llx.run", directory,
            waves ? "waves" : "calm", BOAT_PHYSICS_REVISION, (unsigned long long)course_digest);
    return n >= 0 && (size_t)n < size;
}

bool boat_replay_play(BoatReplayRuns *runs, const BoatReplay *run)
{
    if (!run || !run->complete || run->count < 2)
        return false;
    runs->playing = run;
    runs->replay_time = 0;
    runs->replay_speed_index = 2;
    runs->paused = false;
    return true;
}

void boat_replay_seek(BoatReplayRuns *runs, double at)
{
    if (!runs->playing || !isfinite(at))
        return;
    double duration = runs->playing->splits[BOAT_COURSE_GATES - 1];
    runs->replay_time = fmax(0, fmin(at, duration));
    if (runs->replay_time == duration)
        runs->paused = true;
}

float boat_replay_speed(const BoatReplayRuns *runs)
{
    return boat_time_rate(runs->replay_speed_index);
}

void boat_replay_speed_step(BoatReplayRuns *runs, int direction)
{
    if (!runs->playing)
        return;
    runs->replay_speed_index = boat_time_speed_step(runs->replay_speed_index, direction);
}

void boat_replay_advance(BoatReplayRuns *runs, double seconds, bool focused)
{
    if (!runs->playing || runs->paused || !focused || !isfinite(seconds) || seconds <= 0)
        return;
    boat_replay_seek(runs, runs->replay_time + fmin(seconds, .1) * boat_replay_speed(runs));
}

void boat_replay_runs_destroy(BoatReplayRuns *runs)
{
    boat_replay_destroy(&runs->recording);
    boat_replay_destroy(&runs->last);
    boat_replay_destroy(&runs->best);
    boat_replay_destroy(&runs->opponent);
    boat_replay_destroy(&runs->reference);
}

void boat_replay_runs_reset(BoatReplayRuns *runs)
{
    boat_replay_runs_flush(runs);
    runs->playing = NULL;
    runs->replay_time = 0;
    runs->replay_speed_index = 2;
    runs->paused = false;
    runs->finished = false;
    runs->practice = false;
    boat_replay_destroy(&runs->recording);
    const BoatReplay *source = runs->reference.complete ? &runs->reference : &runs->best;
    if (!boat_replay_copy(&runs->opponent, source)) {
        boat_replay_destroy(&runs->opponent);
        runs->notice = "Could not allocate ghost recording";
    }
}

void boat_replay_runs_step(BoatReplayRuns *runs, BoatReplaySample before, BoatReplaySample after,
    const BoatCourse *course, bool waves)
{
    if (runs->finished)
        return;
    bool failed = runs->recording.failed;
    boat_replay_record(&runs->recording, before, after, course, waves);
    if (!failed && runs->recording.failed)
        runs->notice = "Recording stopped (memory or 10 minute limit); best preserved";
    if (!runs->recording.complete)
        return;
    boat_replay_destroy(&runs->last);
    runs->last = runs->recording;
    runs->recording = (BoatReplay) { 0 };
    runs->finished = true;
    bool faster = !runs->practice
        && (!runs->best.complete
            || runs->last.splits[BOAT_COURSE_GATES - 1] < runs->best.splits[BOAT_COURSE_GATES - 1]);
    runs->notice = runs->practice ? "Practice recorded; best and export preserved"
        : faster                  ? "New personal best!"
                                  : "Run recorded";
    if (faster) {
        if (!boat_replay_copy(&runs->best, &runs->last))
            runs->notice = "Run recorded; could not allocate personal best";
        else
            runs->save_best = true;
    }
    runs->export_last = !runs->practice && runs->export_path != NULL;
    runs->report_finish = true;
}

bool boat_replay_runs_select_best(
    BoatReplayRuns *runs, const char *directory, bool waves, uint64_t course_digest)
{
    boat_replay_destroy(&runs->best);
    if (!boat_replay_best_path(
            runs->best_path, sizeof(runs->best_path), directory, waves, course_digest)) {
        runs->best_path[0] = 0;
        return true;
    }
    if (!FileExists(runs->best_path))
        return true;
    if (!boat_replay_load(&runs->best, runs->best_path) || runs->best.waves != waves) {
        boat_replay_destroy(&runs->best);
        runs->notice = "Saved best is incompatible or damaged; ignored";
        return false;
    }
    return true;
}

void boat_replay_runs_flush(BoatReplayRuns *runs)
{
    if (runs->save_best
        && (!runs->best_path[0] || MakeDirectory(GetDirectoryPath(runs->best_path)) != 0
            || !boat_replay_save(&runs->best, runs->best_path)))
        runs->notice = "New best kept in memory; could not save to disk";
    if (runs->export_last && !boat_replay_save(&runs->last, runs->export_path))
        runs->notice = "Run recorded; could not export replay";
    if (runs->report_finish && runs->notice)
        fprintf(stderr, "%s\n", runs->notice);
    runs->save_best = runs->export_last = runs->report_finish = false;
}
