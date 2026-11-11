#include "boat_plugin_eval.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const unsigned char *input;
    unsigned char *output;
    size_t size, at;
    bool ok;
} Wire;

static void bytes(Wire *w, void *data, size_t n)
{
    if (!w->ok || n > w->size - w->at) {
        w->ok = false;
        return;
    }
    if (w->input)
        memcpy(data, w->input + w->at, n);
    else
        memcpy(w->output + w->at, data, n);
    w->at += n;
}

static uint64_t integer(Wire *w, uint64_t value, unsigned n)
{
    unsigned char b[8] = { 0 };
    for (unsigned i = 0; i < n; ++i)
        b[n - i - 1] = (unsigned char)(value >> (8 * i));
    bytes(w, b, n);
    value = 0;
    for (unsigned i = 0; i < n; ++i)
        value = (value << 8) | b[i];
    return value;
}

static int64_t signed_integer(Wire *w, int64_t value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    bits = integer(w, bits, 8);
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool boolean(Wire *w, bool value)
{
    uint64_t n = integer(w, value, 1);
    if (n > 1)
        w->ok = false;
    return n != 0;
}

static void string(Wire *w, char *value, size_t capacity)
{
    const char *end = w->input ? value : memchr(value, 0, capacity);
    if (!end) {
        w->ok = false;
        return;
    }
    size_t n = (size_t)integer(w, w->input ? 0 : (size_t)(end - value), 4);
    if (n >= capacity) {
        w->ok = false;
        return;
    }
    bytes(w, value, n);
    if (memchr(value, 0, n))
        w->ok = false;
    value[n] = 0;
}

static bool text(const char *value, size_t capacity, bool empty)
{
    const char *end = memchr(value, 0, capacity);
    if (!end || (!empty && end == value))
        return false;
    for (const char *p = value; p < end; ++p)
        if ((unsigned char)*p < 32 || (unsigned char)*p > 126)
            return false;
    return true;
}

// The existing canonical state format is little endian internally. Validate it directly,
// including record ordering and the empty-table representation, without constructing a VM.
typedef struct {
    const BoatPluginState *state;
    size_t at;
    unsigned tables;
} StateReader;

static bool state_value(StateReader *r, unsigned depth)
{
    if (depth > BOAT_PLUGIN_STATE_DEPTH || r->at >= r->state->size)
        return false;
    unsigned tag = r->state->bytes[r->at++];
    if (tag == 't' || tag == 'f')
        return true;
    if (tag == 'i') {
        if (r->state->size - r->at < 8)
            return false;
        r->at += 8;
        return true;
    }
    if (tag != 's' && tag != 'a' && tag != 'r')
        return false;
    if (r->state->size - r->at < 2)
        return false;
    unsigned n = r->state->bytes[r->at] | ((unsigned)r->state->bytes[r->at + 1] << 8);
    r->at += 2;
    if (tag == 's') {
        if (n > 1024 || n > r->state->size - r->at)
            return false;
        r->at += n;
        return true;
    }
    if (n > 256 || (tag == 'a' && !n) || ++r->tables > BOAT_PLUGIN_STATE_TABLES)
        return false;
    char previous[64] = { 0 };
    for (unsigned i = 0; i < n; ++i) {
        if (tag == 'r') {
            if (r->at >= r->state->size)
                return false;
            unsigned length = r->state->bytes[r->at++];
            if (!length || length > 63 || length > r->state->size - r->at)
                return false;
            char key[64] = { 0 };
            memcpy(key, r->state->bytes + r->at, length);
            if (memchr(key, 0, length) || (i && strcmp(previous, key) >= 0))
                return false;
            memcpy(previous, key, sizeof(key));
            r->at += length;
        }
        if (!state_value(r, depth + 1))
            return false;
    }
    return true;
}

static bool checkpoint(const BoatPluginObjective *o)
{
    if (o->state.size > BOAT_PLUGIN_STATE_BYTES || o->hud_count > BOAT_PLUGIN_HUD
        || o->timer_count > BOAT_PLUGIN_TIMERS || o->command_count > BOAT_PLUGIN_COMMANDS
        || !o->generation || !o->rng || o->state_version > 65535 || o->tick < 0
        || o->acknowledged < 0 || (!o->timers_enabled && o->timer_count))
        return false;
    StateReader r = { .state = &o->state };
    if (o->state.size) {
        if (!state_value(&r, 0) || r.at != o->state.size)
            return false;
    } else if (o->initialized)
        return false;
    for (unsigned i = 0; i < o->hud_count; ++i) {
        if (!text(o->hud[i].id, sizeof(o->hud[i].id), false)
            || !text(o->hud[i].label, sizeof(o->hud[i].label), false))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(o->hud[i].id, o->hud[j].id))
                return false;
    }
    for (unsigned i = 0; i < o->timer_count; ++i) {
        const BoatPluginTimer *t = &o->timers[i];
        if (!text(t->id, sizeof(t->id), false) || t->tick <= 0)
            return false;
        if (i
            && (o->timers[i - 1].tick > t->tick
                || (o->timers[i - 1].tick == t->tick && strcmp(o->timers[i - 1].id, t->id) >= 0)))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(o->timers[j].id, t->id))
                return false;
    }
    for (unsigned i = 0; i < o->command_count; ++i) {
        const BoatPluginCommand *c = &o->commands[i];
        if (!text(c->kind, sizeof(c->kind), false) || !text(c->id, sizeof(c->id), false)
            || !text(c->label, sizeof(c->label), true) || c->generation != o->generation)
            return false;
        bool timer = !strcmp(c->kind, "timer.set") || !strcmp(c->kind, "timer.cancel");
        bool set = !strcmp(c->kind, "hud.set");
        if ((!timer && !set && strcmp(c->kind, "hud.remove")) || (timer && !o->timers_enabled)
            || (set && !c->label[0]) || (!set && c->label[0])
            || (!strcmp(c->kind, "timer.set") && c->value <= o->tick)
            || ((!strcmp(c->kind, "timer.cancel") || !strcmp(c->kind, "hud.remove")) && c->value))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(c->id, o->commands[j].id)
                && timer == !strncmp(o->commands[j].kind, "timer.", 6))
                return false;
    }
    return true;
}

