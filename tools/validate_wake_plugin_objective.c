/* AR3e: the game's browser objective policy compiled natively. wake_velocity_plugin.c runs with
 * WAKE_PLUGIN_ISOLATED_OBJECTIVE=1 over this C model of its page bridges; the cases drive the
 * 5 s deadline, stale and failed receipts, BUSY controls and reset/close cleanup on virtual time.
 */
#include "boat_plugin_eval.h"
#include "boat_test_cases.h"
#include "boat_world.h"
#include "raymath.h"
#include "wake_velocity_plugin.h"
#include "wake_velocity_plugin_web.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// The page bridge model: one record per started job, indexed by its nonzero, increasing id.
#define MODEL_JOBS 8

typedef struct {
    unsigned id;
    unsigned char request[BOAT_PLUGIN_EVAL_REQUEST_MAX];
    unsigned request_size;
    int status; // 0 pending, 1 replied, -1 failed.
    unsigned char reply[BOAT_PLUGIN_EVAL_REPLY_MAX];
    unsigned reply_size;
    bool used, closed;
} ModelJob;

static struct {
    ModelJob jobs[MODEL_JOBS];
    unsigned next_id, last_id;
    unsigned started, closed, reads, shutdowns;
    unsigned actions;
} model;

static void model_reset(void)
{
    memset(&model, 0, sizeof(model));
    model.next_id = 1;
}

static ModelJob *job_find(unsigned id)
{
    for (unsigned i = 0; i < MODEL_JOBS; ++i)
        if (model.jobs[i].used && model.jobs[i].id == id)
            return &model.jobs[i];
    return NULL;
}

static ModelJob *job_find_open(unsigned id)
{
    ModelJob *job = job_find(id);
    return job && !job->closed ? job : NULL;
}

static bool model_reply(unsigned job_id)
{
    ModelJob *job = job_find(job_id);
    if (!job)
        return false;
    size_t size
        = boat_plugin_eval_execute(job->request, job->request_size, job->reply, sizeof(job->reply));
    if (!size)
        return false;
    job->reply_size = (unsigned)size;
    job->status = 1;
    return true;
}

static bool model_fail(unsigned job_id)
{
    ModelJob *job = job_find(job_id);
    if (!job)
        return false;
    job->status = -1;
    return true;
}

static unsigned model_current(void) { return model.last_id; }

static unsigned model_open_jobs(void)
{
    unsigned open = 0;
    for (unsigned i = 0; i < MODEL_JOBS; ++i)
        if (model.jobs[i].used && !model.jobs[i].closed)
            ++open;
    return open;
}

static bool model_closed(unsigned id)
{
    ModelJob *job = job_find(id);
    return job && job->closed;
}

void wake_web_plugin_owner(unsigned revision, unsigned low, unsigned high, const char *status)
{
    (void)revision;
    (void)low;
    (void)high;
    (void)status;
}

bool wake_web_plugin_editing(void) { return false; }

unsigned wake_web_plugin_actions(void)
{
    unsigned actions = model.actions;
    model.actions = 0;
    return actions;
}

unsigned wake_web_objective_start(const unsigned char *bytes, unsigned size)
{
    unsigned slot = MODEL_JOBS;
    for (unsigned i = 0; i < MODEL_JOBS; ++i)
        if (!model.jobs[i].used) {
            slot = i;
            break;
        }
    if (slot == MODEL_JOBS || size > BOAT_PLUGIN_EVAL_REQUEST_MAX)
        return 0;
    ModelJob *job = &model.jobs[slot];
    memset(job, 0, sizeof(*job));
    job->id = model.next_id++;
    job->used = true;
    job->request_size = size;
    memcpy(job->request, bytes, size);
    model.last_id = job->id;
    ++model.started;
    return job->id;
}

int wake_web_objective_status(unsigned job)
{
    ModelJob *open = job_find_open(job);
    return open ? open->status : -1;
}

