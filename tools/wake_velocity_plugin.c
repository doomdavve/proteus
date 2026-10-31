#include "wake_velocity_plugin.h"
#include "boat_plugin_eval.h"
#include "boat_ui.h"
#include "waterfront_traffic.h"

#ifdef _WIN32
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, browser_owner, (unsigned revision, unsigned low, unsigned high, const char *status), {
    Module.boatPluginOwner = [ revision >>> 0, low >>> 0, high >>> 0 ];
    if (Module.boatPluginNotice)
        Module.boatPluginNotice(UTF8ToString(status));
})
EM_JS(bool, browser_editing, (void), { return !!Module.boatPluginEditing; })
EM_JS(unsigned, browser_actions, (void), {
    const flags = Module.boatPluginActions || 0;
    Module.boatPluginActions = 0;
    return flags;
})
#endif

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void report(WakePlugin *plugin, const char *outcome, bool alert, const char *detail,
    const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(plugin->status, sizeof(plugin->status), format, arguments);
    va_end(arguments);
    snprintf(plugin->detail, sizeof(plugin->detail), "%s", detail);
    plugin->alert = alert;
    plugin->outcome = outcome;
#ifdef __EMSCRIPTEN__
    if (plugin->host.depth) {
        browser_owner(boat_plugin_host_active(&plugin->host)->number, (unsigned)plugin->host.epoch,
            (unsigned)(plugin->host.epoch >> 32), plugin->status);
    }
#endif
    fprintf(stderr, "Plugin %s: %s%s%s\n", plugin->directory, plugin->status,
        detail[0] ? " / " : "", detail);
}

static void report_failure(WakePlugin *plugin)
{
    const BoatPluginDiagnostic *d = &plugin->host.diagnostic;
    char where[96];
    snprintf(where, sizeof(where), "%s%s%.0d%s%s", d->file, d->line ? ":" : "", d->line,
        d->field[0] ? " " : "", d->field);
    report(plugin, "rejected", true, d->message, "%s ERROR %s; KEEPING R%u",
        boat_plugin_status_name(d->status), where, boat_plugin_host_active(&plugin->host)->number);
}

static BoatPluginStatus read_package(WakePlugin *plugin, uint64_t *key)
{
    BoatPluginDiagnostic d;
    BoatPluginStatus status = boat_plugin_read(plugin->directory, NULL, &plugin->files, &d);
    *key = boat_plugin_read_key(status, &plugin->files, &d);
    if (status != BOAT_PLUGIN_OK)
        plugin->host.diagnostic = d;
    return status;
}

const BoatWorldDefinition *wake_plugin_location(const char *directory)
{
    static BoatPluginFiles files;
    BoatPluginDiagnostic d;
    BoatPluginManifest manifest;
    if (boat_plugin_read(directory, NULL, &files, &d) != BOAT_PLUGIN_OK
        || boat_plugin_parse_manifest(files.manifest, files.manifest_size, NULL, &manifest, &d)
            != BOAT_PLUGIN_OK) {
        fprintf(stderr, "Plugin %s: %s error in %s %s: %s\n", directory,
            boat_plugin_status_name(d.status), d.file, d.field, d.message);
        return NULL;
    }
    BoatPluginKind kind = boat_plugin_kind_parse(manifest.kind);
    if (kind != BOAT_PLUGIN_KIND_VOYAGE && kind != BOAT_PLUGIN_KIND_FREE
        && kind != BOAT_PLUGIN_KIND_COURSE) {
        fprintf(stderr,
            "--plugin accepts voyage, free and course packages; race and match run in rooms.\n");
        return NULL;
    }
    return boat_location_find(manifest.location);
}

static void trace(WakePlugin *p, BoatPluginTransition action, uint64_t before,
    const BoatPluginObjective *after, unsigned events)
{
    if (p->trace_count == WAKE_PLUGIN_TRACE) {
        memmove(p->trace, p->trace + 1, (WAKE_PLUGIN_TRACE - 1) * sizeof(p->trace[0]));
        --p->trace_count;
    }
    WakePluginTrace *entry = &p->trace[p->trace_count++];
    *entry = (WakePluginTrace) { .tick = after->tick,
        .revision = boat_plugin_host_active(&p->host)->number,
        .events = events,
        .transition = action,
        .status = p->objective_diagnostic.status,
        .before = before,
        .after = boat_plugin_objective_digest(after) };
    if (entry->status == BOAT_PLUGIN_OK) {
        entry->command_count = after->command_count;
        memcpy(entry->commands, after->commands, sizeof(entry->commands));
    }
}

// A callback receipt is private to its request and complete committed input. No imported
// Lua is evaluated by this adapter in a browser, including initialization and Undo.
static bool commit(WakePlugin *plugin, BoatSession *session);
static void close_stage(WakePlugin *plugin)
{
    boat_plugin_stage_close(plugin->staging);
    plugin->staging = NULL;
}
// The active revision owns the scene; the session borrows it for physics and presentation.
// Absent scene content restores the compiled marina without touching the world.
static void bind_scene(WakePlugin *plugin, BoatSession *session)
{
    const BoatPluginRevision *revision = boat_plugin_host_active(&plugin->host);
    session->world.scene
        = plugin->host.kind == BOAT_PLUGIN_KIND_VOYAGE && revision->voyage.has_scene
        ? &revision->voyage.scene
        : NULL;
}

static bool scene_differs(const BoatPluginRevision *active, const BoatPluginRevision *candidate)
{
    return active->voyage.has_scene != candidate->voyage.has_scene
        || active->voyage.scene_digest != candidate->voyage.scene_digest;
}
#ifdef __EMSCRIPTEN__
struct WakePluginEvaluation {
    BoatPluginEvalRequest request;
    unsigned job;
    double deadline;
};
EM_JS(unsigned, objective_start, (const unsigned char *bytes, unsigned size), {
    return Module.boatPluginObjective
        ? Module.boatPluginObjective.start(HEAPU8.slice(bytes, bytes + size))
        : 0;
})
EM_JS(int, objective_status, (unsigned job),
    { return Module.boatPluginObjective ? Module.boatPluginObjective.status(job) : -1; })
EM_JS(int, objective_read, (unsigned job, unsigned char *bytes), {
    const data = Module.boatPluginObjective ?.read(job);
    if (!data || data.length > 49152)
        return -1;
    HEAPU8.set(data, bytes);
    return data.length;
})
EM_JS(void, objective_close, (unsigned job), { Module.boatPluginObjective ?.close(job); })
EM_JS(void, objective_shutdown, (void), { Module.boatPluginObjective ?.reset(); })
EM_JS(void, objective_snapshot,
    (unsigned low, unsigned high, unsigned native_low, unsigned native_high, unsigned generation,
        bool initialized, bool quarantined, unsigned events, bool waiting,
        const unsigned char *bytes, unsigned size),
    {
        Module.boatPluginCheckpoint = {
            tick : [ low, high ],
            nativeTick : [ native_low, native_high ],
            generation,
            initialized : !!initialized,
            quarantined : !!quarantined,
            events,
            waiting : !!waiting,
            state : HEAPU8.slice(bytes, bytes + size)
        };
    })