static void objective_wire(Wire *w, BoatPluginObjective *o)
{
    o->state.size = (unsigned)integer(w, o->state.size, 4);
    if (o->state.size > BOAT_PLUGIN_STATE_BYTES) {
        w->ok = false;
        return;
    }
    bytes(w, o->state.bytes, o->state.size);
    o->rng = (uint32_t)integer(w, o->rng, 4);
    o->acknowledged = signed_integer(w, o->acknowledged);
    o->tick = signed_integer(w, o->tick);
    o->state_version = (unsigned)integer(w, o->state_version, 4);
    o->generation = (unsigned)integer(w, o->generation, 4);
    o->initialized = boolean(w, o->initialized);
    o->quarantined = boolean(w, o->quarantined);
    o->timers_enabled = boolean(w, o->timers_enabled);
    o->hud_count = (unsigned)integer(w, o->hud_count, 4);
    if (o->hud_count > BOAT_PLUGIN_HUD) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < o->hud_count; ++i) {
        string(w, o->hud[i].id, sizeof(o->hud[i].id));
        string(w, o->hud[i].label, sizeof(o->hud[i].label));
        o->hud[i].value = signed_integer(w, o->hud[i].value);
    }
    o->timer_count = (unsigned)integer(w, o->timer_count, 4);
    if (o->timer_count > BOAT_PLUGIN_TIMERS) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < o->timer_count; ++i) {
        string(w, o->timers[i].id, sizeof(o->timers[i].id));
        o->timers[i].tick = signed_integer(w, o->timers[i].tick);
    }
    o->command_count = (unsigned)integer(w, o->command_count, 4);
    if (o->command_count > BOAT_PLUGIN_COMMANDS) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < o->command_count; ++i) {
        BoatPluginCommand *c = &o->commands[i];
        string(w, c->kind, sizeof(c->kind));
        string(w, c->id, sizeof(c->id));
        string(w, c->label, sizeof(c->label));
        c->value = signed_integer(w, c->value);
        c->generation = (unsigned)integer(w, c->generation, 4);
    }
}

