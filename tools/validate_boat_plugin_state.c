#include "boat_plugin_state.h"
#include "boat_test_cases.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static const char SCRIPT[] = "local hidden = 0\n"
                             "return {\n"
                             " init = function(c) return {visits=0, draws={}}, {} end,\n"
                             " update = function(c,s) hidden=hidden+1; assert(hidden==1);\n"
                             "  for _,e in ipairs(c.events) do\n"
                             "   if e.kind=='voyage.shelter_reached' then s.visits=s.visits+1 end\n"
                             "  end\n"
                             "  s.draws={c.random(),c.tick};\n"
                             "  return s, {{kind='hud.set',generation=c.generation,\n"
                             "   id='visits',label='STOPS',value=s.visits}}\n"
                             " end,\n"
                             " migrate = function(c,s) s.version=2; return s,{} end\n"
                             "}";

static BoatPluginObjective fresh(void)
{
    return (BoatPluginObjective) { .generation = 7, .rng = 12345 };
}

static BoatPluginDiagnostic diagnostic;
static BoatPluginStatus step(BoatPluginObjective *o, const char *source,
    BoatPluginTransition action, int64_t tick, const BoatPluginEvent *events, unsigned count)
{
    BoatPluginStatus result = boat_plugin_objective_step(
        o, source, strlen(source), action, tick, events, count, NULL, &diagnostic);
    if (diagnostic.cost.leaked_bytes) {
        fprintf(stderr, "leaked Lua bytes: %zu\n", diagnostic.cost.leaked_bytes);
        return BOAT_PLUGIN_MEMORY;
    }
    return result;
}

static int deterministic_resume(void)
{
    BoatPluginObjective a = fresh(), b = fresh();
    CHECK(step(&a, SCRIPT, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(step(&b, SCRIPT, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_OK);
    for (int64_t tick = 12; tick <= 1200; tick += 12) {
        BoatPluginEvent event = { .tick = tick - 1,
            .sequence = tick / 12,
            .kind = "voyage.shelter_reached",
            .id = "shelter" };
        CHECK(step(&a, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, tick, &event, 1) == BOAT_PLUGIN_OK);
        CHECK(step(&b, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, tick, &event, 1) == BOAT_PLUGIN_OK);
        CHECK(boat_plugin_objective_digest(&a) == boat_plugin_objective_digest(&b));
        CHECK(a.hud_count == 1 && a.hud[0].value == tick / 12);
        if (tick == 600) { // Caller-owned checkpoint; run forward, then seek/replay in isolation.
            BoatPluginObjective checkpoint = a;
            CHECK(step(&a, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, tick + 12, NULL, 0)
                == BOAT_PLUGIN_OK);
            a = checkpoint;
        }
    }
    printf("objective_hash=%016llx lua_peak=%zu instructions=%llu\n",
        (unsigned long long)boat_plugin_objective_digest(&a), diagnostic.cost.peak_bytes,
        (unsigned long long)diagnostic.cost.instructions);
    return 0;
}

static int atomic_failures(void)
{
    const char *bodies[] = {
        "s.visits=99; c.random(); local "
        "commands={{kind='hud.set',generation=7,id='ok',label='OK',value=1},{kind='bad'}}; "
        "error('after mutations and command construction')",
        "s.visits=99; c.random(); return s, {"
        "{kind='hud.set',generation=7,id='ok',label='OK',value=1},"
        "{kind='invalid'}}",
        "s.visits=99; c.random(); return s, {"
        "{kind='hud.set',generation=7,id='ok',label='OK',value=1},"
        "{kind='hud.set',generation=7,id='ok',label='OK',value=2}}",
        "return s, {{kind='hud.set',generation=6,id='stale',label='STALE',value=1}}",
        "s.visits=99; c.random(); error(123)",
        "s.visits=99; c.random(); error({})",
        "s.visits=99; c.random(); while true do end",
        "local x={}; for i=1,1000000 do x[i]=i end; return x,{}",
    };
    for (unsigned i = 0; i < sizeof(bodies) / sizeof(bodies[0]); ++i) {
        BoatPluginObjective o = fresh();
        CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_OK);
        BoatPluginObjective before = o;
        char source[2048];
        snprintf(source, sizeof(source), "return {update=function(c,s) %s end}", bodies[i]);
        BoatPluginEvent event = { .tick = 10, .sequence = 1, .kind = "visit" };
        CHECK(step(&o, source, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, &event, 1) != BOAT_PLUGIN_OK);
        CHECK(o.quarantined);
        CHECK(boat_plugin_objective_digest(&o) == boat_plugin_objective_digest(&before));
        CHECK(!memcmp(&o.state, &before.state, sizeof(o.state)));
        CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 24, &event, 1) != BOAT_PLUGIN_OK);
        CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_RESET, 24, NULL, 0) == BOAT_PLUGIN_OK);
        CHECK(!o.quarantined && !o.hud_count && o.rng == before.rng);
    }
    return 0;
}