static void close_evaluation(WakePlugin *plugin)
{
    if (plugin->evaluation) {
        objective_close(plugin->evaluation->job);
        free(plugin->evaluation);
        plugin->evaluation = NULL;
    }
}
static bool begin_evaluation(
    WakePlugin *p, BoatPluginTransition action, int64_t tick, const BoatPluginRevision *revision)
{
    static uint64_t serial;
    if (p->evaluation || serial == UINT64_MAX)
        return false;
    struct WakePluginEvaluation *e = calloc(1, sizeof(*e));
    unsigned char *bytes = malloc(BOAT_PLUGIN_EVAL_REQUEST_MAX);
    if (!e || !bytes) {
        free(e);
        free(bytes);
        return false;
    }
    BoatPluginEvalRequest *r = &e->request;
    r->serial = ++serial;
    r->epoch = p->host.epoch;
    r->revision = boat_plugin_host_active(&p->host)->number;
    r->candidate = revision->number;
    r->transition = action;
    r->tick = tick;
    r->objective = p->objective;
    r->source_size = revision->source_size;
    memcpy(r->source, revision->source, r->source_size);
    r->version = revision->voyage.manifest.state_version;
    r->generation = action == BOAT_PLUGIN_OBJECTIVE_MIGRATE ? p->generation + 1 : p->generation;
    r->timers = revision->voyage.manifest.timers;
    if (action == BOAT_PLUGIN_OBJECTIVE_UPDATE) {
        r->event_count = p->event_count;
        memcpy(r->events, p->events, r->event_count * sizeof(r->events[0]));
    }
    size_t size = boat_plugin_eval_encode_request(r, bytes, BOAT_PLUGIN_EVAL_REQUEST_MAX);
    if (size)
        e->job = objective_start(bytes, (unsigned)size);
    free(bytes);
    if (!e->job) {
        free(e);
        return false;
    }
    e->deadline = p->frame_now + 5;
    p->evaluation = e;
    return true;
}
static bool poll_evaluation(WakePlugin *p, BoatSession *session, double now)
{
    struct WakePluginEvaluation *e = p->evaluation;
    if (!e)
        return false;
    int status = objective_status(e->job);
    bool stale = p->host.epoch != e->request.epoch
        || boat_plugin_host_active(&p->host)->number != e->request.revision
        || session->tick != (uint64_t)e->request.tick
        || p->objective.quarantined != e->request.objective.quarantined
        || boat_plugin_objective_digest(&p->objective)
            != boat_plugin_objective_digest(&e->request.objective);
    bool migration = e->request.transition == BOAT_PLUGIN_OBJECTIVE_MIGRATE;
    if (migration) {
        const BoatPluginRevision *held = boat_plugin_host_held(&p->host);
        stale |= !held || held->number != e->request.candidate;
    }
    bool expired = !isfinite(now) || now >= e->deadline;
    if (!status && !stale && !expired)
        return false;
    unsigned char *bytes = malloc(BOAT_PLUGIN_EVAL_REPLY_MAX);
    int size = status == 1 && bytes ? objective_read(e->job, bytes) : -1;
    BoatPluginObjective result;
    BoatPluginDiagnostic d = { .status = BOAT_PLUGIN_IO };
    bool accepted = !stale && !expired && size > 0
        && boat_plugin_eval_result(&e->request, bytes, (size_t)size, &result, &d);
    free(bytes);
    if (!accepted) {
        d.status = BOAT_PLUGIN_IO;
        snprintf(d.message, sizeof(d.message), "%s",
            stale         ? "stale objective receipt"
                : expired ? "isolated objective deadline"
                          : "invalid or failed objective worker reply");
    }
    BoatPluginTransition action = e->request.transition;
    uint64_t before = boat_plugin_objective_digest(&p->objective);
    unsigned events = e->request.event_count;
    close_evaluation(p);
    p->objective_diagnostic = d;
    if (accepted && d.status == BOAT_PLUGIN_OK) {
        p->staged_objective = result;
        if (migration) {
            p->evaluation_migrated = true;
            return commit(p, session);
        }
        if (action == BOAT_PLUGIN_OBJECTIVE_INIT || action == BOAT_PLUGIN_OBJECTIVE_RESET)
            p->objective = result; // Initialization completes before any native step is permitted.
        else
            p->objective_pending = true;
        trace(p, action, before, &p->staged_objective, events);
    } else {
        if (!migration)
            p->objective.quarantined = true;
        trace(p, action, before, &p->objective, events);
        report(p, migration ? "migration_failed" : "objective_suspended", true, d.message,
            migration ? "MIGRATION FAILED; ACTIVE REVISION KEPT" : "OBJECTIVE SUSPENDED");
    }
    return false;
}
#endif

bool wake_plugin_waiting(const WakePlugin *plugin) { return plugin->evaluation != NULL; }

void wake_plugin_reset_objective(WakePlugin *plugin, const BoatSession *session)
{
#ifdef __EMSCRIPTEN__
    close_evaluation(plugin);
#endif
    plugin->objective = (BoatPluginObjective) { .generation = ++plugin->generation, .rng = 12345 };
    plugin->event_count = 0;
    plugin->sequence = 0;
    plugin->objective_pending = false;
    if (!plugin->host.depth || plugin->host.kind != BOAT_PLUGIN_KIND_VOYAGE)
        return;
    const BoatPluginRevision *r = boat_plugin_host_active(&plugin->host);
    plugin->objective.timers_enabled = r->voyage.manifest.timers;
    plugin->objective.state_version = r->voyage.manifest.state_version;
    if (r->voyage.manifest.objective) {
        if (session->tick > INT64_MAX || !plugin->generation) {
            plugin->objective.quarantined = true;
            plugin->objective_diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_LIMIT };
            snprintf(plugin->objective_diagnostic.message,
                sizeof(plugin->objective_diagnostic.message),
                "objective clock or generation exhausted");
            return;
        }
#ifdef __EMSCRIPTEN__
        if (!begin_evaluation(plugin, BOAT_PLUGIN_OBJECTIVE_INIT, (int64_t)session->tick, r)) {
            plugin->objective.quarantined = true;
            plugin->objective_diagnostic.status = BOAT_PLUGIN_IO;
            snprintf(plugin->objective_diagnostic.message,
                sizeof(plugin->objective_diagnostic.message),
                "cannot start isolated objective initialization");
        }
#else
        boat_plugin_objective_step(&plugin->objective, r->source, r->source_size,
            BOAT_PLUGIN_OBJECTIVE_INIT, (int64_t)session->tick, NULL, 0, NULL,
            &plugin->objective_diagnostic);
        trace(plugin, BOAT_PLUGIN_OBJECTIVE_INIT, 0, &plugin->objective, 0);
#endif
    }
}

void wake_plugin_before_tick(WakePlugin *plugin)
{
    if (plugin->objective_pending) {
        plugin->objective = plugin->staged_objective;
        plugin->objective_pending = false;
        unsigned retired = 0;
        while (retired < plugin->event_count
            && plugin->events[retired].sequence <= plugin->objective.acknowledged)
            ++retired;
        plugin->event_count -= retired;
        memmove(plugin->events, plugin->events + retired,
            plugin->event_count * sizeof(plugin->events[0]));
    }
}

