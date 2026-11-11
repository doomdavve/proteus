#include "wake_velocity_objective.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void wake_objective_trace(WakeObjective *run, unsigned revision, BoatPluginTransition action,
    uint64_t before, const BoatPluginObjective *after, unsigned events)
{
    if (run->trace_count == WAKE_PLUGIN_TRACE) {
        memmove(run->trace, run->trace + 1, (WAKE_PLUGIN_TRACE - 1) * sizeof(run->trace[0]));
        --run->trace_count;
    }
    WakePluginTrace *entry = &run->trace[run->trace_count++];
    *entry = (WakePluginTrace) { .tick = after->tick,
        .revision = revision,
        .events = events,
        .transition = action,
        .status = run->objective_diagnostic.status,
        .before = before,
        .after = boat_plugin_objective_digest(after) };
    if (entry->status == BOAT_PLUGIN_OK) {
        entry->command_count = after->command_count;
        memcpy(entry->commands, after->commands, sizeof(entry->commands));
    }
}

static void limit(WakeObjective *run, BoatPluginStatus status, const char *message)
{
    run->objective.quarantined = true;
    run->objective_pending = false;
    run->objective_diagnostic.status = status;
    snprintf(run->objective_diagnostic.message, sizeof(run->objective_diagnostic.message), "%s",
        message);
}

bool wake_objective_clear(WakeObjective *run, const BoatPluginRevision *revision, uint64_t tick)
{
    run->objective = (BoatPluginObjective) { .generation = ++run->generation, .rng = 12345 };
    run->event_count = 0;
    run->sequence = 0;
    run->objective_pending = false;
    if (!revision)
        return false;
    run->objective.timers_enabled = revision->voyage.manifest.timers;
    run->objective.state_version = revision->voyage.manifest.state_version;
    if (!revision->voyage.manifest.objective)
        return false;
    if (tick > INT64_MAX || !run->generation) {
        run->objective.quarantined = true;
        run->objective_diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_LIMIT };
        snprintf(run->objective_diagnostic.message, sizeof(run->objective_diagnostic.message),
            "objective clock or generation exhausted");
        return false;
    }
    return true;
}

void wake_objective_before_tick(WakeObjective *run)
{
    if (!run->objective_pending)
        return;
    run->objective = run->staged_objective;
    run->objective_pending = false;
    unsigned retired = 0;
    while (
        retired < run->event_count && run->events[retired].sequence <= run->objective.acknowledged)
        ++retired;
    run->event_count -= retired;
    memmove(run->events, run->events + retired, run->event_count * sizeof(run->events[0]));
}

bool wake_objective_queue(WakeObjective *run, const BoatPluginRevision *revision, uint64_t tick,
    const BoatVoyageEvents *events)
{
    if (!run->objective.initialized || run->objective.quarantined)
        return false;
    if (tick > INT64_MAX || run->sequence > INT64_MAX - BOAT_VOYAGE_EVENTS) {
        limit(run, BOAT_PLUGIN_LIMIT, "objective integer clock exhausted");
        return false;
    }
    static const char *const kinds[] = { "", "voyage.shelter_reached", "voyage.exposed",
        "voyage.sheltered", "voyage.arrived", "voyage.recovered", "voyage.weather" };
    for (unsigned i = 0; i < events->count; ++i) {
        if (run->event_count == BOAT_PLUGIN_EVENTS) {
            limit(run, BOAT_PLUGIN_LIMIT, "voyage event queue overflow");
            return false;
        }
        BoatVoyageNotice notice = events->items[i];
        BoatPluginEvent *e = &run->events[run->event_count++];
        *e = (BoatPluginEvent) { .tick = (int64_t)tick, .sequence = ++run->sequence };
        snprintf(e->kind, sizeof(e->kind), "%s", kinds[notice.kind]);
        snprintf(e->id, sizeof(e->id), "%s", revision->voyage.shelter_ids[notice.shelter]);
    }
    return tick % WAKE_OBJECTIVE_CADENCE == 0 && (int64_t)tick > run->objective.tick
        && !run->objective_pending;
}

#ifndef __EMSCRIPTEN__
void wake_objective_init(WakeObjective *run, const BoatPluginRevision *revision, uint64_t tick)
{
    boat_plugin_objective_step(&run->objective, revision->source, revision->source_size,
        BOAT_PLUGIN_OBJECTIVE_INIT, (int64_t)tick, NULL, 0, NULL, &run->objective_diagnostic);
    wake_objective_trace(run, revision->number, BOAT_PLUGIN_OBJECTIVE_INIT, 0, &run->objective, 0);
}