static int state_contract(void)
{
    const char *bad[] = {
        "{x=0.0}",
        "{x=0/0}",
        "{x=math.huge}",
        "{x=function() end}",
        "(function() local s={} s.self=s return s end)()",
        "(function() local s={} return {s,s} end)()",
        "{[1]=1,[3]=3}",
        "{[1]=1,other=true}",
        "{[false]=1}",
        "{[string.char(0)]=1}",
        "{x=string.rep('x',1025)}",
        "(function() local s={} for i=1,17 do s={s} end return s end)()",
        "(function() local s={} for i=1,257 do s[i]=i end return s end)()",
        "(function() local s={} for i=1,20 do s[i]=string.rep('x',1024) end return s end)()",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        BoatPluginObjective o = fresh();
        char source[2048];
        snprintf(source, sizeof(source), "return {init=function() return %s, {} end}", bad[i]);
        CHECK(step(&o, source, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_SCHEMA);
        CHECK(!o.initialized && o.state.size == 0);
    }
    BoatPluginObjective a = fresh(), b = fresh();
    CHECK(step(&a, "return {init=function() return {z=true,a={1,-2,'x',false}},{} end}",
              BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0)
        == BOAT_PLUGIN_OK);
    CHECK(step(&b, "return {init=function() local s={a={1,-2,'x',false}} s.z=true return s,{} end}",
              BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0)
        == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_objective_digest(&a) == boat_plugin_objective_digest(&b));
    // Integer extremes and embedded NUL strings round-trip without loss.
    const char *wide
        = "return {init=function() return {lo=math.mininteger,hi=math.maxinteger,"
          "s='a'..string.char(0)..'b'},{} end, update=function(c,s) "
          "assert(s.lo==math.mininteger and s.hi==math.maxinteger and #s.s==3); return s,{} end}";
    a = fresh();
    CHECK(step(&a, wide, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(step(&a, wide, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, NULL, 0) == BOAT_PLUGIN_OK);
    return 0;
}

static int migration_checkpoint(void)
{
    BoatPluginObjective o = fresh();
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_OK);
    BoatPluginObjective before = o;
    CHECK(step(&o, "return {migrate=function(c,s) s.visits=99; c.random(); error('bad') end}",
              BOAT_PLUGIN_OBJECTIVE_MIGRATE, 0, NULL, 0)
        == BOAT_PLUGIN_RUNTIME);
    CHECK(!memcmp(&o, &before, sizeof(o)));
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_MIGRATE, 0, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_objective_digest(&o) != boat_plugin_objective_digest(&before));
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, NULL, 0) == BOAT_PLUGIN_OK);
    for (unsigned size = 0; size < before.state.size; ++size) {
        o = before;
        o.state.size = size;
        CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, NULL, 0) == BOAT_PLUGIN_SCHEMA);
    }
    o = before;
    o.state.bytes[o.state.size++] = 'f';
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, NULL, 0) == BOAT_PLUGIN_SCHEMA);
    o = before;
    o.state.bytes[0] = 0xff;
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, NULL, 0) == BOAT_PLUGIN_SCHEMA);
    return 0;
}

