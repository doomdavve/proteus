#include "wake_velocity/wake_velocity_course.h"

#include <math.h>
#include <string.h>

enum { PRACTICE = 1, FINISHED = 2, RECORDING_FAILED = 4 };

void wake_course_destroy(WakeCourseAuthority *course)
{
    boat_replay_destroy(&course->recording);
    boat_replay_destroy(&course->run);
    course->pending = false;
}

void wake_course_init(WakeCourseAuthority *course, const BoatSession *session, unsigned slot)
{
    wake_course_destroy(course);
    const BoatReplay *best = &session->runs.best;
    *course = (WakeCourseAuthority) { .definition = session->course_definition,
        .slot = slot,
        .waves = session->world.water.amplitude != 0,
        .record = session->record_runs,
        .follow = session->presentation_enabled,
        .has_best = best->complete,
        .best = best->complete ? best->splits[BOAT_COURSE_GATES - 1] : 0,
        .course = { .enabled = true, .definition = session->course_definition },
        .previous = session->boat,
        .camera_heading = session->presentation.camera_heading };
}

static BoatReplaySample pose(const Boat *boat, float camera_heading, double time)
{
    static const BoatPresentation none;
    BoatReplaySample sample = boat_replay_pose(boat, &none, time);
    sample.camera_heading = camera_heading;
    return sample;
}

// Run on the authority after each committed tick, as boat_session_step steps and records.
static void committed(void *context, uint64_t tick, const BoatFleet *fleet)
{
    WakeCourseAuthority *c = context;
    const Boat *boat = &fleet->state.slots[c->slot].boat;
    double now = boat_tick_seconds(tick);
    bool record = c->record && !c->finished;
    BoatReplaySample before = record
        ? pose(&c->previous, c->camera_heading, boat_tick_seconds(tick - 1))
        : (BoatReplaySample) { 0 };
    boat_course_step(&c->course, c->previous.position, boat->position, BOAT_TICK_SECONDS);
    if (c->follow)
        c->camera_heading = boat_camera_follow(c->camera_heading, boat->heading);
    c->previous = *boat;
    if (!record)
        return;
    boat_replay_record(
        &c->recording, before, pose(boat, c->camera_heading, now), &c->course, c->waves);
    if (!c->recording.complete)
        return;
    double time = c->recording.splits[BOAT_COURSE_GATES - 1];
    bool faster = !c->practice && (!c->has_best || time < c->best);
    if (faster) {
        c->has_best = true;
        c->best = time;
    }
    // One request per generation: a run finishes once, and the host takes it after the frame.
    boat_replay_destroy(&c->run);
    c->run = c->recording;
    c->recording = (BoatReplay) { 0 };
    c->pending = true;
    c->pending_faster = faster;
    c->pending_practice = c->practice;
    c->finished = true;
}

static void put64(uint8_t **p, uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        *(*p)++ = (uint8_t)(value >> shift);
}

static uint64_t get64(const uint8_t **p)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value = value << 8 | *(*p)++;
    return value;
}

static void putd(uint8_t **p, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put64(p, bits);
}

static double getd(const uint8_t **p)
{
    uint64_t bits = get64(p);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

size_t wake_course_encode(const WakeCourseAuthority *course, uint8_t *out)
{
    const BoatCourse *c = &course->course;
    uint8_t *p = out;
    *p++ = (uint8_t)c->next_gate;
    *p++ = (uint8_t)((course->practice ? PRACTICE : 0) | (course->finished ? FINISHED : 0)
        | (course->recording.failed ? RECORDING_FAILED : 0));
    putd(&p, c->elapsed);
    for (unsigned i = 0; i < BOAT_COURSE_GATES; ++i)
        putd(&p, c->splits[i]);
    for (unsigned i = 0; i < BOAT_COURSE_GATES; ++i)
        putd(&p, c->crossing[i]);
    return (size_t)(p - out);
}

static size_t private_state(void *context, uint8_t *out)
{
    return wake_course_encode(context, out);
}

// L1b: an unchanged or renamed course revision: the same gates in the new revision's storage, so
// the authority never borrows a revision the host may reuse.
static bool commit(void *context, uint64_t tick, unsigned slot, uint32_t revision, uint8_t change,
    BoatNetCommitResult *result)
{
    (void)tick;
    (void)result;
    WakeCourseAuthority *c = context;
    const BoatCourseDefinition *definition = c->resolve && change == WAKE_COURSE_REBIND
        ? c->resolve(c->resolve_context, revision)
        : NULL;
    if (slot != c->slot || !definition)
        return false;
    c->definition = definition;
    c->course.definition = definition;
    return true;
}

BoatNetRules wake_course_rules(WakeCourseAuthority *course)
{
    return (BoatNetRules) {
        .context = course, .committed = committed, .private_state = private_state, .commit = commit
    };
}

void wake_course_practice(WakeCourseAuthority *course)
{
    if (!course->finished)
        course->practice = true;
}

bool wake_course_take(WakeCourseAuthority *course, BoatReplayRuns *runs)
{
    if (!course->pending)
        return false;
    runs->practice = course->pending_practice;
    boat_replay_runs_finish(runs, &course->run, course->pending_faster);
    course->pending = false;
    return true;
}

bool wake_course_view(const uint8_t *state, size_t size, BoatSession *session)
{
    if (!state || size != WAKE_COURSE_STATE_BYTES || state[0] > BOAT_COURSE_GATES
        || (state[1] & ~(PRACTICE | FINISHED | RECORDING_FAILED)))
        return false;
    const uint8_t *p = state + 2;
    BoatCourse *c = &session->course;
    c->next_gate = state[0];
    c->elapsed = getd(&p);
    for (unsigned i = 0; i < BOAT_COURSE_GATES; ++i)
        c->splits[i] = getd(&p);
    for (unsigned i = 0; i < BOAT_COURSE_GATES; ++i)
        c->crossing[i] = getd(&p);
    BoatReplayRuns *runs = &session->runs;
    if (!runs->finished)
        runs->practice = (state[1] & PRACTICE) != 0;
    if ((state[1] & RECORDING_FAILED) && !runs->recording.failed) {
        runs->recording.failed = true; // The client records nothing; this marks the notice seen.
        runs->notice = "Recording stopped (memory or 10 minute limit); best preserved";
    }
    return true;
}