void wake_plugin_after_tick(WakePlugin *plugin, const BoatSession *session)
{
    if (!plugin->objective.initialized || plugin->objective.quarantined)
        return;
    if (session->tick > INT64_MAX || plugin->sequence > INT64_MAX - BOAT_VOYAGE_EVENTS) {
        plugin->objective.quarantined = true;
        plugin->objective_pending = false;
        plugin->objective_diagnostic.status = BOAT_PLUGIN_LIMIT;
        snprintf(plugin->objective_diagnostic.message, sizeof(plugin->objective_diagnostic.message),
            "objective integer clock exhausted");
        return;
    }
    static const char *const kinds[] = { "", "voyage.shelter_reached", "voyage.exposed",
        "voyage.sheltered", "voyage.arrived", "voyage.recovered", "voyage.weather" };
    const BoatPluginRevision *r = boat_plugin_host_active(&plugin->host);
    for (unsigned i = 0; i < session->voyage_events.count; ++i) {
        if (plugin->event_count == BOAT_PLUGIN_EVENTS) {
            plugin->objective.quarantined = true;
            plugin->objective_pending = false;
            plugin->objective_diagnostic.status = BOAT_PLUGIN_LIMIT;
            snprintf(plugin->objective_diagnostic.message,
                sizeof(plugin->objective_diagnostic.message), "voyage event queue overflow");
            return;
        }
        BoatVoyageNotice notice = session->voyage_events.items[i];
        BoatPluginEvent *e = &plugin->events[plugin->event_count++];
        *e = (BoatPluginEvent) { .tick = (int64_t)session->tick, .sequence = ++plugin->sequence };
        snprintf(e->kind, sizeof(e->kind), "%s", kinds[notice.kind]);
        snprintf(e->id, sizeof(e->id), "%s", r->voyage.shelter_ids[notice.shelter]);
    }
    if (session->tick % 12 || (int64_t)session->tick <= plugin->objective.tick
        || plugin->objective_pending)
        return;
#ifdef __EMSCRIPTEN__
    if (!begin_evaluation(plugin, BOAT_PLUGIN_OBJECTIVE_UPDATE, (int64_t)session->tick, r)) {
        plugin->objective.quarantined = true;
        plugin->objective_diagnostic.status = BOAT_PLUGIN_IO;
        snprintf(plugin->objective_diagnostic.message, sizeof(plugin->objective_diagnostic.message),
            "cannot start isolated objective update");
    }
#else
    plugin->staged_objective = plugin->objective;
    if (boat_plugin_objective_step(&plugin->staged_objective, r->source, r->source_size,
            BOAT_PLUGIN_OBJECTIVE_UPDATE, (int64_t)session->tick, plugin->events,
            plugin->event_count, NULL, &plugin->objective_diagnostic)
        == BOAT_PLUGIN_OK)
        plugin->objective_pending = true;
    else
        plugin->objective.quarantined = true;
    trace(plugin, BOAT_PLUGIN_OBJECTIVE_UPDATE, boat_plugin_objective_digest(&plugin->objective),
        plugin->objective_pending ? &plugin->staged_objective : &plugin->objective,
        plugin->event_count);
    plugin->trace[plugin->trace_count - 1].tick = (int64_t)session->tick;
#endif
}

bool wake_plugin_open(WakePlugin *plugin, const char *directory, BoatSession *session, double now)
{
    if (session->voyage_route != session->world.definition->voyage) {
        fprintf(stderr, "Plugin voyage already has an owner; second owner rejected.\n");
        return false;
    }
    // Keep the large revision history out of temporary stack storage on Windows.
    memset(plugin, 0, sizeof(*plugin));
    plugin->directory = directory;
    plugin->authoring = true;
    plugin->frame_now = now;
    uint64_t key;
    bool read = read_package(plugin, &key) == BOAT_PLUGIN_OK;
    BoatPluginBundle bundle = boat_plugin_bundle(&plugin->files);
#ifdef __EMSCRIPTEN__
    (void)bundle;
    // Only a completed, byte-matched browser-worker receipt may launch an imported package.
    // Callback execution also uses worker receipts; no imported Lua runs on this thread.
    BoatPluginManifest manifest;
    bool parsed = read
        && boat_plugin_parse_manifest(
               plugin->files.manifest, plugin->files.manifest_size, NULL, &manifest,
               &plugin->host.diagnostic)
            == BOAT_PLUGIN_OK;
    plugin->host.location = session->world.definition;
    plugin->host.map = &session->world.resources->map;
    plugin->host.kind = parsed ? boat_plugin_kind_parse(manifest.kind) : BOAT_PLUGIN_KIND_NONE;
    BoatPluginStageJob *launch = read && parsed && plugin->host.kind != BOAT_PLUGIN_KIND_NONE
        ? boat_plugin_stage_start(NULL, &plugin->host, &plugin->files, now)
        : NULL;
    bool started = launch && boat_plugin_stage_poll(launch, &plugin->host, now) == 1;
    boat_plugin_stage_close(launch);
    plugin->stage_executable = "browser-worker";
#else
    bool started = read
        && boat_plugin_host_start(
            &plugin->host, session->world.definition, &session->world.resources->map, &bundle);
#endif
    if (!started) {
        const BoatPluginDiagnostic *d = &plugin->host.diagnostic;
        fprintf(stderr, "Plugin %s: %s error in %s%s%.0d %s: %s\n", directory,
            boat_plugin_status_name(d->status), d->file, d->line ? ":" : "", d->line, d->field,
            d->message);
        return false;
    }
#if !defined(__EMSCRIPTEN__)
    plugin->watching = true;
    boat_plugin_watch_start(&plugin->watch, key, now);
#endif
    const BoatPluginRevision *active = boat_plugin_host_active(&plugin->host);
    switch (plugin->host.kind) {
    case BOAT_PLUGIN_KIND_VOYAGE:
        // At tick zero nothing has been observed, so this is the voyage's start.
        boat_session_restart_voyage(session, &active->voyage.route);
        wake_plugin_reset_objective(plugin, session);
        break;
    case BOAT_PLUGIN_KIND_COURSE:
        boat_session_set_course_definition(session, &active->course.course);
        break;
    default:
        break;
    }
    bind_scene(plugin, session);
    uint64_t digest = 0;
    switch (plugin->host.kind) {
    case BOAT_PLUGIN_KIND_VOYAGE: digest = active->voyage.digest; break;
    case BOAT_PLUGIN_KIND_RACE: digest = active->race.digest; break;
    case BOAT_PLUGIN_KIND_FREE: digest = active->free.digest; break;
    case BOAT_PLUGIN_KIND_COURSE: digest = active->course.digest; break;
    case BOAT_PLUGIN_KIND_MATCH: digest = active->match.digest; break;
    default: break;
    }
    report(plugin, "active", false,
#ifdef __EMSCRIPTEN__
        "PLUGIN PANEL: STAGE / APPLY / UNDO",
#else
        "F5 RELOAD / F6 UNDO / SAVES RELOAD",
#endif
        "%s R1 ACTIVE %016llx", active->manifest.id, (unsigned long long)digest);
    return true;
}

bool wake_plugin_open_catalog(
    WakePlugin *plugin, const BoatPluginCatalogEntry *entry, BoatSession *session)
{
    if (session->voyage_route != session->world.definition->voyage) {
        fprintf(stderr, "Plugin voyage already has an owner; second owner rejected.\n");
        return false;
    }
    memset(plugin, 0, sizeof(*plugin));
    plugin->directory = entry->id;
    plugin->authoring = false;
    plugin->frame_now = 0;
#ifdef __EMSCRIPTEN__
    // The browser stages edits in a worker; there is no executable path.
    plugin->stage_executable = "browser-worker";
#endif
    BoatPluginRevision revision;
    if (!boat_plugin_catalog_decode(entry, &revision, &plugin->host.diagnostic)
        || !boat_plugin_host_start_decoded(
            &plugin->host, session->world.definition, &session->world.resources->map, &revision)) {
        const BoatPluginDiagnostic *d = &plugin->host.diagnostic;
        fprintf(stderr, "Package %s: %s error in %s %s: %s\n", entry->id,
            boat_plugin_status_name(d->status), d->file, d->field, d->message);
        return false;
    }
    const BoatPluginRevision *active = boat_plugin_host_active(&plugin->host);
    if (plugin->host.kind == BOAT_PLUGIN_KIND_VOYAGE) {
        boat_session_restart_voyage(session, &active->voyage.route);
        wake_plugin_reset_objective(plugin, session);
    } else if (plugin->host.kind == BOAT_PLUGIN_KIND_COURSE) {
        boat_session_set_course_definition(session, &active->course.course);
    }
    bind_scene(plugin, session);
    report(plugin, "active", false, "", "%s ACTIVE %016llx", active->manifest.name,
        (unsigned long long)entry->digest);
    return true;
}