static bool request_valid(const BoatPluginEvalRequest *r)
{
    if (!r->serial || !r->source_size || r->source_size > BOAT_PLUGIN_SOURCE_MAX
        || r->transition < BOAT_PLUGIN_OBJECTIVE_INIT || r->transition > BOAT_PLUGIN_OBJECTIVE_RESET
        || r->tick < 0 || r->event_count > BOAT_PLUGIN_EVENTS || !checkpoint(&r->objective))
        return false;
    if (r->transition != BOAT_PLUGIN_OBJECTIVE_UPDATE && r->event_count)
        return false;
    int64_t sequence = r->objective.acknowledged, tick = 0;
    for (unsigned i = 0; i < r->event_count; ++i) {
        const BoatPluginEvent *e = &r->events[i];
        if (e->tick < tick || e->tick > r->tick || e->sequence <= sequence
            || !text(e->kind, sizeof(e->kind), false) || !text(e->id, sizeof(e->id), true))
            return false;
        sequence = e->sequence;
        tick = e->tick;
    }
    return true;
}

static void pins(Wire *w, BoatPluginEvalRequest *r)
{
    r->serial = integer(w, r->serial, 8);
    r->epoch = integer(w, r->epoch, 8);
    r->revision = (unsigned)integer(w, r->revision, 4);
    r->candidate = (unsigned)integer(w, r->candidate, 4);
    r->transition = (BoatPluginTransition)integer(w, (unsigned)r->transition, 4);
    r->tick = signed_integer(w, r->tick);
}

static void request_wire(Wire *w, BoatPluginEvalRequest *r)
{
    if (integer(w, 0x50455131, 4) != 0x50455131)
        w->ok = false;
    pins(w, r);
    r->version = (unsigned)integer(w, r->version, 4);
    r->generation = (unsigned)integer(w, r->generation, 4);
    r->timers = boolean(w, r->timers);
    objective_wire(w, &r->objective);
    r->source_size = (size_t)integer(w, r->source_size, 4);
    if (r->source_size > BOAT_PLUGIN_SOURCE_MAX) {
        w->ok = false;
        return;
    }
    bytes(w, r->source, r->source_size);
    r->event_count = (unsigned)integer(w, r->event_count, 4);
    if (r->event_count > BOAT_PLUGIN_EVENTS) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < r->event_count; ++i) {
        BoatPluginEvent *e = &r->events[i];
        e->tick = signed_integer(w, e->tick);
        e->sequence = signed_integer(w, e->sequence);
        string(w, e->kind, sizeof(e->kind));
        string(w, e->id, sizeof(e->id));
    }
}

size_t boat_plugin_eval_encode_request(
    const BoatPluginEvalRequest *request, unsigned char *output, size_t capacity)
{
    if (!request || !output || !request_valid(request))
        return 0;
    BoatPluginEvalRequest *copy = malloc(sizeof(*copy));
    if (!copy)
        return 0;
    *copy = *request;
    Wire w = { .output = output, .size = capacity, .ok = true };
    request_wire(&w, copy);
    free(copy);
    return w.ok && w.at <= BOAT_PLUGIN_EVAL_REQUEST_MAX ? w.at : 0;
}

bool boat_plugin_eval_decode_request(
    const unsigned char *input, size_t size, BoatPluginEvalRequest *out)
{
    if (!input || !out || !size || size > BOAT_PLUGIN_EVAL_REQUEST_MAX)
        return false;
    BoatPluginEvalRequest *r = calloc(1, sizeof(*r));
    if (!r)
        return false;
    Wire w = { .input = input, .size = size, .ok = true };
    request_wire(&w, r);
    bool ok = w.ok && w.at == size && request_valid(r);
    if (ok)
        *out = *r;
    free(r);
    return ok;
}

static uint64_t digest(const unsigned char *input, size_t size)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ input[i]) * UINT64_C(1099511628211);
    return hash;
}

static void diagnostic_wire(Wire *w, BoatPluginDiagnostic *d)
{
    d->status = (BoatPluginStatus)integer(w, (unsigned)d->status, 4);
    uint64_t line = integer(w, (unsigned)d->line, 4);
    if ((unsigned)d->status > BOAT_PLUGIN_SCHEMA || line > INT_MAX)
        w->ok = false;
    d->line = (int)line;
    string(w, d->file, sizeof(d->file));
    string(w, d->field, sizeof(d->field));
    string(w, d->message, sizeof(d->message));
    d->cost.instructions = integer(w, d->cost.instructions, 8);
    d->cost.entry_instructions = integer(w, d->cost.entry_instructions, 8);
    d->cost.callback_instructions = integer(w, d->cost.callback_instructions, 8);
    uint64_t peak = integer(w, d->cost.peak_bytes, 8);
    uint64_t leaked = integer(w, d->cost.leaked_bytes, 8);
    if (peak > SIZE_MAX || leaked > SIZE_MAX)
        w->ok = false;
    d->cost.peak_bytes = (size_t)peak;
    d->cost.leaked_bytes = (size_t)leaked;
    d->cost.fields = (unsigned)integer(w, d->cost.fields, 4);
}

