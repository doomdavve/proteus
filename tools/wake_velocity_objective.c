#include "wake_velocity_objective.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
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