int wake_web_objective_read(unsigned job, unsigned char *bytes)
{
    ++model.reads;
    ModelJob *open = job_find_open(job);
    if (!open)
        return -1;
    memcpy(bytes, open->reply, open->reply_size);
    return (int)open->reply_size;
}

void wake_web_objective_close(unsigned job)
{
    ModelJob *open = job_find_open(job);
    if (open) {
        open->closed = true;
        ++model.closed;
    }
}

// Counted only: the cases require the policy itself to have closed every job by then.
void wake_web_objective_shutdown(void) { ++model.shutdowns; }

void wake_web_objective_snapshot(unsigned low, unsigned high, unsigned native_low,
    unsigned native_high, unsigned generation, bool initialized, bool quarantined, unsigned events,
    bool waiting, const unsigned char *bytes, unsigned size)
{
    (void)low;
    (void)high;
    (void)native_low;
    (void)native_high;
    (void)generation;
    (void)initialized;
    (void)quarantined;
    (void)events;
    (void)waiting;
    (void)bytes;
    (void)size;
}

int wake_web_plugin_export(const unsigned char *manifest, unsigned manifest_size,
    const unsigned char *source, unsigned source_size)
{
    (void)manifest;
    (void)manifest_size;
    (void)source;
    (void)source_size;
    return 0;
}

void wake_web_plugin_controls(bool pending, bool menu_open)
{
    (void)pending;
    (void)menu_open;
}

static BoatWorldResources outer_world;
static bool outer_ready;

static const BoatWorldResources *outer(void)
{
    if (!outer_ready) {
        if (!boat_world_resources_init(&outer_world, boat_location(BOAT_LOCATION_OUTER))) {
            fprintf(stderr, "cannot prepare the outer world\n");
            exit(1);
        }
        outer_ready = true;
    }
    return &outer_world;
}

static BoatWater calm(void)
{
    return boat_world_test_water(boat_location(BOAT_LOCATION_OUTER), false, false, false);
}

// Open the shelter-tour package on a fresh outer session at NOW. Its INIT job is left open.
static int begin(WakePlugin *p, BoatSession *s, double now)
{
    boat_session_init(s, outer(), calm());
    s->presentation_enabled = false;
    bool opened = wake_plugin_open(p, "plugins/shelter-tour", s, now);
    CHECK(opened);
    // Read the package once: no frame polls the directory, so a concurrent edit cannot reload.
    p->watching = false;
    return 0;
}

// Reply to the open INIT job and accept it at NOW.
static int accept_init(WakePlugin *p, BoatSession *s, double now)
{
    CHECK(model_reply(model_current()));
    CHECK(!wake_plugin_frame(p, s, false, false, false, now));
    CHECK(p->run.objective.initialized && !p->run.objective.quarantined && !wake_plugin_waiting(p));
    return 0;
}

// An accepted INIT at 101.0, then ticks 1..12. The update job starts at tick 12 and is left open;
// its 5 s deadline is 101 + 5 = 106.
static void observe(WakePlugin *p, BoatSession *s, unsigned tick)
{
    wake_plugin_before_tick(p);
    boat_session_set_tick(s, tick);
    boat_voyage_step_events(&s->voyage, s->voyage.route->shelters[1].position, (Vector2) { 0 },
        boat_session_time(s), &s->voyage_events);
    wake_plugin_after_tick(p, s);
}

static int begin_update(WakePlugin *p, BoatSession *s)
{
    CHECK(begin(p, s, 100.0) == 0);
    CHECK(accept_init(p, s, 101.0) == 0);
    for (unsigned tick = 1; tick <= 12; ++tick)
        observe(p, s, tick);
    CHECK(wake_plugin_waiting(p));
    CHECK(model_open_jobs() == 1);
    CHECK(!p->run.objective_pending);
    return 0;
}

