#include "room/boat_room_record_objective.h"

#include <limits.h>
#include <string.h>

typedef struct {
    const uint8_t *input;
    uint8_t *output;
    size_t at, size;
    bool reading, failed;
} Codec;

static void bytes(Codec *c, void *value, size_t size)
{
    if (c->failed || c->at > c->size || size > c->size - c->at) {
        c->failed = true;
        return;
    }
    if (c->reading)
        memcpy(value, c->input + c->at, size);
    else
        memcpy(c->output + c->at, value, size);
    c->at += size;
}

static void integer(Codec *c, uint64_t *value, unsigned width)
{
    uint8_t data[8] = { 0 };
    if (!c->reading)
        for (unsigned i = 0; i < width; ++i)
            data[i] = (uint8_t)(*value >> (8 * i));
    bytes(c, data, width);
    if (c->reading && !c->failed) {
        *value = 0;
        for (unsigned i = 0; i < width; ++i)
            *value |= (uint64_t)data[i] << (8 * i);
    }
}

static void count(Codec *c, unsigned *value, unsigned maximum)
{
    uint64_t n = *value;
    integer(c, &n, 4);
    if (n > maximum)
        c->failed = true;
    else
        *value = (unsigned)n;
}

static void boolean(Codec *c, bool *value)
{
    uint64_t n = *value;
    integer(c, &n, 1);
    if (n > 1)
        c->failed = true;
    else
        *value = n != 0;
}

static void signed64(Codec *c, int64_t *value)
{
    uint64_t n;
    memcpy(&n, value, sizeof(n));
    integer(c, &n, 8);
    memcpy(value, &n, sizeof(n));
}

static void text(Codec *c, char *value, size_t capacity)
{
    uint64_t n = 0;
    if (!c->reading) {
        const char *end = memchr(value, 0, capacity);
        if (!end) {
            c->failed = true;
            return;
        }
        n = (uint64_t)(end - value);
    }
    integer(c, &n, 2);
    if (c->failed || n >= capacity) {
        c->failed = true;
        return;
    }
    bytes(c, value, (size_t)n);
    if (c->reading && !c->failed) {
        if (memchr(value, 0, (size_t)n))
            c->failed = true;
        value[n] = 0;
    }
}

static void scalar(Codec *c, float *value)
{
    uint32_t bits;
    memcpy(&bits, value, sizeof(bits));
    uint64_t n = bits;
    integer(c, &n, 4);
    bits = (uint32_t)n;
    memcpy(value, &bits, sizeof(bits));
}

static void diagnostic(Codec *c, BoatPluginDiagnostic *d)
{
    unsigned status = (unsigned)d->status;
    count(c, &status, BOAT_PLUGIN_SCHEMA);
    d->status = (BoatPluginStatus)status;
    unsigned line = d->line < 0 ? UINT_MAX : (unsigned)d->line;
    count(c, &line, INT_MAX);
    d->line = (int)line;
    text(c, d->file, sizeof(d->file));
    text(c, d->field, sizeof(d->field));
    text(c, d->message, sizeof(d->message));
    integer(c, &d->cost.instructions, 8);
    integer(c, &d->cost.entry_instructions, 8);
    integer(c, &d->cost.callback_instructions, 8);
    uint64_t peak = d->cost.peak_bytes, leaked = d->cost.leaked_bytes;
    integer(c, &peak, 8);
    integer(c, &leaked, 8);
    if (peak > SIZE_MAX || leaked > SIZE_MAX)
        c->failed = true;
    d->cost.peak_bytes = (size_t)peak;
    d->cost.leaked_bytes = (size_t)leaked;
    count(c, &d->cost.fields, UINT_MAX);
}

static void source(Codec *c, BoatRoomObjectiveSource *s)
{
    boolean(c, &s->present);
    count(c, &s->state_version, 65535);
    boolean(c, &s->migrates);
    uint64_t size = s->source_size;
    integer(c, &size, 4);
    if (c->failed || !s->present || !s->state_version || !size || size > BOAT_PLUGIN_SOURCE_MAX) {
        c->failed = true;
        return;
    }
    s->source_size = (size_t)size;
    bytes(c, s->source, s->source_size);
    count(c, &s->marks.count, BOAT_MARKS_MAX);
    for (unsigned i = 0; !c->failed && i < s->marks.count; ++i) {
        BoatMark *m = &s->marks.items[i];
        text(c, m->id, sizeof(m->id));
        text(c, m->name, sizeof(m->name));
        scalar(c, &m->x);
        scalar(c, &m->z);
        scalar(c, &m->radius);
    }
    if (!c->failed && s->marks.count && !boat_marks_validate(&s->marks, NULL))
        c->failed = true;
}

