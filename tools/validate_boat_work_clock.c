/* The work session's native daylight clock: fixed ticks, not frame time; rate scaling through
 * the existing virtual frame scheduler; wraps and seeks; reset/recovery; save/resume; and the
 * idle clock dirtying automatic saving. Real bundled basin work resources, no window or files. */
#include "boat_location.h"
#include "boat_plugin_catalog.h"
#include "boat_time_control.h"
#include "boat_work_session.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "work clock check failed at %d: %s\n", __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static BoatWorldResources basin_resources;
static BoatPluginRevision basin_revision;
static bool basin_ready;

static BoatWorldResources job_resources;
static BoatPluginRevision job_revision;
static bool job_ready;

static bool close_enough(double a, double b) { return fabs(a - b) <= 1e-7 * (1.0 + fabs(b)); }

static void bind_session(
    BoatWorkSession *e, const BoatWorldResources *resources, const BoatPluginRevision *revision)
{
    memset(e, 0, sizeof(*e));
    e->resources = resources;
    e->writer = "boat_work_clock";
    e->world.scene = &revision->free.scene;
    e->revision = revision->free.manifest.objective ? revision : NULL;
    e->site_count = revision->free.scene.work_count;
    CHECK(e->site_count > 0 && e->site_count <= BOAT_SCENE_WORK_SITES);
    memcpy(e->sites, revision->free.scene.work, sizeof(e->sites));
    boat_work_session_reset(e);
}

static void basin_setup(BoatWorkSession *e)
{
    if (!basin_ready) {
        CHECK(boat_world_resources_init(&basin_resources, boat_location(BOAT_LOCATION_BASIN)));
        const BoatPluginCatalogEntry *entry = boat_plugin_catalog_work_default("basin");
        CHECK(entry && !strcmp(entry->id, BOAT_PLUGIN_DEFAULT_BASIN_WORK));
        BoatPluginDiagnostic diagnostic = { 0 };
        CHECK(boat_plugin_catalog_decode(entry, &basin_revision, &diagnostic));
        CHECK(basin_revision.free.has_scene && basin_revision.free.scene.work_present);
        basin_ready = true;
    }
    bind_session(e, &basin_resources, &basin_revision);
}

// The smallest bundled job-bearing work package: the save codec refuses a jobless session.
static void job_setup(BoatWorkSession *e)
{
    if (!job_ready) {
        CHECK(boat_world_resources_init(&job_resources, boat_location(BOAT_LOCATION_OUTER)));
        const BoatPluginCatalogEntry *entry = boat_plugin_catalog_work_default("outer");
        CHECK(entry && !strcmp(entry->id, BOAT_PLUGIN_DEFAULT_OUTER_WORK));
        BoatPluginDiagnostic diagnostic = { 0 };
        CHECK(boat_plugin_catalog_decode(entry, &job_revision, &diagnostic));
        CHECK(job_revision.free.has_scene && job_revision.free.scene.work_present);
        job_ready = true;
    }
    bind_session(e, &job_resources, &job_revision);
    CHECK(e->revision != NULL);
}

// Virtual frames at a selected live-play rate, then whole fixed ticks. This is the same scheduling
// the game uses; the clock itself only ever sees the ticks, never the frame seconds.
static uint64_t run_frames(BoatWorkSession *e, unsigned frames, int speed_index)
{
    uint64_t ticks = 0;
    double accumulator = 0;
    for (unsigned frame = 0; frame < frames; ++frame) {
        accumulator += boat_time_frame_seconds(1.0 / 60.0, speed_index);
        while (accumulator >= BOAT_TICK_SECONDS) {
            accumulator -= BOAT_TICK_SECONDS;
            CHECK(boat_work_lighting_advance(e, 1));
            ++ticks;
        }
    }
    return ticks;
}