// a. A pending INIT is still waiting before its deadline; the reply is accepted just before it.
static int init_before_deadline(void)
{
    static WakePlugin p;
    BoatSession s;
    model_reset();
    CHECK(begin(&p, &s, 100.0) == 0);
    CHECK(wake_plugin_waiting(&p));
    CHECK(model.started == 1 && model_open_jobs() == 1);
    CHECK(!p.run.objective.initialized);
    unsigned job = model_current();
    CHECK(!wake_plugin_frame(&p, &s, false, false, false, 104.0));
    CHECK(wake_plugin_waiting(&p) && !p.run.objective.quarantined);
    CHECK(model_reply(job));
    CHECK(!wake_plugin_frame(&p, &s, false, false, false, nextafter(105.0, 0.0)));
    CHECK(p.run.objective.initialized && !p.run.objective.quarantined && !wake_plugin_waiting(&p));
    CHECK(model_closed(job));
    wake_plugin_close(&p, &s);
    boat_session_destroy(&s);
    return 0;
}

// b. A reply first accepted exactly at the 5 s deadline is rejected and quarantines the objective.
static int init_at_deadline(void)
{
    static WakePlugin p;
    BoatSession s;
    model_reset();
    CHECK(begin(&p, &s, 100.0) == 0);
    unsigned job = model_current();
    CHECK(model_reply(job));
    CHECK(!wake_plugin_frame(&p, &s, false, false, false, 105.0));
    CHECK(p.run.objective.quarantined);
    CHECK(!strcmp(p.run.objective_diagnostic.message, "isolated objective deadline"));
    CHECK(!wake_plugin_waiting(&p));
    CHECK(model_closed(job));
    wake_plugin_close(&p, &s);
    boat_session_destroy(&s);
    return 0;
}

// c. The T update job at tick 12 is rejected at exactly 106.0 and quarantines the objective.
static int update_at_deadline(void)
{
    static WakePlugin p;
    BoatSession s;
    model_reset();
    CHECK(begin_update(&p, &s) == 0);
    unsigned job = model_current();
    uint64_t digest = boat_plugin_objective_digest(&p.run.objective);
    CHECK(!p.run.objective_pending);
    wake_plugin_before_tick(&p);
    CHECK(boat_plugin_objective_digest(&p.run.objective) == digest);
    CHECK(model_reply(job));
    CHECK(!wake_plugin_frame(&p, &s, false, false, false, 106.0));
    CHECK(p.run.objective.quarantined);
    CHECK(!strcmp(p.run.objective_diagnostic.message, "isolated objective deadline"));
    CHECK(!wake_plugin_waiting(&p));
    CHECK(model_closed(job));
    wake_plugin_close(&p, &s);
    boat_session_destroy(&s);
    return 0;
}

// d. Just before the deadline the UPDATE is accepted but published only at T+1.
static int update_before_deadline(void)
{
    static WakePlugin p;
    BoatSession s;
    model_reset();
    CHECK(begin_update(&p, &s) == 0);
    unsigned job = model_current();
    uint64_t digest = boat_plugin_objective_digest(&p.run.objective);
    CHECK(model_reply(job));
    CHECK(!wake_plugin_frame(&p, &s, false, false, false, nextafter(106.0, 0.0)));
    CHECK(!p.run.objective.quarantined && !wake_plugin_waiting(&p));
    CHECK(p.run.objective_pending);
    CHECK(boat_plugin_objective_digest(&p.run.objective) == digest);
    wake_plugin_before_tick(&p);
    CHECK(!p.run.objective_pending);
    CHECK(p.run.objective.tick == 12);
    CHECK(boat_plugin_objective_digest(&p.run.objective)
        == boat_plugin_objective_digest(&p.run.staged_objective));
    wake_plugin_close(&p, &s);
    boat_session_destroy(&s);
    return 0;
}

// e. A reply whose committed input moved on is stale and quarantines the objective.
static int stale_receipt(void)
{
    static WakePlugin p;
    BoatSession s;
    model_reset();
    CHECK(begin_update(&p, &s) == 0);
    unsigned job = model_current();
    boat_session_set_tick(&s, 13);
    CHECK(model_reply(job));
    CHECK(!wake_plugin_frame(&p, &s, false, false, false, 102.0));
    CHECK(p.run.objective.quarantined);
    CHECK(!strcmp(p.run.objective_diagnostic.message, "stale objective receipt"));
    CHECK(!wake_plugin_waiting(&p));
    CHECK(model_closed(job));
    wake_plugin_close(&p, &s);
    boat_session_destroy(&s);
    return 0;
}

