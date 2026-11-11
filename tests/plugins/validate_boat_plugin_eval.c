#include "boat_plugin_eval.h"

#include <limits.h>
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

static const char SCRIPT[]
    = "local hidden=0; return {"
      "init=function(c) return {n=0},{} end,"
      "update=function(c,s) hidden=hidden+1; assert(hidden==1); "
      "s.n=s.n+1; s.draw=c.random(); s.tick=c.tick; return s,"
      "{{kind='hud.set',id='n',label='COUNT',value=s.n,generation=c.generation}} end,"
      "migrate=function(c,s) s.version=c.state_version; return s,{} end}";

static BoatPluginEvalRequest request, decoded;
static unsigned char input[BOAT_PLUGIN_EVAL_REQUEST_MAX];
static unsigned char reply[BOAT_PLUGIN_EVAL_REPLY_MAX + 1];

static void setup(void)
{
    memset(&request, 0, sizeof(request));
    request.serial = 1;
    request.epoch = UINT64_MAX;
    request.revision = 7;
    request.candidate = 8;
    request.objective = (BoatPluginObjective) { .rng = 12345, .generation = 3, .state_version = 1 };
    memcpy(request.source, SCRIPT, sizeof(SCRIPT) - 1);
    request.source_size = sizeof(SCRIPT) - 1;
}

static int run(BoatPluginObjective *out, BoatPluginDiagnostic *d)
{
    size_t n = boat_plugin_eval_encode_request(&request, input, sizeof(input));
    CHECK(n);
    CHECK(boat_plugin_eval_decode_request(input, n, &decoded));
    CHECK(decoded.epoch == request.epoch && decoded.source_size == request.source_size);
    size_t m = boat_plugin_eval_execute(input, n, reply, BOAT_PLUGIN_EVAL_REPLY_MAX);
    CHECK(m);
    CHECK(boat_plugin_eval_result(&request, reply, m, out, d));
    return 0;
}

static int parity(void)
{
    setup();
    BoatPluginObjective actual = { 0 }, native = request.objective;
    BoatPluginDiagnostic d, local;
    CHECK(!run(&actual, &d) && d.status == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_objective_step(
              &native, SCRIPT, strlen(SCRIPT), BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0, NULL, &local)
        == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_objective_digest(&actual) == boat_plugin_objective_digest(&native));
    request.objective = actual;
    request.transition = BOAT_PLUGIN_OBJECTIVE_UPDATE;
    request.tick = INT64_C(9007199254741008); // Above JS's exact integer range, divisible by 12.
    request.event_count = 1;
    request.events[0] = (BoatPluginEvent) { .tick = request.tick - 1,
        .sequence = INT64_C(9007199254740993),
        .kind = "voyage.shelter_reached",
        .id = "shelter" };
    CHECK(!run(&actual, &d) && d.status == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_objective_step(&native, SCRIPT, strlen(SCRIPT), BOAT_PLUGIN_OBJECTIVE_UPDATE,
              request.tick, request.events, 1, NULL, &local)
        == BOAT_PLUGIN_OK);
    CHECK(actual.tick == request.tick && actual.acknowledged == request.events[0].sequence);
    CHECK(boat_plugin_objective_digest(&actual) == boat_plugin_objective_digest(&native));
    CHECK(actual.command_count == 1 && actual.hud_count == 1);
    request.objective = actual;
    request.transition = BOAT_PLUGIN_OBJECTIVE_MIGRATE;
    request.event_count = 0;
    request.version = 2;
    request.generation = 4;
    request.timers = true;
    CHECK(!run(&actual, &d) && d.status == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_objective_migrate(
              &native, SCRIPT, strlen(SCRIPT), 2, 4, true, request.tick, &local)
        == BOAT_PLUGIN_OK);
    CHECK(actual.generation == 4 && actual.state_version == 2 && actual.timers_enabled);
    CHECK(boat_plugin_objective_digest(&actual) == boat_plugin_objective_digest(&native));
    CHECK(!d.cost.leaked_bytes);
    return 0;
}