void wake_objective_update(WakeObjective *run, const BoatPluginRevision *revision, uint64_t tick)
{
    run->staged_objective = run->objective;
    if (boat_plugin_objective_step(&run->staged_objective, revision->source, revision->source_size,
            BOAT_PLUGIN_OBJECTIVE_UPDATE, (int64_t)tick, run->events, run->event_count, NULL,
            &run->objective_diagnostic)
        == BOAT_PLUGIN_OK)
        run->objective_pending = true;
    else
        run->objective.quarantined = true;
    wake_objective_trace(run, revision->number, BOAT_PLUGIN_OBJECTIVE_UPDATE,
        boat_plugin_objective_digest(&run->objective),
        run->objective_pending ? &run->staged_objective : &run->objective, run->event_count);
    run->trace[run->trace_count - 1].tick = (int64_t)tick;
}

bool wake_objective_migrate(WakeObjective *run, const BoatPluginRevision *candidate,
    unsigned active, uint64_t tick, BoatPluginObjective *migrated)
{
    // Start from committed T state. Pending old-code output is retired only on success; its
    // unacknowledged input stays queued for the candidate's next update.
    *migrated = run->objective;
    bool exhausted = tick > INT64_MAX || run->generation == UINT_MAX;
    if (exhausted) {
        run->objective_diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_LIMIT };
        snprintf(run->objective_diagnostic.message, sizeof(run->objective_diagnostic.message),
            "objective clock or generation exhausted");
    }
    if (exhausted
        || boat_plugin_objective_migrate(migrated, candidate->source, candidate->source_size,
               candidate->voyage.manifest.state_version, run->generation + 1,
               candidate->voyage.manifest.timers, (int64_t)tick, &run->objective_diagnostic)
            != BOAT_PLUGIN_OK) {
        wake_objective_trace(run, active, BOAT_PLUGIN_OBJECTIVE_MIGRATE,
            boat_plugin_objective_digest(&run->objective), &run->objective, 0);
        return false;
    }
    return true;
}
#endif

void wake_objective_cancel(WakeObjectiveJob *job, const WakeObjectiveWorker *worker)
{
    if (job->job && worker && worker->close)
        worker->close(job->job);
    free(job->request);
    *job = (WakeObjectiveJob) { 0 };
}

bool wake_objective_begin(WakeObjective *run, WakeObjectiveJob *job,
    const WakeObjectiveWorker *worker, BoatPluginTransition action,
    const BoatPluginRevision *revision, uint64_t epoch, uint64_t tick, double now)
{
    static uint64_t serial;
    BoatPluginEvalRequest *r = NULL;
    unsigned char *bytes = NULL;
    unsigned started = 0;
    if (!job->job && worker && worker->start && serial != UINT64_MAX && tick <= INT64_MAX) {
        r = calloc(1, sizeof(*r));
        bytes = malloc(BOAT_PLUGIN_EVAL_REQUEST_MAX);
    }
    if (r && bytes) {
        r->serial = ++serial;
        r->epoch = epoch;
        r->revision = r->candidate = revision->number;
        r->transition = action;
        r->tick = (int64_t)tick;
        r->objective = run->objective;
        r->source_size = revision->source_size;
        memcpy(r->source, revision->source, r->source_size);
        r->version = revision->voyage.manifest.state_version;
        r->generation = run->generation;
        r->timers = revision->voyage.manifest.timers;
        if (action == BOAT_PLUGIN_OBJECTIVE_UPDATE) {
            r->event_count = run->event_count;
            memcpy(r->events, run->events, r->event_count * sizeof(r->events[0]));
        }
        size_t size = boat_plugin_eval_encode_request(r, bytes, BOAT_PLUGIN_EVAL_REQUEST_MAX);
        if (size)
            started = worker->start(bytes, (unsigned)size);
    }
    free(bytes);
    if (!started) {
        free(r);
        limit(run, BOAT_PLUGIN_IO,
            action == BOAT_PLUGIN_OBJECTIVE_INIT ? "cannot start isolated objective initialization"
                                                 : "cannot start isolated objective update");
        return false;
    }
    *job = (WakeObjectiveJob) {
        .job = started, .deadline = now + WAKE_OBJECTIVE_DEADLINE, .request = r
    };
    return true;
}

