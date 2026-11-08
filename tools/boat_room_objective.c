#include "boat_room_objective.h"

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

bool boat_room_objective_after_tick(BoatRoomObjective *o, uint64_t tick)
{
    if (o->objective.quarantined || !o->objective.initialized || tick % BOAT_ROOM_OBJECTIVE_CADENCE
        || (int64_t)tick <= o->objective.tick)
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

BoatNetRules boat_room_objective_rules(BoatRoomObjective *o)
{
    return (BoatNetRules) { .context = o, .event = on_event, .after_tick = on_after_tick };
}