static void check_defaults_and_rates(void)
{
    static BoatWorkSession e;
    basin_setup(&e);
    CHECK(e.lighting_hours == BOAT_SANDBOX_LIGHTING_HOURS_DEFAULT);
    CHECK(e.lighting_cycle_seconds == BOAT_SANDBOX_LIGHTING_PERIOD_DEFAULT);
    CHECK(close_enough(boat_work_lighting_hour(&e), 12.0));

    // 1x for one 900 s day and 8x for 112.5 s both run 108000 physical ticks = 24 game hours.
    CHECK(run_frames(&e, 900 * 60, BOAT_TIME_NORMAL) == 900 * BOAT_TICK_HZ);
    CHECK(close_enough(e.lighting_hours, 36.0));

    basin_setup(&e);
    CHECK(run_frames(&e, 6750, BOAT_TIME_MAX_INDEX) == 900 * BOAT_TICK_HZ);
    CHECK(close_enough(e.lighting_hours, 36.0));

    // Changing speed changes only how many fixed ticks run, so the clock stays continuous.
    basin_setup(&e);
    CHECK(boat_work_lighting_seek(&e, 3.0, 900.0));
    uint64_t ticks = 0;
    ticks += run_frames(&e, 100 * 60, BOAT_TIME_NORMAL);
    ticks += run_frames(&e, 50 * 60, 0);
    ticks += run_frames(&e, 25 * 60, BOAT_TIME_MAX_INDEX);
    CHECK(ticks == 100 * BOAT_TICK_HZ + 50 * BOAT_TICK_HZ / 4 + 25 * BOAT_TICK_HZ * 8);
    CHECK(close_enough(e.lighting_hours, 3.0 + (double)ticks * 24.0 / (BOAT_TICK_HZ * 900.0)));
}

static void check_resume_frame_input(void)
{
    // No focus-false frame is required: an OS/process suspension still contributes no ticks.
    CHECK(boat_time_work_frame_seconds(30, BOAT_TIME_MAX_INDEX, true) == 0);
    CHECK(boat_time_work_frame_seconds(.016, BOAT_TIME_MAX_INDEX, false) == 0);
    CHECK(boat_time_work_frame_seconds(NAN, BOAT_TIME_MAX_INDEX, true) == 0);
    CHECK(boat_time_work_frame_seconds(INFINITY, BOAT_TIME_MAX_INDEX, true) == 0);
    CHECK(boat_time_work_frame_seconds(-1, BOAT_TIME_MAX_INDEX, true) == 0);
    CHECK(boat_time_work_frame_seconds(.5, BOAT_TIME_MAX_INDEX, true)
        == boat_time_frame_seconds(.5, BOAT_TIME_MAX_INDEX));
    CHECK(boat_time_work_frame_seconds(1.0 / 60, BOAT_TIME_MAX_INDEX, true)
        == boat_time_frame_seconds(1.0 / 60, BOAT_TIME_MAX_INDEX));
}

static void check_wraps_and_seeks(void)
{
    static BoatWorkSession e;
    basin_setup(&e);
    CHECK(boat_work_lighting_seek(&e, 23.9, 900.0));
    CHECK(boat_work_lighting_advance(&e, 2250)); // Half an hour.
    CHECK(close_enough(e.lighting_hours, 24.4));
    CHECK(close_enough(boat_work_lighting_hour(&e), 0.4));
    for (unsigned cycle = 1; cycle <= 3; ++cycle) {
        CHECK(boat_work_lighting_advance(&e, 108000)); // Another whole day.
        CHECK(close_enough(e.lighting_hours, 24.4 + 24.0 * cycle));
        CHECK(close_enough(boat_work_lighting_hour(&e), 0.4));
    }

    CHECK(boat_work_lighting_seek(&e, 24.0, 900.0));
    CHECK(close_enough(boat_work_lighting_hour(&e), 0.0));
    CHECK(boat_work_lighting_seek(&e, 23.99999999, 900.0));
    CHECK(boat_work_lighting_hour(&e) == 0); // Float rounding must not expose hour 24.
    CHECK(boat_work_lighting_seek(&e, 30.5, 1200.0));
    CHECK(close_enough(boat_work_lighting_hour(&e), 6.5));
    CHECK(boat_work_lighting_seek(&e, 2.25, 1200.0)); // Backwards.
    CHECK(e.lighting_hours == 2.25 && e.lighting_cycle_seconds == 1200.0);
    CHECK(close_enough(boat_work_lighting_hour(&e), 2.25));
}

