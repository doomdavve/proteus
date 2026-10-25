/* Finite scripted wake-return workload; no window, audio, replay or timed I/O. */
#include "boat_test_world.h"
#include "wake_velocity_state.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t hash, births;
    float time, x, z, vx, vz, height, vertical_velocity;
    unsigned motion, landings, live;
} StateTrace;

static int compare(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static int fail(const char *message)
{
    fprintf(stderr, "boat_benchmark: %s\n", message);
    return 1;
}
static bool count(const char *s, unsigned *out)
{
    if (!*s || *s < '0' || *s > '9')
        return false;
    char *end;
    errno = 0;
    unsigned long n = strtoul(s, &end, 10);
    if (errno || *end || n > 100000)
        return false;
    *out = (unsigned)n;
    return true;
}
int main(int argc, char **argv)
{
    test_worlds_open();
    unsigned warmup = 120, frames = 1800;
    const char *output = NULL;
    bool presentation = true; // Cosmetics on by default, as in the P3a baseline.
    for (int i = 1; i < argc; ++i) {
        const char *key = argv[i];
        if (++i == argc)
            return fail("every option requires a value");
        const char *v = argv[i];
        if (!strcmp(key, "--scenario")) {
            if (strcmp(v, "wake-loop"))
                return fail("unsupported scenario");
        } else if (!strcmp(key, "--mode")) {
            if (strcmp(v, "simulation"))
                return fail("only simulation mode is supported");
        } else if (!strcmp(key, "--renderer")) {
            if (strcmp(v, "none"))
                return fail("only renderer none is supported");
        } else if (!strcmp(key, "--warmup")) {
            if (!count(v, &warmup))
                return fail("warmup must be 0..100000");
        } else if (!strcmp(key, "--frames")) {
            if (!count(v, &frames) || !frames)
                return fail("frames must be 1..100000");
        } else if (!strcmp(key, "--presentation")) {
            if (strcmp(v, "on") && strcmp(v, "off"))
                return fail("presentation must be on or off");
            presentation = !strcmp(v, "on");
        } else if (!strcmp(key, "--output"))
            output = v;
        else
            return fail("unknown option");
    }
    if (!output || !*output || strlen(output) > 4000)
        return fail("output prefix required (1..4000 bytes)");
    /* Counts are bounded above before multiplication, allocation, or conversion to driver int. */
    double *samples = calloc(frames, sizeof(*samples));
    double *sorted = calloc(frames, sizeof(*sorted));
    StateTrace *trace = calloc(warmup + frames, sizeof(*trace));
    if (!samples || !sorted || !trace) {
        free(samples);
        free(sorted);
        free(trace);
        return fail("allocation failed");
    }
    BoatSession session;
    ObjectStudy objects;
    boat_session_init(
        &session, test_world(BOAT_LOCATION_BASIN), boat_water_test(false, false, false));
    const Boat *const boat = &session.boat;
    // Retain the established 1.8 m stress workload; gameplay defaults are tuned separately.
    object_study_reset(&objects, &session, false, BOAT_OBJECT_LIFE_TICKS, 1.8f);
    session.presentation_enabled = presentation;
    unsigned tick = 0;
    uint64_t hash = WAKE_VELOCITY_HASH_SEED;
    for (unsigned f = 0; f < warmup + frames; ++f) {
        double start = boat_profile_time(false);
        for (unsigned j = 0; j < 2; ++j)
            demo_step(&session, (int)tick++, NULL, NULL, &objects);
        double elapsed = (boat_profile_time(false) - start) * 1000;
        /* Trace serialization is deliberately outside the measured simulation interval. */
        double now = boat_session_time(&session);
        hash = wake_velocity_state_hash(hash, boat, now, object_study_wakes(&objects));
        trace[f]
            = (StateTrace) { hash, objects.wakes.emitted, now, boat->position.x, boat->position.y,
                  boat->velocity.x, boat->velocity.y, boat->height, boat->vertical_velocity,
                  (unsigned)boat->motion_state, boat->landings, objects.wakes.live };
        if (f >= warmup)
            samples[f - warmup] = elapsed;
    }
    memcpy(sorted, samples, frames * sizeof(*sorted));
    qsort(sorted, frames, sizeof(*sorted), compare);
    char path[4096];
    snprintf(path, sizeof(path), "%s.csv", output);
    FILE *csv = fopen(path, "w");
    bool ok = csv != NULL;
    if (csv) {
        if (fprintf(csv, "frame,end_tick,simulation_ms\n") < 0)
            ok = false;
        for (unsigned i = 0; i < frames; ++i)
            if (fprintf(csv, "%u,%u,%.9f\n", i, (warmup + i + 1) * 2, samples[i]) < 0)
                ok = false;
        if (fclose(csv))
            ok = false;
    }
    snprintf(path, sizeof(path), "%s.state.csv", output);
    FILE *states = ok ? fopen(path, "w") : NULL;
    if (!states)
        ok = false;
    if (states) {
        if (fprintf(states,
                "end_tick,time,x,z,vx,vz,height,vertical_velocity,motion,landings,live,births,"
                "state_hash\n")
            < 0)
            ok = false;
        for (unsigned i = 0; i < warmup + frames; ++i) {
            StateTrace t = trace[i];
            if (fprintf(states,
                    "%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%u,%u,%" PRIu64 ",%016" PRIx64 "\n",
                    (i + 1) * 2, (double)t.time, (double)t.x, (double)t.z, (double)t.vx,
                    (double)t.vz, (double)t.height, (double)t.vertical_velocity, t.motion,
                    t.landings, t.live, t.births, t.hash)
                < 0)
                ok = false;
        }
        if (fclose(states))
            ok = false;
    }
    snprintf(path, sizeof(path), "%s.json", output);
    FILE *json = ok ? fopen(path, "w") : NULL;
    if (!json)
        ok = false;
    if (json) {
        if (fprintf(json,
                "{\n  \"schema_revision\":1,\"scenario\":\"wake-loop\",\"scenario_revision\":1,\n"
                "  \"physics_revision\":%u,\n"
                "  \"fixture\":\"finite scripted wake-return; powered turns end at tick 1440\",\n"
                "  "
                "\"mode\":\"simulation\",\"renderer\":\"none\",\"presentation\":\"%s\",\n"
                "  \"build\":\"%s\",\"platform\":\"%s\",\n"
                "  "
                "\"warmup_frames\":%u,\"frames\":%u,\"ticks_per_frame\":2,\"warmup_ticks\":%u,"
                "\"measured_ticks\":%u,\"total_ticks\":%u,\n"
                "  \"life_ticks\":%u,\"crest_height\":%.9g,\"births\":%" PRIu64
                ",\"peak_objects\":%u,\"landings\":%u,\n"
                "  \"state_hash\":\"%016" PRIx64 "\",\"hash_revision\":1,\n"
                "  \"simulation_ms\":{\"p50\":%.9f,\"p95\":%.9f,\"p99\":%.9f,\"max\":%.9f},\n"
                "  \"render_cpu_ms\":null,\"gpu_ms\":null\n}\n",
                BOAT_PHYSICS_REVISION, presentation ? "on" : "off", BOAT_BENCH_BUILD,
                BOAT_BENCH_PLATFORM, warmup, frames, warmup * 2, frames * 2, tick,
                objects.wakes.life_ticks, (double)objects.wakes.crest_height, objects.wakes.emitted,
                objects.wakes.peak_live, boat->landings, hash, sorted[(frames - 1) * 50 / 100],
                sorted[(frames - 1) * 95 / 100], sorted[(frames - 1) * 99 / 100],
                sorted[frames - 1])
            < 0)
            ok = false;
        if (fclose(json))
            ok = false;
    }
    free(samples);
    free(sorted);
    free(trace);
    test_worlds_close();
    return ok ? 0 : fail("cannot write report (parent directory must exist)");
}
