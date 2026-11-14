#include "room/boat_room_objective.h"

#include <stdio.h>
#include <string.h>

static void quarantine(BoatRoomObjective *o, const char *message)
{
    o->objective.quarantined = true;
    if (message && o->diagnostic.status == BOAT_PLUGIN_OK) {
        o->diagnostic.status = BOAT_PLUGIN_LIMIT;
        snprintf(o->diagnostic.message, sizeof(o->diagnostic.message), "%s", message);
    }
}

static bool same_hud(const BoatPluginObjective *a, const BoatPluginObjective *b)
{
    return a->hud_count == b->hud_count
        && !memcmp(a->hud, b->hud, a->hud_count * sizeof(a->hud[0]));
}

bool boat_room_objective_init(BoatRoomObjective *o, const BoatRoomObjectiveSource *source)
{
    memset(o, 0, sizeof(*o));
    o->source = source;
    o->revision = 1;
    o->objective = (BoatPluginObjective) { .generation = 1, .rng = 12345 };
    if (!source || !source->present) {
        quarantine(o, "no room objective");
        return false;
    }
    o->objective.state_version = source->state_version;
    if (boat_plugin_objective_step(&o->objective, source->source, source->source_size,
            BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0, NULL, &o->diagnostic)
        != BOAT_PLUGIN_OK) {
        o->objective.quarantined = true;
        return false;
    }
    o->diagnostic = (BoatPluginDiagnostic) { 0 };
    o->hud_changes = 1;
    o->hud_changed = true;
    return true;
}

void boat_room_objective_event(BoatRoomObjective *o, uint64_t tick, const BoatNetRuleEvent *e)
{
    if (e->kind <= BOAT_NET_RULE_CREW_LEFT && e->slot < BOAT_FLEET_SLOTS)
        o->present[e->slot] = e->kind == BOAT_NET_RULE_CREW_JOINED; // Even while suspended.
    if (o->objective.quarantined || !o->objective.initialized)
        return;
    if (o->event_count == BOAT_PLUGIN_EVENTS) {
        quarantine(o, "room objective event queue full");
        return;
    }
    static const char *const kinds[] = { "crew.joined", "crew.left", "mark.reached" };
    if (e->kind > BOAT_NET_RULE_MARK_REACHED || e->slot >= BOAT_FLEET_SLOTS
        || (e->kind == BOAT_NET_RULE_MARK_REACHED && e->mark >= o->source->marks.count))
        return;
    BoatPluginEvent *event = &o->events[o->event_count++];
    *event = (BoatPluginEvent) {
        .tick = (int64_t)tick, .sequence = ++o->sequence, .crew = e->slot + 1
    };
    snprintf(event->kind, sizeof(event->kind), "%s", kinds[e->kind]);
    if (e->kind == BOAT_NET_RULE_MARK_REACHED)
        snprintf(event->id, sizeof(event->id), "%s", o->source->marks.items[e->mark].id);
}

bool boat_room_objective_stage(
    BoatRoomObjective *o, const BoatRoomObjectiveSource *candidate, BoatPluginDiagnostic *d)
{
    const char *error = NULL;
    if (!o->source || !o->source->present)
        error = "the room has no objective to reload";
    else if (!candidate || !candidate->present)
        error = "the candidate package has no room objective";
    else if (boat_marks_digest(&candidate->marks)
        // A waiting revision decides the marks the objective will run with.
        != boat_marks_digest(o->has_revision ? &o->revision_source.marks : &o->source->marks))
        error = "marks changed; that is a definition revision";
    if (error) {
        if (d) {
            *d = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_SCHEMA };
            snprintf(d->message, sizeof(d->message), "%s", error);
        }
        return false;
    }
    if (o->has_revision) {
        // The commit restarts the objective on the revision's source: keep the newest for it.
        o->revision_source = *candidate;
        return true;
    }
    o->pending = *candidate;
    o->has_pending = true;
    return true;
}

bool boat_room_objective_stage_revision(BoatRoomObjective *o, BoatNetServer *server,
    const BoatRoomDefinition *definition, const BoatRoomObjectiveSource *candidate,
    BoatPluginDiagnostic *d)
{
    const char *error = NULL;
    BoatPluginDiagnostic fail = { .status = BOAT_PLUGIN_SCHEMA };
    if (!o->source || !o->source->present)
        error = "the room has no objective to revise";
    else if (!candidate || !candidate->present)
        error = "the candidate package has no room objective";
    else {
        // The candidate must start: the objective restarts on it at the commit.
        BoatPluginObjective trial
            = { .generation = 1, .rng = 12345, .state_version = candidate->state_version };
        if (boat_plugin_objective_step(&trial, candidate->source, candidate->source_size,
                BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0, NULL, &fail)
            != BOAT_PLUGIN_OK) {
            if (d)
                *d = fail;
            return false;
        }
        static uint8_t bytes[BOAT_ROOM_BYTES_MAX];
        BoatRoomDiagnostic diagnostic = { 0 };
        size_t size = boat_room_definition_encode(definition, bytes, sizeof(bytes));
        if (!size)
            error = "the candidate room definition does not encode";
        else if (!boat_net_server_stage_revision(server, bytes, size, &diagnostic)) {
            snprintf(fail.message, sizeof(fail.message), "room definition %s: %s", diagnostic.field,
                diagnostic.message);
            if (d)
                *d = fail;
            return false;
        }
    }
    if (error) {
        if (d) {
            *d = fail;
            snprintf(d->message, sizeof(d->message), "%s", error);
        }
        return false;
    }
    o->revision_source = *candidate;
    o->has_revision = true;
    o->has_pending = false; // The restart at the commit supersedes a live reload.
    return true;
}