static void check_invalid_seek(void)
{
    static BoatWorkSession e;
    basin_setup(&e);
    CHECK(boat_work_lighting_seek(&e, 7.5, 600.0));
    const double bad_hours[] = { -1.0, NAN, INFINITY, BOAT_SANDBOX_LIGHTING_HOURS_MAX + 1.0 };
    const double bad_periods[]
        = { 0.0, 0.5, NAN, INFINITY, BOAT_SANDBOX_LIGHTING_PERIOD_MAX + 1.0 };
    for (unsigned i = 0; i < sizeof(bad_hours) / sizeof(*bad_hours); ++i) {
        CHECK(!boat_work_lighting_seek(&e, bad_hours[i], 600.0));
        CHECK(e.lighting_hours == 7.5 && e.lighting_cycle_seconds == 600.0);
    }
    for (unsigned i = 0; i < sizeof(bad_periods) / sizeof(*bad_periods); ++i) {
        CHECK(!boat_work_lighting_seek(&e, 7.5, bad_periods[i]));
        CHECK(e.lighting_hours == 7.5 && e.lighting_cycle_seconds == 600.0);
    }
}

static void check_reset_and_recovery(void)
{
    static BoatWorkSession e;
    basin_setup(&e);
    CHECK(boat_work_lighting_seek(&e, 13.25, 450.0));
    CHECK(boat_work_lighting_advance(&e, 123));
    double hours = e.lighting_hours, period = e.lighting_cycle_seconds;
    boat_work_session_reset_world(&e);
    CHECK(e.lighting_hours == hours && e.lighting_cycle_seconds == period);
    boat_work_job_reset(&e);
    CHECK(e.lighting_hours == hours && e.lighting_cycle_seconds == period);
    boat_work_recover_action(&e);
    CHECK(e.lighting_hours == hours && e.lighting_cycle_seconds == period);
    boat_work_session_reset(&e);
    CHECK(e.lighting_hours == hours && e.lighting_cycle_seconds == period);

    // A zeroed session is uninitialized: the world reset supplies noon / 900 s.
    static BoatWorkSession fresh;
    memset(&fresh, 0, sizeof(fresh));
    fresh.resources = &basin_resources;
    fresh.writer = "boat_work_clock";
    fresh.world.scene = &basin_revision.free.scene;
    fresh.revision = basin_revision.free.manifest.objective ? &basin_revision : NULL;
    fresh.site_count = basin_revision.free.scene.work_count;
    memcpy(fresh.sites, basin_revision.free.scene.work, sizeof(fresh.sites));
    boat_work_session_reset_world(&fresh);
    CHECK(fresh.lighting_hours == BOAT_SANDBOX_LIGHTING_HOURS_DEFAULT);
    CHECK(fresh.lighting_cycle_seconds == BOAT_SANDBOX_LIGHTING_PERIOD_DEFAULT);
}

static void check_tick_integration(void)
{
    static BoatWorkSession e;
    basin_setup(&e);
    CHECK(boat_work_lighting_seek(&e, 1.0, 900.0));
    uint64_t tick = e.tick;
    double hours = e.lighting_hours;
    boat_work_session_tick(&e, 0, 0, 0, (BoatInput) { 0 });
    CHECK(e.tick == tick + 1);
    CHECK(close_enough(
        e.lighting_hours, hours + 24.0 / ((double)BOAT_TICK_HZ * e.lighting_cycle_seconds)));
}