static int framing(void)
{
    setup();
    size_t n = boat_plugin_eval_encode_request(&request, input, sizeof(input));
    CHECK(n);
    size_t m = boat_plugin_eval_execute(input, n, reply, BOAT_PLUGIN_EVAL_REPLY_MAX);
    CHECK(m);
    BoatPluginObjective o = request.objective, kept = o;
    BoatPluginDiagnostic d = { 0 };
    for (size_t i = 0; i < m; ++i)
        CHECK(!boat_plugin_eval_result(&request, reply, i, &o, &d));
    CHECK(!memcmp(&o, &kept, sizeof(o)));
    reply[m] = 0;
    CHECK(!boat_plugin_eval_result(&request, reply, m + 1, &o, &d));
    ++request.serial;
    CHECK(!boat_plugin_eval_result(&request, reply, m, &o, &d));
    --request.serial;
    --request.epoch;
    CHECK(!boat_plugin_eval_result(&request, reply, m, &o, &d));
    ++request.epoch;
    request.source[0] = ' ';
    CHECK(!boat_plugin_eval_result(&request, reply, m, &o, &d));
    request.source[0] = SCRIPT[0];
    request.objective.rng++;
    CHECK(!boat_plugin_eval_result(&request, reply, m, &o, &d));
    request.objective.rng--;
    CHECK(boat_plugin_eval_result(&request, reply, m, &o, &d));
    request.event_count = UINT_MAX;
    CHECK(!boat_plugin_eval_result(&request, reply, m, &o, &d));
    request.event_count = 0;
    for (size_t i = 0; i < n; i += 11)
        CHECK(!boat_plugin_eval_decode_request(input, i, &decoded));
    input[n] = 0;
    CHECK(!boat_plugin_eval_decode_request(input, n + 1, &decoded));
    input[0] ^= 1;
    CHECK(!boat_plugin_eval_decode_request(input, n, &decoded));
    CHECK(!boat_plugin_eval_execute(input, n, reply, sizeof(reply)));
    input[0] ^= 1;
    CHECK(!boat_plugin_eval_execute(input, n, reply, 1));
    CHECK(!boat_plugin_eval_encode_request(&request, input, 1));
    return 0;
}

static int maximum_state(void)
{
    setup();
    request.transition = BOAT_PLUGIN_OBJECTIVE_UPDATE;
    request.tick = 12;
    request.objective.initialized = true;
    BoatPluginState *s = &request.objective.state;
    s->bytes[s->size++] = 'a';
    s->bytes[s->size++] = 16;
    s->bytes[s->size++] = 0;
    for (unsigned i = 0; i < 16; ++i) {
        unsigned n = i == 15 ? 973 : 1024;
        s->bytes[s->size++] = 's';
        s->bytes[s->size++] = (unsigned char)n;
        s->bytes[s->size++] = (unsigned char)(n >> 8);
        memset(s->bytes + s->size, 'x', n);
        s->size += n;
    }
    CHECK(s->size == BOAT_PLUGIN_STATE_BYTES);
    const char *script = "return { update=function(c,s) return s,{} end }";
    request.source_size = strlen(script);
    memcpy(request.source, script, request.source_size);
    BoatPluginObjective out;
    BoatPluginDiagnostic d;
    CHECK(!run(&out, &d) && d.status == BOAT_PLUGIN_OK);
    CHECK(out.state.size == BOAT_PLUGIN_STATE_BYTES);
    CHECK(!memcmp(out.state.bytes, s->bytes, s->size));
    s->bytes[0] = '?';
    CHECK(!boat_plugin_eval_encode_request(&request, input, sizeof(input)));
    s->bytes[0] = 'a';
    --s->size;
    CHECK(!boat_plugin_eval_encode_request(&request, input, sizeof(input)));
    return 0;
}

static int failure(void)
{
    setup();
    const char *script = "return {init=function(c) error('expected failure') end}";
    request.source_size = strlen(script);
    memcpy(request.source, script, request.source_size);
    BoatPluginObjective out = request.objective, kept = out;
    BoatPluginDiagnostic d;
    CHECK(!run(&out, &d) && d.status == BOAT_PLUGIN_RUNTIME);
    CHECK(!memcmp(&out, &kept, sizeof(out)));
    CHECK(!d.cost.leaked_bytes);
    return 0;
}

static unsigned read32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