size_t boat_plugin_eval_execute(
    const unsigned char *input, size_t size, unsigned char *output, size_t capacity)
{
    if (!output)
        return 0;
    BoatPluginEvalRequest *r = calloc(1, sizeof(*r));
    if (!r)
        return 0;
    if (!boat_plugin_eval_decode_request(input, size, r)) {
        free(r);
        return 0;
    }
    BoatPluginDiagnostic d = { 0 };
    if (r->transition == BOAT_PLUGIN_OBJECTIVE_MIGRATE)
        boat_plugin_objective_migrate(&r->objective, r->source, r->source_size, r->version,
            r->generation, r->timers, r->tick, &d);
    else
        boat_plugin_objective_step(&r->objective, r->source, r->source_size, r->transition, r->tick,
            r->events, r->event_count, NULL, &d);
    Wire w = { .output = output, .size = capacity, .ok = true };
    integer(&w, 0x50455231, 4);
    pins(&w, r);
    integer(&w, digest(input, size), 8);
    diagnostic_wire(&w, &d);
    if (d.status == BOAT_PLUGIN_OK)
        objective_wire(&w, &r->objective);
    free(r);
    return w.ok && w.at <= BOAT_PLUGIN_EVAL_REPLY_MAX ? w.at : 0;
}

bool boat_plugin_eval_result(const BoatPluginEvalRequest *expected, const unsigned char *reply,
    size_t size, BoatPluginObjective *out, BoatPluginDiagnostic *diagnostic)
{
    if (!expected || !reply || !out || !diagnostic || !size || size > BOAT_PLUGIN_EVAL_REPLY_MAX)
        return false;
    unsigned char *encoded = malloc(BOAT_PLUGIN_EVAL_REQUEST_MAX);
    BoatPluginEvalRequest *r = calloc(1, sizeof(*r));
    if (!encoded || !r) {
        free(encoded);
        free(r);
        return false;
    }
    size_t n = boat_plugin_eval_encode_request(expected, encoded, BOAT_PLUGIN_EVAL_REQUEST_MAX);
    if (!n) {
        free(encoded);
        free(r);
        return false;
    }
    uint64_t hash = digest(encoded, n);
    free(encoded);
    Wire w = { .input = reply, .size = size, .ok = true };
    if (integer(&w, 0, 4) != 0x50455231)
        w.ok = false;
    pins(&w, r);
    if (r->serial != expected->serial || r->epoch != expected->epoch
        || r->revision != expected->revision || r->candidate != expected->candidate
        || r->transition != expected->transition || r->tick != expected->tick
        || integer(&w, 0, 8) != hash)
        w.ok = false;
    BoatPluginDiagnostic d = { 0 };
    diagnostic_wire(&w, &d);
    bool migrate = expected->transition == BOAT_PLUGIN_OBJECTIVE_MIGRATE;
    if (d.status == BOAT_PLUGIN_OK) {
        objective_wire(&w, &r->objective);
        const BoatPluginObjective *o = &r->objective;
        int64_t ack = expected->event_count ? expected->events[expected->event_count - 1].sequence
                                            : expected->objective.acknowledged;
        if (!checkpoint(o) || !o->initialized || o->quarantined || o->tick != expected->tick
            || o->acknowledged != ack
            || o->generation != (migrate ? expected->generation : expected->objective.generation)
            || o->state_version != (migrate ? expected->version : expected->objective.state_version)
            || o->timers_enabled
                != (migrate ? expected->timers : expected->objective.timers_enabled))
            w.ok = false;
    }
    bool ok = w.ok && w.at == size;
    if (ok) {
        *diagnostic = d;
        if (d.status == BOAT_PLUGIN_OK)
            *out = r->objective;
    }
    free(r);
    return ok;
}