static void checkpoint(Codec *c, BoatRoomObjective *o)
{
    uint64_t version = 1;
    integer(c, &version, 4);
    if (version != 1) {
        c->failed = true;
        return;
    }
    uint8_t objective[BOAT_PLUGIN_STATE_BYTES + 4096];
    size_t encoded = 0;
    if (!c->reading
        && !boat_plugin_objective_encode(&o->objective, objective, sizeof(objective), &encoded)) {
        c->failed = true;
        return;
    }
    uint64_t size = encoded;
    integer(c, &size, 4);
    if (c->failed || !size || size > sizeof(objective)) {
        c->failed = true;
        return;
    }
    bytes(c, objective, (size_t)size);
    if (c->reading && !c->failed
        && !boat_plugin_objective_decode(objective, (size_t)size, &o->objective))
        c->failed = true;
    count(c, &o->objective.command_count, BOAT_PLUGIN_COMMANDS);
    for (unsigned i = 0; !c->failed && i < o->objective.command_count; ++i) {
        BoatPluginCommand *command = &o->objective.commands[i];
        text(c, command->kind, sizeof(command->kind));
        text(c, command->id, sizeof(command->id));
        text(c, command->label, sizeof(command->label));
        signed64(c, &command->value);
        count(c, &command->generation, UINT_MAX);
    }
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
        boolean(c, &o->present[i]);
    signed64(c, &o->sequence);
    count(c, &o->revision, UINT_MAX);
    count(c, &o->event_count, BOAT_PLUGIN_EVENTS);
    int64_t previous_sequence = o->objective.acknowledged;
    int64_t previous_tick = 0;
    for (unsigned i = 0; !c->failed && i < o->event_count; ++i) {
        BoatPluginEvent *e = &o->events[i];
        signed64(c, &e->tick);
        signed64(c, &e->sequence);
        text(c, e->kind, sizeof(e->kind));
        text(c, e->id, sizeof(e->id));
        signed64(c, &e->load);
        signed64(c, &e->crew);
        if (e->tick < previous_tick || e->sequence <= previous_sequence || e->sequence > o->sequence
            || e->crew < 1 || e->crew > BOAT_FLEET_SLOTS
            || (strcmp(e->kind, "crew.joined") && strcmp(e->kind, "crew.left")
                && strcmp(e->kind, "mark.reached")))
            c->failed = true;
        previous_tick = e->tick;
        previous_sequence = e->sequence;
    }
    if (o->sequence < o->objective.acknowledged || !o->revision)
        c->failed = true;
    integer(c, &o->hud_changes, 8);
    boolean(c, &o->hud_changed);
    integer(c, &o->reported_hud_changes, 8);
    boolean(c, &o->reported_quarantined);
    integer(c, &o->reloads, 8);
    unsigned kind = (unsigned)o->reload.kind;
    count(c, &kind, BOAT_ROOM_RELOAD_REJECTED);
    o->reload.kind = (BoatRoomReloadKind)kind;
    integer(c, &o->reload.tick, 8);
    diagnostic(c, &o->reload.diagnostic);
    diagnostic(c, &o->diagnostic);
    boolean(c, &o->has_pending);
    if (o->has_pending)
        source(c, &o->pending);
    boolean(c, &o->has_revision);
    if (o->has_revision)
        source(c, &o->revision_source);
}

size_t boat_room_record_objective_encode(
    const BoatRoomObjective *objective, uint8_t *out, size_t capacity)
{
    if (!objective || !out)
        return 0;
    BoatRoomObjective copy = *objective;
    Codec c = { .output = out, .size = capacity };
    checkpoint(&c, &copy);
    return c.failed ? 0 : c.at;
}

bool boat_room_record_objective_decode(const uint8_t *data, size_t size,
    const BoatRoomObjectiveSource *running_source, BoatRoomObjective *out)
{
    if (!data || !running_source || !out || !running_source->present
        || !running_source->state_version || running_source->state_version > 65535
        || !running_source->source_size || running_source->source_size > BOAT_PLUGIN_SOURCE_MAX
        || (running_source->marks.count && !boat_marks_validate(&running_source->marks, NULL)))
        return false;
    BoatRoomObjective next = { 0 };
    Codec c = { .input = data, .size = size, .reading = true };
    checkpoint(&c, &next);
    if (c.failed || c.at != size)
        return false;
    next.owned = *running_source;
    *out = next;
    out->source = &out->owned;
    return true;
}