// Apply candidate C at TICK: a staged live reload at a boundary, or (REVISION) the source of a
// definition the server just committed, which always restarts, drops queued mark events (they
// name the old marks) and, if C cannot start, suspends the objective on C's marks. Everything is
// built on copies and committed only when the whole candidate is accepted.
static void apply_source(
    BoatRoomObjective *o, const BoatRoomObjectiveSource *c, uint64_t tick, bool revision)
{
    BoatPluginDiagnostic d = { 0 };
    BoatPluginObjective next = o->objective;
    BoatPluginEvent events[BOAT_PLUGIN_EVENTS];
    unsigned count = o->event_count;
    int64_t sequence = o->sequence;
    bool migrated = !revision && c->migrates && o->objective.initialized;
    BoatPluginStatus status;
    if (migrated) {
        // Migration is stamped with the last committed tick: the update that follows at this
        // boundary must still be strictly later.
        status = boat_plugin_objective_migrate(&next, c->source, c->source_size, c->state_version,
            next.generation + 1, next.timers_enabled, next.tick, &d);
        next.quarantined = false;
        memcpy(events, o->events, count * sizeof(events[0]));
    } else {
        next = (BoatPluginObjective) { .generation = o->objective.generation + 1,
            .rng = 12345,
            .tick = o->objective.tick,
            .state_version = c->state_version };
        status = boat_plugin_objective_step(&next, c->source, c->source_size,
            BOAT_PLUGIN_OBJECTIVE_INIT, next.tick, NULL, 0, NULL, &d);
        // The new code has seen no crew: announce each one present, then the queued marks.
        // Queued joins and leaves are already folded into the presence. A full queue gives up
        // its oldest marks to make room.
        BoatPluginEvent queued[BOAT_PLUGIN_EVENTS];
        unsigned kept = 0, joined = 0;
        for (unsigned i = 0; i < o->event_count; ++i)
            if (!revision && strncmp(o->events[i].kind, "crew.", 5))
                queued[kept++] = o->events[i];
        for (unsigned slot = 0; slot < BOAT_FLEET_SLOTS; ++slot)
            joined += o->present[slot];
        unsigned skip = kept + joined > BOAT_PLUGIN_EVENTS ? kept + joined - BOAT_PLUGIN_EVENTS : 0;
        int64_t at = kept > skip ? queued[skip].tick : (int64_t)tick;
        count = 0;
        for (unsigned slot = 0; slot < BOAT_FLEET_SLOTS; ++slot)
            if (o->present[slot]) {
                events[count]
                    = (BoatPluginEvent) { .tick = at, .sequence = ++sequence, .crew = slot + 1 };
                snprintf(events[count++].kind, sizeof(events[0].kind), "crew.joined");
            }
        for (unsigned i = skip; i < kept; ++i) {
            events[count] = queued[i];
            events[count++].sequence = ++sequence;
        }
    }
    o->reload = (BoatRoomReload) { .tick = tick };
    ++o->reloads;
    if (status != BOAT_PLUGIN_OK) {
        o->reload.kind = BOAT_ROOM_RELOAD_REJECTED;
        o->reload.diagnostic = d;
        if (revision) {
            // The room already runs C's marks: the old source cannot serve them.
            o->owned = *c;
            o->source = &o->owned;
            o->objective.quarantined = true;
            o->diagnostic = d;
            ++o->hud_changes;
            o->hud_changed = true;
        }
        return;
    }
    o->reload.kind = migrated ? BOAT_ROOM_RELOAD_MIGRATED : BOAT_ROOM_RELOAD_RESTARTED;
    o->owned = *c;
    o->source = &o->owned;
    o->objective = next;
    o->diagnostic = (BoatPluginDiagnostic) { 0 };
    memcpy(o->events, events, count * sizeof(events[0]));
    o->event_count = count;
    o->sequence = sequence;
    ++o->revision;
    ++o->hud_changes; // The clients hear of the new revision even when the rows match.
    o->hud_changed = true;
}