static int protected_output(void)
{
    setup();
    size_t n = boat_plugin_eval_encode_request(&request, input, sizeof(input));
    size_t m = boat_plugin_eval_execute(input, n, reply, BOAT_PLUGIN_EVAL_REPLY_MAX);
    CHECK(m);
    BoatPluginObjective out = request.objective, kept = out;
    BoatPluginDiagnostic d = { 0 };
    // Skip PER1's fixed pins/hash and diagnostic's bounded strings/costs.
    size_t at = 56;
    for (unsigned i = 0; i < 3; ++i)
        at += 4 + read32(reply + at);
    at += 44;
    CHECK(at + 4 < m);
    unsigned state_size = read32(reply + at);
    size_t state = at + 4, scalar = state + state_size;
    CHECK(scalar + 35 < m);
    // Invalid state tag, ack, tick, schema, generation, initialized/quarantine/grant.
    const size_t fields[] = { state, scalar + 11, scalar + 19, scalar + 23, scalar + 27,
        scalar + 28, scalar + 29, scalar + 30 };
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        size_t pos = fields[i];
        unsigned char saved = reply[pos];
        reply[pos] ^= 0x80;
        CHECK(!boat_plugin_eval_result(&request, reply, m, &out, &d));
        CHECK(!memcmp(&out, &kept, sizeof(out)));
        reply[pos] = saved;
    }
    CHECK(boat_plugin_eval_result(&request, reply, m, &out, &d));
    return 0;
}

static int capacities(void)
{
    setup();
    request.transition = BOAT_PLUGIN_OBJECTIVE_UPDATE;
    request.tick = 120;
    request.objective.initialized = true;
    request.objective.state = (BoatPluginState) { .size = 1, .bytes = { 't' } };
    request.objective.timers_enabled = true;
    request.objective.hud_count = BOAT_PLUGIN_HUD;
    for (unsigned i = 0; i < BOAT_PLUGIN_HUD; ++i) {
        snprintf(request.objective.hud[i].id, BOAT_PLUGIN_TEXT, "hud%u", i);
        snprintf(request.objective.hud[i].label, BOAT_PLUGIN_TEXT, "VALUE");
        request.objective.hud[i].value = i ? INT64_MAX : INT64_MIN;
    }
    request.objective.timer_count = BOAT_PLUGIN_TIMERS;
    for (unsigned i = 0; i < BOAT_PLUGIN_TIMERS; ++i) {
        snprintf(request.objective.timers[i].id, BOAT_PLUGIN_TEXT, "timer%u", i);
        request.objective.timers[i].tick = INT64_MAX - BOAT_PLUGIN_TIMERS + i;
    }
    request.objective.command_count = BOAT_PLUGIN_COMMANDS;
    for (unsigned i = 0; i < BOAT_PLUGIN_COMMANDS; ++i) {
        BoatPluginCommand *c = &request.objective.commands[i];
        snprintf(c->kind, sizeof(c->kind), "hud.remove");
        snprintf(c->id, sizeof(c->id), "remove%u", i);
        c->generation = request.objective.generation;
    }
    request.event_count = BOAT_PLUGIN_EVENTS;
    for (unsigned i = 0; i < BOAT_PLUGIN_EVENTS; ++i) {
        request.events[i] = (BoatPluginEvent) {
            .tick = i + 1, .sequence = i + 1, .kind = "voyage.sheltered", .id = "shelter"
        };
    }
    const char *script = "return {update=function(c,s) return s,{} end}";
    memset(request.source, ' ', sizeof(request.source));
    memcpy(request.source, script, strlen(script));
    request.source_size = sizeof(request.source);
    BoatPluginObjective out;
    BoatPluginDiagnostic d;
    CHECK(!run(&out, &d) && d.status == BOAT_PLUGIN_OK);
    CHECK(out.hud_count == BOAT_PLUGIN_HUD && out.timer_count == BOAT_PLUGIN_TIMERS);
    CHECK(out.hud[0].value == INT64_MIN && out.hud[1].value == INT64_MAX);
    CHECK(out.acknowledged == BOAT_PLUGIN_EVENTS);
    CHECK(!out.command_count);
    ++request.objective.hud_count;
    CHECK(!boat_plugin_eval_encode_request(&request, input, sizeof(input)));
    --request.objective.hud_count;
    request.objective.commands[0].generation++;
    CHECK(!boat_plugin_eval_encode_request(&request, input, sizeof(input)));
    return 0;
}

int main(void)
{
    if (parity() || framing() || maximum_state() || failure() || protected_output() || capacities())
        return 1;
    puts("boat_plugin_eval=ok");
    return 0;
}