bool wake_objective_poll(
    WakeObjective *run, WakeObjectiveJob *job, const WakeObjectiveWorker *worker, double now)
{
    if (!job->job)
        return false;
    const BoatPluginEvalRequest *r = job->request;
    int status = worker->status(job->job);
    bool stale = run->generation != r->generation
        || run->objective.quarantined != r->objective.quarantined
        || boat_plugin_objective_digest(&run->objective)
            != boat_plugin_objective_digest(&r->objective);
    if (isnan(job->deadline))
        job->deadline = now + WAKE_OBJECTIVE_DEADLINE; // Begun before any elapsed time was known.
    bool expired = !isfinite(now) || now >= job->deadline;
    if (!status && !stale && !expired)
        return false;
    unsigned char *bytes = malloc(BOAT_PLUGIN_EVAL_REPLY_MAX);
    int size = status == 1 && bytes ? worker->read(job->job, bytes) : -1;
    static BoatPluginObjective result;
    BoatPluginDiagnostic d = { .status = BOAT_PLUGIN_IO };
    bool accepted = !stale && !expired && size > 0
        && boat_plugin_eval_result(r, bytes, (size_t)size, &result, &d);
    free(bytes);
    if (!accepted) {
        d.status = BOAT_PLUGIN_IO;
        snprintf(d.message, sizeof(d.message), "%s",
            stale         ? "stale objective receipt"
                : expired ? "isolated objective deadline"
                          : "invalid or failed objective worker reply");
    }
    BoatPluginTransition action = r->transition;
    bool update = action == BOAT_PLUGIN_OBJECTIVE_UPDATE;
    unsigned number = r->candidate, events = update ? r->event_count : 0;
    int64_t tick = r->tick;
    uint64_t before = update ? boat_plugin_objective_digest(&run->objective) : 0;
    wake_objective_cancel(job, worker);
    run->objective_diagnostic = d;
    // The trace wake_objective_init and wake_objective_update record.
    if (accepted && d.status == BOAT_PLUGIN_OK && update) {
        run->staged_objective = result;
        run->objective_pending = true;
        wake_objective_trace(run, number, action, before, &run->staged_objective, events);
    } else {
        if (accepted && d.status == BOAT_PLUGIN_OK)
            run->objective = result;
        else
            run->objective.quarantined = true;
        wake_objective_trace(run, number, action, before, &run->objective, events);
    }
    if (update)
        run->trace[run->trace_count - 1].tick = tick;
    return true;
}

void wake_objective_adopt(
    WakeObjective *run, unsigned revision, const BoatPluginObjective *migrated)
{
    wake_objective_trace(run, revision, BOAT_PLUGIN_OBJECTIVE_MIGRATE,
        boat_plugin_objective_digest(&run->objective), migrated, 0);
    if (migrated != &run->objective)
        run->objective = *migrated;
    run->generation = run->objective.generation;
    run->objective_pending = false;
}

typedef struct {
    char *out;
    size_t capacity, size;
    bool full;
} Text;

// Append one formatted line, or nothing (and stop) when it does not fit.
static void line(Text *text, const char *format, ...)
{
    if (text->full)
        return;
    va_list arguments;
    va_start(arguments, format);
    int n = vsnprintf(text->out + text->size, text->capacity - text->size, format, arguments);
    va_end(arguments);
    if (n < 0 || (size_t)n >= text->capacity - text->size) {
        text->full = true;
        text->out[text->size] = 0;
        return;
    }
    text->size += (size_t)n;
}

size_t wake_objective_inspect(
    const WakeObjective *run, unsigned revision, char *out, size_t capacity, size_t *first)
{
    Text text = { out, capacity, 0, capacity == 0 };
    if (capacity)
        out[0] = 0;
    const BoatPluginObjective *o = &run->objective;
    line(&text, "revision=%u schema=%u generation=%u tick=%lld pending=%u suspended=%u\n", revision,
        o->state_version, run->generation, (long long)o->tick, run->objective_pending,
        o->quarantined);
    *first = text.size;
    line(&text, "rng=%u acknowledged=%lld queued=%u diagnostic=%s\n", o->rng,
        (long long)o->acknowledged, run->event_count, run->objective_diagnostic.message);
    if (run->objective_pending)
        line(&text, "pending_tick=%lld pending_digest=%016llx\n",
            (long long)run->staged_objective.tick,
            (unsigned long long)boat_plugin_objective_digest(&run->staged_objective));
    for (unsigned i = 0; i < o->hud_count; ++i)
        line(&text, "hud id=%s label=%s value=%lld\n", o->hud[i].id, o->hud[i].label,
            (long long)o->hud[i].value);
    for (unsigned i = 0; i < o->timer_count; ++i)
        line(&text, "timer id=%s tick=%lld\n", o->timers[i].id, (long long)o->timers[i].tick);
    // The state bytes as one line of hex.
    static char hex[2 * BOAT_PLUGIN_STATE_BYTES + 1];
    for (unsigned i = 0; i < o->state.size; ++i)
        snprintf(hex + 2 * i, 3, "%02x", o->state.bytes[i]);
    hex[2 * o->state.size] = 0;
    line(&text, "state=%s\n", hex);
    for (unsigned i = 0; i < run->event_count; ++i)
        line(&text, "event tick=%lld sequence=%lld kind=%s id=%s\n", (long long)run->events[i].tick,
            (long long)run->events[i].sequence, run->events[i].kind, run->events[i].id);
    for (unsigned i = 0; i < run->trace_count; ++i) {
        const WakePluginTrace *t = &run->trace[i];
        line(&text, "trace tick=%lld revision=%u action=%u status=%u events=%u %016llx->%016llx\n",
            (long long)t->tick, t->revision, t->transition, t->status, t->events,
            (unsigned long long)t->before, (unsigned long long)t->after);
        for (unsigned j = 0; j < t->command_count; ++j) {
            const BoatPluginCommand *cmd = &t->commands[j];
            line(&text, "command %s generation=%u id=%s label=%s value=%lld\n", cmd->kind,
                cmd->generation, cmd->id, cmd->label, (long long)cmd->value);
        }
    }
    return text.size;
}