// Install the held candidate under its policy. True when the session must restart.
static bool commit(WakePlugin *plugin, BoatSession *session)
{
    BoatPluginChange change = plugin->host.change;
    bool undo = plugin->host.undo;
    const BoatPluginRevision *held = boat_plugin_host_held(&plugin->host);
    if (change == BOAT_PLUGIN_MIGRATE && held) {
#ifdef __EMSCRIPTEN__
        if (!plugin->evaluation_migrated) {
            if (session->tick > INT64_MAX || plugin->generation == UINT_MAX
                || !begin_evaluation(
                    plugin, BOAT_PLUGIN_OBJECTIVE_MIGRATE, (int64_t)session->tick, held))
                report(plugin, "migration_failed", true, "cannot start isolated migration",
                    "MIGRATION FAILED; ACTIVE REVISION KEPT");
            return false;
        }
        plugin->evaluation_migrated = false;
#else
        // Start from committed T state. Pending old-code output is retired only on success;
        // its unacknowledged input stays queued for the candidate's next update.
        BoatPluginObjective migrated = plugin->objective;
        bool exhausted = session->tick > INT64_MAX || plugin->generation == UINT_MAX;
        if (exhausted) {
            plugin->objective_diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_LIMIT };
            snprintf(plugin->objective_diagnostic.message,
                sizeof(plugin->objective_diagnostic.message),
                "objective clock or generation exhausted");
        }
        if (exhausted
            || boat_plugin_objective_migrate(&migrated, held->source, held->source_size,
                   held->voyage.manifest.state_version, plugin->generation + 1,
                   held->voyage.manifest.timers, (int64_t)session->tick,
                   &plugin->objective_diagnostic)
                != BOAT_PLUGIN_OK) {
            trace(plugin, BOAT_PLUGIN_OBJECTIVE_MIGRATE,
                boat_plugin_objective_digest(&plugin->objective), &plugin->objective, 0);
            report(plugin, "migration_failed", true, plugin->objective_diagnostic.message,
                "MIGRATION FAILED; KEEPING R%u", boat_plugin_host_active(&plugin->host)->number);
            return false;
        }
        // Commit below cannot fail once the base has been checked.
        if (plugin->host.base != boat_plugin_host_active(&plugin->host)->number)
            return false;
        plugin->staged_objective = migrated;
#endif
    }
    bool scene_restart = change == BOAT_PLUGIN_RESTART_SESSION && held
        && scene_differs(boat_plugin_host_active(&plugin->host), held);
    const BoatPluginRevision *previous = boat_plugin_host_active(&plugin->host);
    const char *shelter_id = plugin->host.kind == BOAT_PLUGIN_KIND_VOYAGE
            && session->voyage.shelter < previous->voyage.route.shelter_count
        ? previous->voyage.shelter_ids[session->voyage.shelter]
        : NULL;
    const BoatPluginRevision *revision = boat_plugin_host_commit(&plugin->host);
    if (!revision)
        return false;
    bind_scene(plugin, session);
    const BoatVoyageRoute *route = &revision->voyage.route;
    const char *how = undo ? "UNDONE TO" : "ACTIVE";
    switch (change) {
    case BOAT_PLUGIN_UNCHANGED:
    case BOAT_PLUGIN_RENAMED:
        boat_session_rebind_voyage(session, route);
        report(plugin, "applied", false, "PROGRESS KEPT", "R%u %s, %s", revision->number, how,
            boat_plugin_change_name(change));
        return false;
    case BOAT_PLUGIN_LIVE: {
        const char *ids[BOAT_VOYAGE_SHELTERS] = { 0 };
        unsigned count = revision->voyage.route.shelter_count;
        if (count > BOAT_VOYAGE_SHELTERS)
            count = BOAT_VOYAGE_SHELTERS;
        for (unsigned i = 0; i < count; ++i)
            ids[i] = revision->voyage.shelter_ids[i];
        if (plugin->host.kind == BOAT_PLUGIN_KIND_VOYAGE)
            boat_session_rebind_voyage_live(session, route, shelter_id, ids, count);
        report(plugin, "applied", false, "BOAT KEPT", "R%u %s, %s", revision->number, how,
            boat_plugin_change_name(change));
        return false;
    }
    case BOAT_PLUGIN_MIGRATE:
        trace(plugin, BOAT_PLUGIN_OBJECTIVE_MIGRATE,
            boat_plugin_objective_digest(&plugin->objective), &plugin->staged_objective, 0);
        plugin->objective = plugin->staged_objective;
        plugin->generation = plugin->objective.generation;
        plugin->objective_pending = false;
        boat_session_rebind_voyage(session, route);
        report(plugin, "applied", false, "STATE MIGRATED; NATIVE PROGRESS KEPT", "R%u %s",
            revision->number, how);
        return false;
    case BOAT_PLUGIN_RESTART_VOYAGE:
        boat_session_restart_voyage(session, route);
        wake_plugin_reset_objective(plugin, session);
        report(plugin, "applied", false, "VOYAGE RESTARTED AT THE START SHELTER", "R%u %s",
            revision->number, how);
        return false;
    default:
        session->voyage_route = route; // The caller's restart begins it.
        report(plugin, "applied", false,
            scene_restart ? "SESSION RESTARTED WITH THE NEW MARINA LAYOUT"
                          : "SESSION RESTARTED WITH THE NEW WATER",
            "R%u %s", revision->number, how);
        return true;
    }
}

// Apply a rename now; hold a restart for APPLY.
static bool offer(WakePlugin *plugin, BoatSession *session)
{
    const BoatPluginRevision *held = boat_plugin_host_held(&plugin->host);
    BoatPluginChange change = plugin->host.change;
    if (change == BOAT_PLUGIN_INCOMPATIBLE) {
        report(plugin, "incompatible", true,
            "PLUGIN ID, KIND OR LOCATION CHANGED: RELAUNCH TO USE IT", "NOT APPLIED; KEEPING R%u",
            boat_plugin_host_active(&plugin->host)->number);
        return false;
    }
    if (!held) {
        report(plugin, "unchanged", false, "", "R%u UNCHANGED",
            boat_plugin_host_active(&plugin->host)->number);
        return false;
    }
    if (change == BOAT_PLUGIN_UNCHANGED || change == BOAT_PLUGIN_RENAMED)
        return commit(plugin, session);
    if (change == BOAT_PLUGIN_LIVE) {
        if (wake_plugin_live_ready(plugin, session))
            return commit(plugin, session);
        report(plugin, "held", true, "WAITING FOR CLEAR WATER; THE BOAT IS NOT MOVED",
            "R%u LIVE EDIT HELD", held->number);
        return false;
    }
    if (change == BOAT_PLUGIN_MIGRATE) {
        report(plugin, "held", true, "F7 MIGRATE STATE / F6 DISCARD", "R%u %s OBJECTIVE",
            held->number, plugin->host.undo ? "(UNDO) CHANGES" : "CHANGES");
        return false;
    }
    bool water = change == BOAT_PLUGIN_RESTART_SESSION;
    bool scene = water && scene_differs(boat_plugin_host_active(&plugin->host), held);
    report(plugin, "held", true,
        water ? "F7 RESTART SESSION / F6 DISCARD" : "F7 RESTART VOYAGE / F6 DISCARD", "R%u %s %s",
        held->number, plugin->host.undo ? "(UNDO) CHANGES" : "CHANGES",
        scene       ? "THE MARINA LAYOUT"
            : water ? "THE EXPOSED WATER"
                    : "THE ROUTE");
    return false;
}