static void check_save_and_resume(void)
{
    static BoatWorkSession e;
    job_setup(&e);
    CHECK(boat_work_lighting_seek(&e, 30.123456789, 720.5));
    CHECK(boat_work_lighting_advance(&e, 12345));
    double hours = e.lighting_hours, period = e.lighting_cycle_seconds;
    uint64_t tick = e.tick;
    static BoatSandboxSave save;
    CHECK(boat_work_session_save(&e, 42, &save));
    CHECK(save.lighting_hours == hours && save.lighting_cycle_seconds == period);

    CHECK(boat_work_lighting_seek(&e, 1.25, 900.0));
    CHECK(boat_work_lighting_advance(&e, 1));
    CHECK(e.lighting_hours != hours);
    CHECK(boat_work_session_resume(&e, &save));
    CHECK(e.lighting_hours == hours && e.lighting_cycle_seconds == period && e.tick == tick);
    BoatWorkWatch unchanged = boat_work_watch(&e);

    static BoatSandboxSave bad;
    bad = save;
    bad.lighting_hours = -1.0;
    CHECK(!boat_work_session_resume(&e, &bad));
    CHECK(e.lighting_hours == hours && e.lighting_cycle_seconds == period && e.tick == tick);
    bad = save;
    bad.lighting_cycle_seconds = 0.0;
    CHECK(!boat_work_session_resume(&e, &bad));
    BoatWorkWatch still = boat_work_watch(&e);
    CHECK(e.lighting_hours == hours && e.lighting_cycle_seconds == period && e.tick == tick
        && still.position.x == unchanged.position.x && still.position.y == unchanged.position.y
        && still.heading == unchanged.heading && still.progress == unchanged.progress
        && still.load == unchanged.load && still.active == unchanged.active
        && still.moored == unchanged.moored);

    // A rejected save must not even drain a pending native cargo event into the job queue.
    static BoatWorkSession invalid;
    job_setup(&invalid);
    CHECK(invalid.sandbox.event_count < BOAT_CARGO_EVENTS);
    invalid.sandbox.events[invalid.sandbox.event_count++]
        = (BoatCargoEvent) { .kind = BOAT_CARGO_EVENT_RESET, .site = 0, .load = 1 };
    unsigned native = invalid.sandbox.event_count, queued = invalid.event_count;
    invalid.lighting_hours = -1.0;
    static BoatSandboxSave ignored;
    CHECK(!boat_work_session_save(&invalid, 0, &ignored));
    CHECK(invalid.sandbox.event_count == native && invalid.event_count == queued);
}

static void check_idle_autosave(void)
{
    static BoatWorkSession e;
    basin_setup(&e);
    CHECK(boat_work_lighting_seek(&e, 12.0, 900.0));
    BoatWorkWatch before = boat_work_watch(&e);
    CHECK(boat_work_lighting_advance(&e, 1));
    BoatWorkWatch after = boat_work_watch(&e);
    CHECK(after.lighting_hours != before.lighting_hours);
    CHECK(after.position.x == before.position.x && after.position.y == before.position.y);

    BoatAutosave autosave = { 0 };
    BoatWorkFrameWatch frame = { .last = before, .was_paused = false, .was_focused = true };
    bool show = false;
    CHECK(!boat_work_frame_due(&e, &autosave, &frame, false, true, &show));
    CHECK(autosave.dirty); // The passing light alone made the idle session dirty.
    CHECK(!boat_work_frame_due(&e, &autosave, &frame, false, true, &show));
    CHECK(autosave.dirty);
    CHECK(boat_work_frame_due(&e, &autosave, &frame, true, true, &show)); // Pausing saves it.

    BoatAutosave periodic = { 0 };
    BoatWorkFrameWatch periodic_frame
        = { .last = boat_work_watch(&e), .was_paused = false, .was_focused = true };
    CHECK(boat_work_lighting_advance(&e, 1));
    CHECK(!boat_work_frame_due(&e, &periodic, &periodic_frame, false, true, &show));
    CHECK(periodic.dirty);
    e.tick = BOAT_AUTOSAVE_PERIOD;
    CHECK(boat_work_frame_due(&e, &periodic, &periodic_frame, false, true, &show));

    BoatAutosave leaving = { 0 };
    BoatWorkFrameWatch leaving_frame
        = { .last = boat_work_watch(&e), .was_paused = false, .was_focused = true };
    CHECK(boat_work_lighting_advance(&e, 1));
    CHECK(!boat_work_frame_due(&e, &leaving, &leaving_frame, false, true, &show));
    CHECK(leaving.dirty);
    CHECK(boat_work_frame_due(&e, &leaving, &leaving_frame, false, false, &show)); // Focus loss.

    // The real tick advances the same watch, not just the helper.
    BoatWorkWatch ticked_before = boat_work_watch(&e);
    boat_work_session_tick(&e, 0, 0, 0, (BoatInput) { 0 });
    BoatWorkWatch ticked_after = boat_work_watch(&e);
    CHECK(ticked_after.lighting_hours != ticked_before.lighting_hours);
}

int main(void)
{
    check_defaults_and_rates();
    check_resume_frame_input();
    check_wraps_and_seeks();
    check_invalid_seek();
    check_reset_and_recovery();
    check_tick_integration();
    check_save_and_resume();
    check_idle_autosave();
    printf("work clock checks passed\n");
    return 0;
}