static int event_and_command_limits(void)
{
    BoatPluginObjective before = fresh(), o;
    CHECK(step(&before, SCRIPT, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_OK);
    BoatPluginEvent events[BOAT_PLUGIN_EVENTS + 1] = { 0 };
    for (unsigned i = 0; i < BOAT_PLUGIN_EVENTS + 1; ++i)
        events[i] = (BoatPluginEvent) { .tick = 1, .sequence = i + 1, .kind = "visit" };
    o = before;
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, events, BOAT_PLUGIN_EVENTS + 1)
        == BOAT_PLUGIN_SCHEMA);
    CHECK(boat_plugin_objective_digest(&o) == boat_plugin_objective_digest(&before));
    o = before;
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, events, BOAT_PLUGIN_EVENTS)
        == BOAT_PLUGIN_OK);
    CHECK(o.acknowledged == BOAT_PLUGIN_EVENTS);
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 24, events, 1) == BOAT_PLUGIN_SCHEMA);
    o = before;
    events[1].sequence = events[0].sequence;
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, events, 2) == BOAT_PLUGIN_SCHEMA);
    o = before;
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 13, NULL, 0) == BOAT_PLUGIN_SCHEMA);
    o = before;
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 0, NULL, 0) == BOAT_PLUGIN_SCHEMA);
    const char *counts[] = { "9", "33" };
    for (unsigned i = 0; i < 2; ++i) {
        o = before;
        char source[1024];
        snprintf(source, sizeof(source),
            "return {update=function(c,s) local commands={} "
            "for i=1,%s do commands[i]={kind='hud.set',generation=7,id=tostring(i),"
            "label='TEST',value=i} end return s,commands end}",
            counts[i]);
        CHECK(step(&o, source, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, NULL, 0) == BOAT_PLUGIN_SCHEMA);
        CHECK(boat_plugin_objective_digest(&o) == boat_plugin_objective_digest(&before));
    }
    return 0;
}

static int budgets_and_wide_time(void)
{
    BoatPluginObjective o = fresh();
    BoatPluginLimits limits = boat_plugin_default_limits;
    limits.memory_bytes = 1;
    CHECK(boat_plugin_objective_step(&o, SCRIPT, strlen(SCRIPT), BOAT_PLUGIN_OBJECTIVE_INIT, 0,
              NULL, 0, &limits, &diagnostic)
        == BOAT_PLUGIN_MEMORY);
    CHECK(!diagnostic.cost.leaked_bytes);
    o = fresh();
    limits = boat_plugin_default_limits;
    limits.instructions = 100;
    const char *loop = "return {init=function() while true do end end}";
    CHECK(boat_plugin_objective_step(
              &o, loop, strlen(loop), BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0, &limits, &diagnostic)
        == BOAT_PLUGIN_BUDGET);
    CHECK(!diagnostic.cost.leaked_bytes);
    o = fresh();
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_OK);
    int64_t tick = INT64_C(9007199254741008); // > 2^53, divisible by 12.
    CHECK(tick % 12 == 0);
    BoatPluginEvent event = { .tick = tick, .sequence = INT64_MAX, .kind = "visit" };
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, tick, &event, 1) == BOAT_PLUGIN_OK);
    CHECK(o.tick == tick && o.acknowledged == INT64_MAX);
    printf("callback_peak=%zu instructions=%llu\n", diagnostic.cost.peak_bytes,
        (unsigned long long)diagnostic.cost.instructions);
    return 0;
}

static int timer_capabilities(void)
{
    const char *caps[] = {
        "\"voyage.define\",\"objective.hud\",\"objective.timers\"",
        "\"voyage.define\",\"objective.timers\"",
        "\"voyage.define\",\"objective.hud\",\"objective.hud\"",
        "\"voyage.define\",\"objective.hud\",\"unknown\"",
        "\"race.define\",\"objective.hud\",\"objective.timers\"",
        "\"voyage.define\",\"objective.hud\",\"objective.timers\",\"objective.timers\"",
    };
    for (unsigned i = 0; i < sizeof(caps) / sizeof(caps[0]); ++i) {
        char json[1024];
        snprintf(json, sizeof(json),
            "{\"id\":\"test.timers\",\"name\":\"Timers\",\"api\":1,\"kind\":\"%s\","
            "\"entry\":\"main.lua\",\"location\":\"%s\",\"capabilities\":[%s],\"state_version\":1}",
            i == 4 ? "race" : "voyage", i == 4 ? "river" : "outer", caps[i]);
        BoatPluginManifest manifest = { 0 };
        BoatPluginStatus status
            = boat_plugin_parse_manifest(json, strlen(json), NULL, &manifest, &diagnostic);
        CHECK(status == (i == 0 ? BOAT_PLUGIN_OK : BOAT_PLUGIN_MANIFEST));
        if (!i)
            CHECK(manifest.objective && manifest.timers);
    }
    return 0;
}