static bool stage(WakePlugin *plugin, BoatSession *session, bool read_ok, double now)
{
    ++plugin->requests;
    close_stage(plugin); // Superseding a request also reaps its worker.
    BoatPluginBundle bundle = boat_plugin_bundle(&plugin->files);
    if (read_ok && plugin->stage_executable) {
        plugin->staging
            = boat_plugin_stage_start(plugin->stage_executable, &plugin->host, &plugin->files, now);
        if (plugin->staging) {
            report(plugin, "staging", false, "F6 CANCEL / ACTIVE REVISION KEEPS RUNNING",
                "STAGING EDIT");
            return false;
        }
        plugin->host.diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_IO };
        snprintf(plugin->host.diagnostic.message, sizeof(plugin->host.diagnostic.message),
            "cannot start isolated staging worker");
        boat_plugin_host_discard(&plugin->host);
        report_failure(plugin);
        return false;
    }
    if (!read_ok) {
        boat_plugin_host_discard(&plugin->host);
        report_failure(plugin);
        return false;
    }
#ifdef __EMSCRIPTEN__
    (void)bundle;
    (void)session;
    plugin->host.diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_IO };
    snprintf(plugin->host.diagnostic.message, sizeof(plugin->host.diagnostic.message),
        "browser reload requires isolated staging");
    boat_plugin_host_discard(&plugin->host);
    report_failure(plugin);
    return false;
#else
    if (!boat_plugin_host_stage(&plugin->host, &bundle)) {
        report_failure(plugin);
        return false;
    }
    return offer(plugin, session);
#endif
}

bool wake_plugin_stage_files(
    WakePlugin *plugin, BoatSession *session, const BoatPluginFiles *files, double now)
{
    if (!plugin->authoring)
        return false;
    // Bytes the selected package holds now count as its save, whether or not a poll has seen
    // them yet; other bytes leave polling alone, so a later edit of the directory still stages.
    static BoatPluginFiles disk;
    BoatPluginDiagnostic ignored;
    uint64_t key = boat_plugin_read_key(BOAT_PLUGIN_OK, files, NULL);
    if (plugin->watching
        && boat_plugin_read(plugin->directory, NULL, &disk, &ignored) == BOAT_PLUGIN_OK
        && boat_plugin_read_key(BOAT_PLUGIN_OK, &disk, NULL) == key)
        boat_plugin_watch_staged(&plugin->watch, key);
    plugin->files = *files;
    return stage(plugin, session, true, now);
}

void wake_plugin_close(WakePlugin *plugin, BoatSession *session)
{
    close_stage(plugin);
    if (plugin->query) {
        boat_plugin_stage_close(plugin->query);
        plugin->query = NULL;
    }
    if (session) {
        // The session borrowed the plugin's route and scene. Restore both compiled defaults
        // before the host storage may be reused, so re-entry never sees a dangling borrower.
        if (plugin->host.kind == BOAT_PLUGIN_KIND_VOYAGE) {
            session->voyage_route = session->world.definition->voyage;
            if (session->voyage.route)
                session->voyage.route = session->voyage_route;
        } else if (plugin->host.kind == BOAT_PLUGIN_KIND_COURSE) {
            boat_session_set_course_definition(session, NULL);
        }
        session->world.scene = NULL;
    }
#ifdef __EMSCRIPTEN__
    close_evaluation(plugin);
    objective_shutdown();
#endif
}

bool wake_plugin_editing(void)
{
#ifdef __EMSCRIPTEN__
    return browser_editing();
#else
    return false;
#endif
}

// PC live console -----------------------------------------------------------------------------

static void json_text(char *out, size_t capacity, const char *text)
{
    size_t at = 0;
    for (const char *c = text; *c && at + 7 < capacity; ++c) {
        unsigned char value = (unsigned char)*c;
        if (*c == '"' || *c == '\\') {
            out[at++] = '\\';
            out[at++] = *c;
        } else if (value < 32) {
            at += (size_t)snprintf(out + at, capacity - at, "\\u%04x", value);
        } else {
            out[at++] = *c;
        }
    }
    out[at] = 0;
}

// Canonical manifest text for a catalog revision. PD1 stores the parsed manifest, not its source
// text; reserialising it changes no meaning, and the catalog generator emits the same fields.
static size_t manifest_json(const BoatPluginManifest *m, char *out, size_t capacity)
{
    char id[192], name[192], kind[32], entry[32], location[32];
    json_text(id, sizeof(id), m->id);
    json_text(name, sizeof(name), m->name);
    json_text(kind, sizeof(kind), m->kind);
    json_text(entry, sizeof(entry), m->entry);
    json_text(location, sizeof(location), m->location);
    int n = snprintf(out, capacity,
        "{\"id\":\"%s\",\"name\":\"%s\",\"api\":%u,\"kind\":\"%s\",\"entry\":\"%s\","
        "\"location\":\"%s\",\"capabilities\":[\"%s.define\"%s%s%s],\"state_version\":%u}",
        id, name, m->api, kind, entry, location, m->kind, m->scene ? ",\"scene.define\"" : "",
        m->objective ? ",\"objective.hud\"" : "", m->timers ? ",\"objective.timers\"" : "",
        m->state_version);
    return n > 0 && (size_t)n < capacity ? (size_t)n : 0;
}

bool wake_plugin_promote(WakePlugin *plugin)
{
    if (plugin->authoring)
        return true;
    if (!plugin->host.depth)
        return false;
    const BoatPluginRevision *active = boat_plugin_host_active(&plugin->host);
    size_t manifest_size
        = manifest_json(&active->manifest, plugin->files.manifest, sizeof(plugin->files.manifest));
    if (!manifest_size || active->source_size > sizeof(plugin->files.source))
        return false;
    plugin->files.manifest_size = manifest_size;
    plugin->files.source_size = active->source_size;
    memcpy(plugin->files.source, active->source, active->source_size);
    plugin->authoring = true;
    plugin->watching = false;
    plugin->directory = NULL;
    return true;
}

bool wake_plugin_stage_source(
    WakePlugin *plugin, BoatSession *session, const char *source, size_t size, double now)
{
    if (!plugin->authoring || !plugin->stage_executable || !source || !size
        || size > sizeof(plugin->files.source) || !plugin->files.manifest_size)
        return false;
    memcpy(plugin->files.source, source, size);
    plugin->files.source_size = size;
    wake_plugin_stage_files(plugin, session, &plugin->files, now);
    return plugin->staging != NULL;
}

size_t wake_plugin_manifest(const WakePlugin *plugin, const char **text)
{
    *text = plugin->files.manifest;
    return plugin->authoring ? plugin->files.manifest_size : 0;
}