// f. A failed worker reply is rejected with its own diagnostic.
static int failed_reply(void)
{
    static WakePlugin p;
    BoatSession s;
    model_reset();
    CHECK(begin_update(&p, &s) == 0);
    unsigned job = model_current();
    CHECK(model_fail(job));
    CHECK(!wake_plugin_frame(&p, &s, false, false, false, 102.0));
    CHECK(p.run.objective.quarantined);
    CHECK(!strcmp(p.run.objective_diagnostic.message, "invalid or failed objective worker reply"));
    CHECK(!wake_plugin_waiting(&p));
    CHECK(model_closed(job));
    wake_plugin_close(&p, &s);
    boat_session_destroy(&s);
    return 0;
}

// g. Controls are refused visibly while the update job is in flight and start no new job.
static int busy_refuses_controls(void)
{
    static WakePlugin p;
    BoatSession s;
    model_reset();
    CHECK(begin_update(&p, &s) == 0);
    unsigned started = model.started;
    CHECK(!wake_plugin_frame(&p, &s, true, false, false, 102.0));
    CHECK(wake_plugin_waiting(&p));
    CHECK(strstr(p.status, "OBJECTIVE EVALUATION BUSY"));
    CHECK(model.started == started && model_open_jobs() == 1);
    wake_plugin_close(&p, &s);
    boat_session_destroy(&s);
    return 0;
}

// h. Reset closes the in-flight job and starts a fresh INIT; late replies to the old job are never
// read. Close releases every job and shuts the evaluator down once per session.
static int reset_and_repeated_sessions(void)
{
    static WakePlugin p;
    BoatSession s, second;
    model_reset();
    CHECK(begin_update(&p, &s) == 0);
    unsigned old = model_current();
    unsigned started = model.started;
    CHECK(model_open_jobs() == 1);
    wake_plugin_reset_objective(&p, &s);
    CHECK(model_closed(old));
    CHECK(model.started == started + 1);
    unsigned fresh = model_current();
    CHECK(fresh != old && model_open_jobs() == 1);
    CHECK(model_reply(old)); // A late reply to the released job is inert to the policy.
    unsigned reads = model.reads;
    CHECK(!wake_plugin_frame(&p, &s, false, false, false, 101.5));
    CHECK(wake_plugin_waiting(&p) && model.reads == reads);
    CHECK(!wake_plugin_frame(&p, &s, false, false, false, 101.75));
    CHECK(model.reads == reads && model_open_jobs() == 1);
    wake_plugin_close(&p, &s);
    CHECK(model_open_jobs() == 0);
    CHECK(model.shutdowns == 1);
    boat_session_destroy(&s);

    CHECK(begin(&p, &second, 200.0) == 0);
    CHECK(model_reply(model_current()));
    CHECK(!wake_plugin_frame(&p, &second, false, false, false, 201.0));
    CHECK(p.run.objective.initialized && !wake_plugin_waiting(&p));
    wake_plugin_close(&p, &second);
    CHECK(model_open_jobs() == 0);
    CHECK(model.shutdowns == 2);
    boat_session_destroy(&second);
    return 0;
}

int main(int argc, char **argv)
{
    static const TestCase cases[] = {
        TEST_CASE(init_before_deadline),
        TEST_CASE(init_at_deadline),
        TEST_CASE(update_at_deadline),
        TEST_CASE(update_before_deadline),
        TEST_CASE(stale_receipt),
        TEST_CASE(failed_reply),
        TEST_CASE(busy_refuses_controls),
        TEST_CASE(reset_and_repeated_sessions),
    };
    int result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    if (outer_ready)
        boat_world_resources_destroy(&outer_world);
    return result;
}