static const char TIMER_SCRIPT[]
    = "return {init=function(c) return {fired=0}, {"
      "{kind='timer.set',generation=c.generation,id='z',tick=c.tick+13},"
      "{kind='timer.set',generation=c.generation,id='a',tick=c.tick+13}} end,"
      "update=function(c,s) "
      "if #c.timers>0 then assert(#c.timers==2 and c.timers[1].id=='a' "
      "and c.timers[2].id=='z' and c.timers[1].tick==13); s.fired=s.fired+2 end;"
      "return s, {{kind='hud.set',generation=c.generation,id='a',label='FIRED',value=s.fired}} end,"
      "migrate=function(c,s) assert(#c.timers==0); return s,{} end}";

static int timers_delivery(void)
{
    BoatPluginObjective a = fresh();
    a.timers_enabled = true;
    CHECK(step(&a, TIMER_SCRIPT, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(a.timer_count == 2 && !strcmp(a.timers[0].id, "a"));
    CHECK(step(&a, TIMER_SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(a.timer_count == 2 && a.hud[0].value == 0);
    BoatPluginObjective b = a;
    // A migration preserves pending timers, even when their deadlines pass at its boundary.
    CHECK(step(&a, TIMER_SCRIPT, BOAT_PLUGIN_OBJECTIVE_MIGRATE, 13, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(a.timer_count == 2);
    BoatPluginEvent e = { .tick = 20, .sequence = 1, .kind = "native" };
    CHECK(step(&a, TIMER_SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 24, &e, 1) == BOAT_PLUGIN_OK);
    CHECK(step(&b, TIMER_SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 24, &e, 1) == BOAT_PLUGIN_OK);
    CHECK(a.timer_count == 0 && a.hud[0].value == 2 && a.acknowledged == 1);
    CHECK(boat_plugin_objective_digest(&a) == boat_plugin_objective_digest(&b));
    CHECK(step(&a, TIMER_SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 36, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(a.hud[0].value == 2); // One shot; native acknowledgment is independent.
    // Exact integer deadlines beyond double precision; dispatch after a skipped boundary.
    a = fresh();
    a.timers_enabled = true;
    int64_t start = INT64_C(9007199254741008);
    CHECK(step(&a, TIMER_SCRIPT, BOAT_PLUGIN_OBJECTIVE_INIT, start, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(a.timers[0].tick == start + 13);
    const char *wide = "return {update=function(c,s) assert(c.timers[1].tick==9007199254741021); "
                       "return s,{} end}";
    CHECK(step(&a, wide, BOAT_PLUGIN_OBJECTIVE_UPDATE, start + 24, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(!a.timer_count);
    return 0;
}

static int timers_atomicity(void)
{
    BoatPluginObjective before = fresh(), o;
    before.timers_enabled = true;
    CHECK(step(&before, TIMER_SCRIPT, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_OK);
    const char *bad[] = {
        "{kind='timer.set',generation=7,id='new',tick=25},{kind='bad'}",
        "{kind='timer.cancel',generation=7,id='z'},{kind='timer.set',generation=7,id='z',tick=25}",
        "{kind='timer.set',generation=6,id='new',tick=25}",
        "{kind='timer.set',generation=7,id='new',tick=24}",
        "{kind='timer.set',generation=7,id='new',tick=25.0}",
        "{kind='timer.set',generation=7,id='new',tick=-1}",
        "{kind='timer.cancel',generation=7,id='z',tick=25}",
        "{kind='timer.set',generation=7,id='new',tick=25,label='bad'}",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        o = before;
        char source[1024];
        snprintf(source, sizeof(source),
            "return {update=function(c,s) s.fired=99; c.random(); return s,{%s} end}", bad[i]);
        CHECK(step(&o, source, BOAT_PLUGIN_OBJECTIVE_UPDATE, 24, NULL, 0) == BOAT_PLUGIN_SCHEMA);
        CHECK(o.quarantined && o.timer_count == 2);
        CHECK(boat_plugin_objective_digest(&o) == boat_plugin_objective_digest(&before));
    }
    o = before;
    CHECK(step(&o, "return {update=function(c,s) assert(#c.timers==2); error('after due') end}",
              BOAT_PLUGIN_OBJECTIVE_UPDATE, 24, NULL, 0)
        == BOAT_PLUGIN_RUNTIME);
    CHECK(o.timer_count == 2);
    CHECK(step(&o, TIMER_SCRIPT, BOAT_PLUGIN_OBJECTIVE_MIGRATE, 24, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(o.timer_count == 2 && !o.quarantined);
    CHECK(step(&o, TIMER_SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 36, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(!o.timer_count && o.hud[0].value == 2);
    // Reset retires every old timer before init constructs replacements.
    o = before;
    CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_RESET, 24, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(!o.timer_count);
    o = fresh();
    CHECK(step(&o, TIMER_SCRIPT, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_SCHEMA);
    CHECK(!o.timer_count && !o.initialized); // No capability grant.
    return 0;
}

static int timers_capacity_and_checkpoint(void)
{
    const char *full
        = "return {init=function(c) local cmds={} for i=1,16 do "
          "cmds[i]={kind='timer.set',generation=c.generation,id=tostring(i),tick=120} end "
          "return {},cmds end}";
    BoatPluginObjective o = fresh();
    o.timers_enabled = true;
    CHECK(step(&o, full, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(o.timer_count == BOAT_PLUGIN_TIMERS);
    BoatPluginObjective before = o;
    CHECK(step(&o,
              "return {update=function(c,s) return s, {"
              "{kind='timer.set',generation=7,id='overflow',tick=120}} end}",
              BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, NULL, 0)
        == BOAT_PLUGIN_SCHEMA);
    CHECK(boat_plugin_objective_digest(&o) == boat_plugin_objective_digest(&before));
    o = before;
    CHECK(step(&o,
              "return {update=function(c,s) return s, {"
              "{kind='timer.cancel',generation=7,id='1'},"
              "{kind='timer.cancel',generation=7,id='absent'},"
              "{kind='timer.set',generation=7,id='new',tick=100},"
              "{kind='timer.set',generation=7,id='2',tick=101}} end}",
              BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, NULL, 0)
        == BOAT_PLUGIN_OK);
    CHECK(o.timer_count == 16 && !strcmp(o.timers[0].id, "new") && o.timers[1].tick == 101);
    // Re-arm a just-delivered timer in the same transaction.
    CHECK(step(&o,
              "return {update=function(c,s) assert(#c.timers==2); return s, {"
              "{kind='timer.set',generation=7,id='new',tick=math.maxinteger}} end}",
              BOAT_PLUGIN_OBJECTIVE_UPDATE, 108, NULL, 0)
        == BOAT_PLUGIN_OK);
    CHECK(o.timer_count == 15 && o.timers[14].tick == INT64_MAX);
    for (unsigned i = 0; i < 6; ++i) {
        o = before;
        if (i == 0)
            o.timer_count = BOAT_PLUGIN_TIMERS + 1;
        if (i == 1)
            o.timers_enabled = false;
        if (i == 2)
            o.timers[0].tick = 0;
        if (i == 3)
            memset(o.timers[0].id, 'a', sizeof(o.timers[0].id));
        if (i == 4)
            o.timers[1] = o.timers[0];
        if (i == 5)
            o.timers[0].tick = 121; // Unsorted checkpoint.
        CHECK(!boat_plugin_objective_digest(&o));
        CHECK(step(&o, SCRIPT, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, NULL, 0) == BOAT_PLUGIN_SCHEMA);
    }
    return 0;
}

static int schema_timer_retirement(void)
{
    BoatPluginObjective o = fresh();
    o.state_version = 1;
    o.timers_enabled = true;
    const char *init = "return {init=function(c) return {n=1},"
                       "{{kind='timer.set',generation=c.generation,id='keep',tick=24},"
                       "{kind='timer.set',generation=c.generation,id='drop',tick=36}} end}";
    CHECK(step(&o, init, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_OK);
    const char *migrate
        = "return {migrate=function(c,s) "
          "assert(c.previous_state_version==1 and c.state_version==2 and #c.timers==0); "
          "return s,{{kind='timer.cancel',generation=c.generation,id='drop'}} end}";
    CHECK(boat_plugin_objective_migrate(&o, migrate, strlen(migrate), 2, 8, true, 12, &diagnostic)
        == BOAT_PLUGIN_OK);
    CHECK(o.timer_count == 1 && !strcmp(o.timers[0].id, "keep") && o.timers[0].tick == 24);
    CHECK(o.generation == 8 && o.state_version == 2 && o.command_count == 1);
    BoatPluginObjective before = o;
    const char *bad = "return {migrate=function(c,s) c.random(); error('reject') end}";
    CHECK(boat_plugin_objective_migrate(&o, bad, strlen(bad), 3, 9, false, 12, &diagnostic)
        == BOAT_PLUGIN_RUNTIME);
    CHECK(!memcmp(&o, &before, sizeof(o)));
    const char *drop = "return {migrate=function(c,s) assert(c.previous_state_version==2 "
                       "and c.state_version==3 and c.timers==nil); return s,{} end}";
    CHECK(boat_plugin_objective_migrate(&o, drop, strlen(drop), 3, 9, false, 12, &diagnostic)
        == BOAT_PLUGIN_OK);
    CHECK(!o.timers_enabled && !o.timer_count && o.generation == 9 && o.state_version == 3);
    before = o;
    CHECK(boat_plugin_objective_migrate(&o, drop, strlen(drop), 0, 10, false, 12, &diagnostic)
        == BOAT_PLUGIN_SCHEMA);
    CHECK(!memcmp(&o, &before, sizeof(o)));
    return 0;
}

static int independent_budgets(void)
{
    // Entry spends more than a callback is allowed, without borrowing its allowance.
    const char *source = "local n=0; for i=1,60000 do n=n+1 end; return {"
                         "init=function(c) return {},{} end,"
                         "update=function(c,s) for i=1,60000 do n=n+1 end; return s,{} end,"
                         "migrate=function(c,s) for i=1,60000 do n=n+1 end; return s,{} end}";
    BoatPluginObjective o = fresh();
    CHECK(step(&o, source, BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(diagnostic.cost.entry_instructions > BOAT_PLUGIN_CALLBACK_INSTRUCTIONS);
    CHECK(diagnostic.cost.callback_instructions < BOAT_PLUGIN_CALLBACK_INSTRUCTIONS);
    BoatPluginObjective before = o;
    CHECK(step(&o, source, BOAT_PLUGIN_OBJECTIVE_UPDATE, 12, NULL, 0) == BOAT_PLUGIN_BUDGET);
    CHECK(diagnostic.cost.callback_instructions
        <= BOAT_PLUGIN_CALLBACK_INSTRUCTIONS + BOAT_PLUGIN_HOOK_STEP);
    CHECK(boat_plugin_objective_digest(&o) == boat_plugin_objective_digest(&before));
    CHECK(o.quarantined);
    o = before;
    CHECK(step(&o, source, BOAT_PLUGIN_OBJECTIVE_MIGRATE, 12, NULL, 0) == BOAT_PLUGIN_OK);
    CHECK(diagnostic.cost.callback_instructions > BOAT_PLUGIN_CALLBACK_INSTRUCTIONS);
    CHECK(diagnostic.cost.instructions
        == diagnostic.cost.entry_instructions + diagnostic.cost.callback_instructions);
    before = o;
    CHECK(step(&o, "return {migrate=function(c,s) while true do end end}",
              BOAT_PLUGIN_OBJECTIVE_MIGRATE, 12, NULL, 0)
        == BOAT_PLUGIN_BUDGET);
    CHECK(!memcmp(&o, &before, sizeof(o)));
    CHECK(diagnostic.cost.callback_instructions
        <= BOAT_PLUGIN_MIGRATION_INSTRUCTIONS + BOAT_PLUGIN_HOOK_STEP);
    CHECK(step(&o, "while true do end", BOAT_PLUGIN_OBJECTIVE_MIGRATE, 12, NULL, 0)
        == BOAT_PLUGIN_BUDGET);
    CHECK(diagnostic.cost.entry_instructions > boat_plugin_default_limits.instructions);
    CHECK(!diagnostic.cost.callback_instructions && !diagnostic.cost.leaked_bytes);
    CHECK(!memcmp(&o, &before, sizeof(o)));
    return 0;
}

static const TestCase cases[] = {
    TEST_CASE(independent_budgets),
    TEST_CASE(schema_timer_retirement),
    TEST_CASE(timer_capabilities),
    TEST_CASE(timers_delivery),
    TEST_CASE(timers_atomicity),
    TEST_CASE(timers_capacity_and_checkpoint),
    TEST_CASE(deterministic_resume),
    TEST_CASE(atomic_failures),
    TEST_CASE(state_contract),
    TEST_CASE(migration_checkpoint),
    TEST_CASE(event_and_command_limits),
    TEST_CASE(budgets_and_wide_time),
};

int main(int argc, char **argv)
{
    return test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
}