size_t wake_plugin_source(const WakePlugin *plugin, const char **text)
{
    const BoatPluginRevision *active = boat_plugin_host_active(&plugin->host);
    *text = active->source;
    return plugin->host.depth ? active->source_size : 0;
}

bool wake_plugin_grant(WakePlugin *plugin, const char *capability, char *message, size_t capacity)
{
    if (!plugin->host.depth || plugin->host.kind != BOAT_PLUGIN_KIND_VOYAGE) {
        snprintf(message, capacity, "only a voyage package can take optional grants");
        return false;
    }
    if (strcmp(capability, "scene.define")) {
        snprintf(message, capacity, "unknown capability %s", capability);
        return false;
    }
    if (boat_plugin_host_active(&plugin->host)->manifest.scene) {
        snprintf(message, capacity, "%s is already granted", capability);
        return false;
    }
    if (!wake_plugin_promote(plugin)) {
        snprintf(message, capacity, "cannot promote the session to authoring");
        return false;
    }
    const char *text = plugin->files.manifest;
    const char *key = strstr(text, "\"capabilities\"");
    const char *open = key ? strchr(key, '[') : NULL;
    const char *close = open ? strchr(open, ']') : NULL;
    if (!open || !close) {
        snprintf(message, capacity, "the manifest has no capabilities array");
        return false;
    }
    bool content = false;
    for (const char *c = open + 1; c < close; ++c)
        content |= *c != ' ' && *c != '\n' && *c != '\r' && *c != '\t';
    char out[BOAT_PLUGIN_MANIFEST_MAX];
    int n = snprintf(out, sizeof(out), "%.*s%s\"%s\"%s", (int)(close - text), text,
        content ? ", " : "", capability, close);
    if (n < 0 || (size_t)n >= sizeof(out) || (size_t)n > BOAT_PLUGIN_MANIFEST_MAX) {
        snprintf(message, capacity, "the manifest would exceed its size limit");
        return false;
    }
    BoatPluginManifest parsed;
    BoatPluginDiagnostic d;
    if (boat_plugin_parse_manifest(out, (size_t)n, NULL, &parsed, &d) != BOAT_PLUGIN_OK
        || !parsed.scene) {
        snprintf(message, capacity, "%s", d.message[0] ? d.message : "the grant is not valid");
        return false;
    }
    memcpy(plugin->files.manifest, out, (size_t)n);
    plugin->files.manifest_size = (size_t)n;
    return true;
}

bool wake_plugin_query_begin(
    WakePlugin *plugin, const char *source, size_t size, const char *expression, double now)
{
    if (plugin->query || !plugin->stage_executable || !plugin->host.depth)
        return false;
    // A query can run before the first edit promotes the session. Build the manifest text here
    // without changing the runtime/authoring mode.
    char manifest[BOAT_PLUGIN_MANIFEST_MAX];
    const char *text = plugin->files.manifest;
    size_t manifest_size = plugin->authoring ? plugin->files.manifest_size : 0;
    if (!manifest_size) {
        manifest_size = manifest_json(
            &boat_plugin_host_active(&plugin->host)->manifest, manifest, sizeof(manifest));
        text = manifest;
    }
    if (!manifest_size)
        return false;
    plugin->query = boat_plugin_query_start(
        plugin->stage_executable, text, manifest_size, source, size, expression, now);
    return plugin->query != NULL;
}

int wake_plugin_query_result(WakePlugin *plugin, char *text, size_t capacity, double now)
{
    if (!plugin->query)
        return -1;
    BoatPluginDiagnostic d = { 0 };
    int result = boat_plugin_query_poll(plugin->query, text, capacity, &d, now);
    if (result) {
        boat_plugin_stage_close(plugin->query);
        plugin->query = NULL;
        if (result < 0)
            snprintf(text, capacity, "%s", d.message[0] ? d.message : "query failed");
    }
    return result;
}

bool wake_plugin_query_pending(const WakePlugin *plugin) { return plugin->query != NULL; }

bool wake_plugin_live_ready(const WakePlugin *plugin, const BoatSession *session)
{
    const BoatPluginRevision *held = boat_plugin_host_held(&plugin->host);
    if (!held || plugin->host.change != BOAT_PLUGIN_LIVE
        || plugin->host.kind != BOAT_PLUGIN_KIND_VOYAGE)
        return false;
    const BoatSceneDefinition *scene = held->voyage.has_scene ? &held->voyage.scene : NULL;
    return boat_scene_clear(
        &session->boat, &session->world, scene, boat_session_time(session), .5f);
}

#if !defined(__EMSCRIPTEN__)
static bool directory_nonempty(const char *directory)
{
#ifdef _WIN32
    char pattern[1100];
    snprintf(pattern, sizeof(pattern), "%s/*", directory);
    WIN32_FIND_DATAA entry;
    HANDLE found = FindFirstFileA(pattern, &entry);
    if (found == INVALID_HANDLE_VALUE)
        return false;
    bool nonempty = strcmp(entry.cFileName, ".") && strcmp(entry.cFileName, "..");
    FindClose(found);
    return nonempty;
#else
    DIR *dir = opendir(directory);
    if (!dir)
        return false;
    bool nonempty = false;
    for (struct dirent *entry; (entry = readdir(dir)) && !nonempty;)
        nonempty = strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");
    closedir(dir);
    return nonempty;
#endif
}

static bool replace_file(const char *directory, const char *name, const char *bytes, size_t size)
{
    char path[1100], temporary[1100];
    if (snprintf(path, sizeof(path), "%s/%s", directory, name) >= (int)sizeof(path)
        || snprintf(temporary, sizeof(temporary), "%s/.%s.tmp", directory, name)
            >= (int)sizeof(temporary))
        return false;
    FILE *file = fopen(temporary, "wb");
    if (!file)
        return false;
    bool ok = fwrite(bytes, 1, size, file) == size;
    ok = !fclose(file) && ok;
#ifdef _WIN32
    remove(path); // rename does not replace on Windows.
#endif
    if (!ok || rename(temporary, path)) {
        remove(temporary);
        return false;
    }
    return true;
}
#endif

bool wake_plugin_save(
    const WakePlugin *plugin, const char *directory, char *message, size_t capacity)
{
#if defined(__EMSCRIPTEN__)
    (void)plugin;
    (void)directory;
    snprintf(message, capacity, "the browser exports downloads instead");
    return false;
#else
    if (!plugin || !plugin->host.depth || !directory || !*directory) {
        snprintf(message, capacity, "no package to save");
        return false;
    }
#ifdef _WIN32
    if (_mkdir(directory) && errno != EEXIST) {
        snprintf(message, capacity, "cannot create %s", directory);
        return false;
    }
#else
    if (mkdir(directory, 0700) && errno != EEXIST) {
        snprintf(message, capacity, "cannot create %s", directory);
        return false;
    }
#endif
    bool own = plugin->directory && !strcmp(plugin->directory, directory);
    if (!own && directory_nonempty(directory)) {
        snprintf(
            message, capacity, "%s is not empty and does not belong to this session", directory);
        return false;
    }
    const char *manifest = plugin->files.manifest;
    size_t manifest_size = plugin->files.manifest_size;
    char generated[BOAT_PLUGIN_MANIFEST_MAX];
    if (!plugin->authoring || !manifest_size) {
        manifest_size = manifest_json(
            &boat_plugin_host_active(&plugin->host)->manifest, generated, sizeof(generated));
        manifest = generated;
    }
    const BoatPluginRevision *active = boat_plugin_host_active(&plugin->host);
    if (!manifest_size || !active->source_size
        || !replace_file(directory, "plugin.json", manifest, manifest_size)
        || !replace_file(directory, "main.lua", active->source, active->source_size)) {
        snprintf(message, capacity, "cannot write the package files");
        return false;
    }
    snprintf(message, capacity, "SAVED %s", directory);
    return true;
#endif
}