bool boat_room_objective_after_tick(BoatRoomObjective *o, uint64_t tick)
{
    if (tick % BOAT_ROOM_OBJECTIVE_CADENCE || (int64_t)tick <= o->objective.tick)
        return false;
    if (o->has_pending) {
        o->has_pending = false;
        apply_source(o, &o->pending, tick, false);
    }
    if (o->objective.quarantined || !o->objective.initialized)
        return false;
    BoatPluginObjective next = o->objective;
    BoatPluginDiagnostic d = { 0 };
    if (boat_plugin_objective_step(&next, o->source->source, o->source->source_size,
            BOAT_PLUGIN_OBJECTIVE_UPDATE, (int64_t)tick, o->events, o->event_count, NULL, &d)
        != BOAT_PLUGIN_OK) {
        o->diagnostic = d;
        o->objective.quarantined = true;
        return false;
    }
    if (!same_hud(&next, &o->objective)) {
        ++o->hud_changes;
        o->hud_changed = true;
    }
    o->objective = next;
    o->event_count = 0; // The batch acknowledged every queued event.
    return true;
}

static void on_event(void *context, uint64_t tick, const BoatNetRuleEvent *event)
{
    boat_room_objective_event(context, tick, event);
}

static void on_after_tick(void *context, uint64_t tick)
{
    boat_room_objective_after_tick(context, tick);
}

// The server committed a definition revision: the objective runs on the source staged with it.
static void on_definition(void *context, uint64_t tick, const BoatRoomDefinition *room)
{
    BoatRoomObjective *o = context;
    (void)room;
    if (!o->has_revision)
        return;
    o->has_revision = false;
    o->has_pending = false;
    apply_source(o, &o->revision_source, tick, true);
}

// The HUD rows and suspension for the clients, once per change. `hud_changed` stays for the
// caller's own reporting.
static bool on_state(void *context, BoatNetObjectiveState *out)
{
    BoatRoomObjective *o = context;
    if (o->reported_hud_changes == o->hud_changes
        && o->reported_quarantined == o->objective.quarantined)
        return false;
    o->reported_hud_changes = o->hud_changes;
    o->reported_quarantined = o->objective.quarantined;
    *out = (BoatNetObjectiveState) { .suspended = o->objective.quarantined,
        .row_count = (uint8_t)o->objective.hud_count };
    for (unsigned i = 0; i < o->objective.hud_count && i < BOAT_NET_OBJECTIVE_ROWS; ++i) {
        snprintf(out->rows[i].id, sizeof(out->rows[i].id), "%s", o->objective.hud[i].id);
        snprintf(out->rows[i].label, sizeof(out->rows[i].label), "%s", o->objective.hud[i].label);
        out->rows[i].value = o->objective.hud[i].value;
    }
    if (out->row_count > BOAT_NET_OBJECTIVE_ROWS)
        out->row_count = BOAT_NET_OBJECTIVE_ROWS;
    return true;
}

BoatNetRules boat_room_objective_rules(BoatRoomObjective *o)
{
    return (BoatNetRules) {
        .context = o,
        .event = on_event,
        .after_tick = on_after_tick,
        .state = on_state,
        .definition = on_definition,
    };
}

static bool same_source(const BoatRoomObjectiveSource *a, const BoatRoomObjectiveSource *b)
{
    return a->present == b->present && a->state_version == b->state_version
        && a->migrates == b->migrates && a->source_size == b->source_size
        && !memcmp(a->source, b->source, a->source_size);
}

void boat_room_reload_watch_init(BoatRoomReloadWatch *watch, const BoatRoomDefinition *room,
    const BoatRoomObjectiveSource *source)
{
    memset(watch, 0, sizeof(*watch));
    watch->seen = *source;
    watch->definition = boat_room_definition_digest(room);
}

void boat_room_reload_offer(BoatRoomReloadWatch *watch, BoatRoomObjective *o, BoatNetServer *server,
    const BoatRoomDefinition *definition, const BoatRoomObjectiveSource *candidate, FILE *out)
{
    uint64_t digest = boat_room_definition_digest(definition);
    if (digest == watch->definition && same_source(candidate, &watch->seen))
        return;
    memcpy(&watch->seen, candidate, sizeof(watch->seen));
    BoatPluginDiagnostic d;
    if (digest != watch->definition) {
        if (!boat_room_objective_stage_revision(o, server, definition, candidate, &d)) {
            watch->announced = 0;
            if (out) {
                fprintf(out, "objective reload rejected: %s\n", d.message);
                fflush(out);
            }
            return;
        }
        watch->definition = digest;
        uint32_t next = server->stats.revision + 1;
        if (out) {
            if (!boat_net_server_players(server))
                fprintf(out,
                    "objective reload: marks changed; revision %u commits at once (the room is "
                    "empty)\n",
                    next);
            else if (watch->announced != next)
                fprintf(out,
                    "objective reload: marks changed; revision %u waits for an empty room\n", next);
            fflush(out);
        }
        watch->announced = next;
    } else if (!boat_room_objective_stage(o, candidate, &d) && out) {
        fprintf(out, "objective reload rejected: %s\n", d.message);
        fflush(out);
    }
}