bool wake_plugin_frame(
    WakePlugin *plugin, BoatSession *session, bool reload, bool undo, bool apply, double now)
{
    if (!plugin->authoring)
        return false;
    plugin->frame_now = now;
#ifdef __EMSCRIPTEN__
    unsigned actions = browser_actions();
    if (plugin->evaluation
        && plugin->evaluation->request.transition == BOAT_PLUGIN_OBJECTIVE_MIGRATE
        && ((actions & (2 | 8)) || undo)) {
        close_evaluation(plugin);
        boat_plugin_host_discard(&plugin->host);
        report(plugin, "canceled", false, "ACTIVE REVISION AND STATE KEPT", "MIGRATION CANCELED");
        return false;
    }
    if ((actions & 8) && plugin->staging) {
        close_stage(plugin);
        boat_plugin_host_discard(&plugin->host);
        report(plugin, "canceled", false, "", "STAGING CANCELED; ACTIVE REVISION KEPT");
        actions &= ~8u;
    }
    bool restart = poll_evaluation(plugin, session, now);
    objective_snapshot((unsigned)plugin->objective.tick,
        (unsigned)((uint64_t)plugin->objective.tick >> 32), (unsigned)session->tick,
        (unsigned)(session->tick >> 32), plugin->generation, plugin->objective.initialized,
        plugin->objective.quarantined, plugin->event_count, wake_plugin_waiting(plugin),
        plugin->objective.state.bytes, plugin->objective.state.size);
    if (restart)
        return true;
    if (wake_plugin_waiting(plugin)) {
        // Control actions are refused visibly rather than applied to an in-flight checkpoint.
        if (actions || reload || undo || apply)
            report(plugin, "busy", false, "RETRY WHEN THE CALLBACK FINISHES",
                "OBJECTIVE EVALUATION BUSY");
        return false;
    }
    reload |= (actions & 1) != 0;
    undo |= (actions & 2) != 0;
    apply |= (actions & 4) != 0;
#endif
    if (apply && !plugin->staging && boat_plugin_host_held(&plugin->host)) {
        close_stage(plugin);
        return commit(plugin, session);
    }
    if (undo) {
        if (plugin->staging) {
            close_stage(plugin);
            boat_plugin_host_discard(&plugin->host);
            report(plugin, "canceled", false, "", "STAGING CANCELED; ACTIVE REVISION KEPT");
            return false;
        }
        if (boat_plugin_host_held(&plugin->host)) {
            boat_plugin_host_discard(&plugin->host);
            report(plugin, "discarded", false, "", "DISCARDED; KEEPING R%u",
                boat_plugin_host_active(&plugin->host)->number);
            return false;
        }
        if (!boat_plugin_host_undo(&plugin->host)) {
            report(plugin, "nothing_to_undo", false, "", "NOTHING TO UNDO");
            return false;
        }
        return offer(plugin, session);
    }
    uint64_t key;
    if (reload) {
        bool ok = read_package(plugin, &key) == BOAT_PLUGIN_OK;
        if (plugin->watching)
            boat_plugin_watch_staged(&plugin->watch, key);
        return stage(plugin, session, ok, now);
    }
    if (plugin->watching && boat_plugin_watch_due(&plugin->watch, now)) {
        bool ok = read_package(plugin, &key) == BOAT_PLUGIN_OK;
        if (boat_plugin_watch_settled(&plugin->watch, key, now))
            return stage(plugin, session, ok, now);
    }
    if (plugin->staging) {
        int ready = boat_plugin_stage_poll(plugin->staging, &plugin->host, now);
        if (ready) {
            close_stage(plugin);
            if (ready < 0) {
                report_failure(plugin);
                return false;
            }
            return offer(plugin, session);
        }
    }
    // A held live edit commits at the first boundary where the hull clears its candidate
    // envelopes. It never moves the boat and needs no APPLY.
    if (!plugin->staging && boat_plugin_host_held(&plugin->host)
        && plugin->host.change == BOAT_PLUGIN_LIVE && wake_plugin_live_ready(plugin, session))
        return commit(plugin, session);
    return false;
}

void wake_plugin_draw(const WakePlugin *plugin, int x, int bottom)
{
    const Color accent = { 90, 240, 255, 255 }, warning = { 255, 161, 0, 255 };
    char objective_lines[BOAT_PLUGIN_HUD][192] = { 0 };
    char *objective_line = objective_lines[0];
    if (plugin->objective.quarantined)
        snprintf(objective_line, sizeof(objective_lines[0]), "OBJECTIVE SUSPENDED: %.150s",
            plugin->objective_diagnostic.message);
    else {
        BoatPluginHudOwner owner
            = { boat_plugin_host_active(&plugin->host)->voyage.manifest.id, &plugin->objective };
        BoatPluginHudRow rows[BOAT_PLUGIN_HUD];
        unsigned count = 0;
        boat_plugin_hud_compose(&owner, 1, rows, BOAT_PLUGIN_HUD, &count);
        for (unsigned i = 0; i < count; ++i) {
            const BoatPluginHud *row = &rows[i].row;
            snprintf(objective_lines[i], sizeof(objective_lines[i]), "%s: %lld", row->label,
                (long long)row->value);
        }
    }
    const char *lines[2 + BOAT_PLUGIN_HUD] = { plugin->status };
    int rows = 1;
    if (plugin->detail[0])
        lines[rows++] = plugin->detail;
    for (unsigned i = 0; i < BOAT_PLUGIN_HUD; ++i)
        if (objective_lines[i][0])
            lines[rows++] = objective_lines[i];
    int width = 0;
    for (int i = 0; i < rows; ++i) {
        int w = boat_ui_width(lines[i], 12);
        width = w > width ? w : width;
    }
    int top = bottom - rows * 16 - 4;
    DrawRectangle(x - 4, top, width + 8, rows * 16 + 4, (Color) { 14, 23, 32, 220 });
    for (int i = 0; i < rows; ++i)
        boat_ui_text(lines[i], x, top + 3 + i * 16, 12,
            i == 0 ? (plugin->alert ? warning : accent) : RAYWHITE);
}

// Route strings point into revision storage: ordinary struct assignment is insufficient.
static void copy_plugin(WakePlugin *destination, const WakePlugin *source)
{
    *destination = *source;
    for (unsigned i = 0; i < BOAT_PLUGIN_UNDO + 2; ++i)
        boat_plugin_voyage_copy(&destination->host.slots[i].voyage, &source->host.slots[i].voyage);
}

static bool preview_session(const BoatSession *s)
{
    const BoatReplayRuns *r = &s->runs;
    if (s->world.water.objects)
        for (unsigned i = 1; i < BOAT_OBJECT_BOATS; ++i)
            if (s->world.water.objects->emitters[i].initialized)
                return false; // A crossing/fleet fixture needs its other boats checkpointed too.
    return !s->record_runs && !s->course.enabled && !s->world.combat && !s->world.wake_field
        && !s->world.water.radial && !r->playing && !r->export_path && !r->recording.samples
        && !r->last.samples && !r->best.samples && !r->opponent.samples && !r->reference.samples
        && !r->save_best && !r->export_last && !r->report_finish;
}

bool wake_plugin_preview_save(WakePluginPreview *c, const WakePlugin *p, const BoatSession *s)
{
    if (p->staging || p->evaluation || !preview_session(s) || !p->host.depth
        || p->host.kind != BOAT_PLUGIN_KIND_VOYAGE
        || s->voyage_route != &boat_plugin_host_active(&p->host)->voyage.route
        || s->voyage.route != s->voyage_route)
        return false;
    copy_plugin(&c->plugin, p);
    c->plugin.watching = false;
    c->plugin.stage_executable = NULL;
    c->session = *s;
    c->has_objects = s->world.water.objects != NULL;
    c->has_traffic = s->world.water.traffic_objects != NULL;
    if (c->has_objects)
        c->objects = *s->world.water.objects;
    if (c->has_traffic)
        c->traffic = *s->world.water.traffic_objects;
    c->session.world.water.objects = c->has_objects ? &c->objects : NULL;
    c->session.world.water.traffic_objects = c->has_traffic ? &c->traffic : NULL;
    const BoatPluginRevision *active = boat_plugin_host_active(&c->plugin.host);
    c->session.voyage_route = &active->voyage.route;
    c->session.voyage.route = c->session.voyage_route;
    c->session.world.scene = active->voyage.has_scene ? &active->voyage.scene : NULL;
    c->valid = true;
    return true;
}

bool wake_plugin_preview_restore(const WakePluginPreview *c, WakePlugin *p, BoatSession *s,
    BoatWakeObjects *objects, BoatWakeObjects *traffic)
{
    if (p->staging || p->evaluation || !c->valid || c->plugin.evaluation || !preview_session(s)
        || s->world.resources != c->session.world.resources || (c->has_objects && !objects)
        || (c->has_traffic && !traffic) || (c->has_objects && c->has_traffic && objects == traffic))
        return false;
    copy_plugin(p, &c->plugin);
    *s = c->session;
    if (c->has_objects)
        *objects = c->objects;
    if (c->has_traffic)
        *traffic = c->traffic;
    s->world.water.objects = c->has_objects ? objects : NULL;
    s->world.water.traffic_objects = c->has_traffic ? traffic : NULL;
    const BoatPluginRevision *active = boat_plugin_host_active(&p->host);
    s->voyage_route = &active->voyage.route;
    s->voyage.route = s->voyage_route;
    s->world.scene = active->voyage.has_scene ? &active->voyage.scene : NULL;
    return true;
}

bool wake_plugin_preview_seek(const WakePluginPreview *c, WakePlugin *p, BoatSession *s,
    BoatWakeObjects *objects, BoatWakeObjects *traffic, const WakePluginReviewTick *ticks,
    unsigned count)
{
    if ((count && !ticks) || c->session.tick > INT64_MAX - (uint64_t)count
        || !wake_plugin_preview_restore(c, p, s, objects, traffic))
        return false;
    for (unsigned i = 0; i < count; ++i) {
        if (ticks[i].recover) {
            boat_session_recover(s);
            wake_plugin_after_tick(p, s);
        }
        wake_plugin_before_tick(p);
        if (c->has_objects)
            boat_wake_objects_advance(objects);
        if (c->has_traffic
            && !waterfront_traffic_wakes_step(traffic, boat_session_time(s) + BOAT_TICK_SECONDS))
            return false;
        if (!boat_session_step(s, ticks[i].input))
            return false;
        if (c->has_objects) {
            BoatWakeStern stern = boat_wake_stern(&s->boat, &s->world, boat_session_time(s));
            if (!boat_wake_objects_track(objects, 0, (Vector2) { (float)stern.x, (float)stern.z },
                    (float)hypot(stern.vx, stern.vz), (float)stern.wet))
                return false;
        }
        wake_plugin_after_tick(p, s);
    }
    return true;
}

void wake_plugin_inspect(const WakePlugin *p, FILE *output)
{
    BoatPluginHostMemory memory = boat_plugin_host_memory(&p->host);
    fprintf(output,
        "memory native=%zu host=%zu revisions=%u source=%zu lua_limit=%zu owned_ceiling=%zu "
        "preview=%zu staging_native=%zu\n",
        sizeof(*p), memory.reserved_bytes, memory.retained_revisions, memory.retained_source_bytes,
        boat_plugin_default_limits.memory_bytes,
        sizeof(*p) + boat_plugin_default_limits.memory_bytes, sizeof(WakePluginPreview),
        boat_plugin_stage_bytes(p->staging));
    const BoatPluginRevision *active = boat_plugin_host_active(&p->host);
    fprintf(output, "revision=%u schema=%u generation=%u tick=%lld pending=%u suspended=%u\n",
        active->number, p->objective.state_version, p->generation, (long long)p->objective.tick,
        p->objective_pending, p->objective.quarantined);
    fprintf(output,
        "scene present=%u version=%u digest=%016llx content=%u berths=%u modules=%u "
        "entrance=%u vessels=%u flocks=%u\n",
        active->voyage.has_scene, active->voyage.scene.version,
        (unsigned long long)active->voyage.scene_digest,
        boat_scene_has_content(&active->voyage.scene), active->voyage.scene.berth_count,
        active->voyage.scene.module_count, active->voyage.scene.entrance_present,
        active->voyage.scene.traffic.actor_count, active->voyage.scene.gulls.flock_count);
    fprintf(output, "rng=%u acknowledged=%lld queued=%u diagnostic=%s\n", p->objective.rng,
        (long long)p->objective.acknowledged, p->event_count, p->objective_diagnostic.message);
    if (p->objective_pending)
        fprintf(output, "pending_tick=%lld pending_digest=%016llx\n",
            (long long)p->staged_objective.tick,
            (unsigned long long)boat_plugin_objective_digest(&p->staged_objective));
    for (unsigned i = 0; i < p->objective.hud_count; ++i)
        fprintf(output, "hud id=%s label=%s value=%lld\n", p->objective.hud[i].id,
            p->objective.hud[i].label, (long long)p->objective.hud[i].value);
    for (unsigned i = 0; i < p->objective.timer_count; ++i)
        fprintf(output, "timer id=%s tick=%lld\n", p->objective.timers[i].id,
            (long long)p->objective.timers[i].tick);
    fprintf(output, "state=");
    for (unsigned i = 0; i < p->objective.state.size; ++i)
        fprintf(output, "%02x", p->objective.state.bytes[i]);
    fprintf(output, "\n");
    for (unsigned i = 0; i < p->event_count; ++i)
        fprintf(output, "event tick=%lld sequence=%lld kind=%s id=%s\n",
            (long long)p->events[i].tick, (long long)p->events[i].sequence, p->events[i].kind,
            p->events[i].id);
    for (unsigned i = 0; i < p->trace_count; ++i) {
        const WakePluginTrace *t = &p->trace[i];
        fprintf(output,
            "trace tick=%lld revision=%u action=%u status=%u events=%u %016llx->%016llx\n",
            (long long)t->tick, t->revision, t->transition, t->status, t->events,
            (unsigned long long)t->before, (unsigned long long)t->after);
        for (unsigned j = 0; j < t->command_count; ++j) {
            const BoatPluginCommand *cmd = &t->commands[j];
            fprintf(output, "command %s generation=%u id=%s label=%s value=%lld\n", cmd->kind,
                cmd->generation, cmd->id, cmd->label, (long long)cmd->value);
        }
    }
}
