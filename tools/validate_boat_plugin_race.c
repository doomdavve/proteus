/* P2 descriptor, lifecycle and physical parity checks. Time is always virtual. */
#include "boat_fleet_bots.h"
#include "boat_plugin_host.h"
#include "boat_race_plugin.h"
#include "boat_test_cases.h"
#include "boat_test_world.h"
#include "raymath.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static BoatPluginFiles files, edit_files;
static BoatPluginRace reference_race;
static const BoatWorldResources *river(void) { return test_world(BOAT_LOCATION_RIVER); }

static BoatPluginBundle original(void) { return boat_plugin_bundle(&files); }

static BoatPluginBundle edit(const char *from, const char *to, bool manifest)
{
    edit_files = files;
    char *text = manifest ? edit_files.manifest : edit_files.source;
    size_t *size = manifest ? &edit_files.manifest_size : &edit_files.source_size;
    char *at = strstr(text, from);
    if (!at || strstr(at + strlen(from), from)) {
        fprintf(stderr, "ambiguous fixture edit: %s\n", from);
        exit(1);
    }
    size_t a = strlen(from), b = strlen(to);
    memmove(at + b, at + a, *size - (size_t)(at - text) - a);
    memcpy(at, to, b);
    *size = *size - a + b;
    text[*size] = 0;
    return boat_plugin_bundle(&edit_files);
}

static int reference_matches_compiled(void)
{
    BoatRaceRules rules = boat_race_default_rules();
    uint64_t compiled = boat_plugin_race_digest(boat_race_river_course(), &rules);
    CHECK(reference_race.digest == compiled);
    BoatPluginDiagnostic d = { 0 };
    CHECK(boat_plugin_race_check_location(&reference_race, river()->definition, &river()->map, &d)
        == BOAT_PLUGIN_OK);
    printf("race digest=%016llx compiled=%016llx\n", (unsigned long long)reference_race.digest,
        (unsigned long long)compiled);
    // The descriptor is pointer-free and survives source/copy lifetime changes.
    BoatPluginRace copied = reference_race;
    CHECK(boat_plugin_race_digest(&copied.course, &copied.rules) == compiled);
    return 0;
}

static int rejects(void)
{
    struct {
        const char *from, *to, *field;
        bool manifest;
    } bad[] = {
        { "half_width = 40", "half_width = 0", "race.start.half_width", false },
        { "half_width = 40", "half_width = 0/0", "race.start.half_width", false },
        { "dx = .786", "dx = 0", "race.start", false },
        { "x = -72.3", "x = math.huge", "race.gates[1].x", false },
        { "x = -72.3, z = -376.6", "x = -180, z = -250", "race.gates[1]", false },
        { "x = -118.966", "x = 10000", "race.source", false },
        { "x = -193.8, z = -248.1", "x = -202.4, z = -259.1", "race.grid[2]", false },
        { "x = -202.4", "x = 10000", "race.grid[1]", false },
        { "countdown_ticks = 360", "countdown_ticks = .5", "race.rules.countdown_ticks", false },
        { "cap_ticks = 10800", "cap_ticks = 1e100", "race.rules.cap_ticks", false },
        { "min_players = 2", "min_players = 5", "race.rules.min_players", false },
        { "skip_ticks = 60", "skip_ticks = 241", "race.rules.skip_ticks", false },
        { "gates = {", "gates = { extra = {},", "race.gates", false },
        { "gates = {", "gates = { [9] = {},", "race.gates", false },
        { "height = 1.6", "height = -1", "race.storm.height", false },
        { "speed = 12", "speed = 'fast'", "race.source.speed", false },
        { "race.define", "voyage.define", "capabilities[1]", true },
        { "\"river\"", "\"outer\"", "location", true },
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        BoatPluginBundle b = edit(bad[i].from, bad[i].to, bad[i].manifest);
        BoatPluginDiagnostic d = { 0 };
        BoatPluginRace r = reference_race;
        BoatPluginStatus status = boat_plugin_load_race(&b, NULL, &r, &d);
        if (status == BOAT_PLUGIN_OK)
            status = boat_plugin_race_check_location(&r, river()->definition, &river()->map, &d);
        printf("reject %u: %s [%s] %s\n", i, boat_plugin_status_name(status), d.field, d.message);
        CHECK(status != BOAT_PLUGIN_OK && !strcmp(d.field, bad[i].field));
        CHECK(d.cost.leaked_bytes == 0);
    }
    // Shared VM failures leave the supplied last-good definition untouched.
    const char *broken[] = { "return {", "while true do end", "return { race = os.execute('x') }",
        "return { race = { gates = {} } }" };
    for (unsigned i = 0; i < sizeof(broken) / sizeof(*broken); ++i) {
        BoatPluginBundle b = original();
        b.source = broken[i];
        b.source_size = strlen(broken[i]);
        BoatPluginRace r = reference_race;
        BoatPluginDiagnostic d;
        CHECK(boat_plugin_load_race(&b, NULL, &r, &d) != BOAT_PLUGIN_OK);
        CHECK(!memcmp(&r, &reference_race, sizeof(r)) && d.cost.leaked_bytes == 0);
    }
    BoatPluginLimits small = boat_plugin_default_limits;
    small.memory_bytes = 100;
    BoatPluginBundle b = original();
    BoatPluginRace r = reference_race;
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_load_race(&b, &small, &r, &d) == BOAT_PLUGIN_MEMORY);
    CHECK(d.cost.leaked_bytes == 0 && !memcmp(&r, &reference_race, sizeof(r)));
    small = boat_plugin_default_limits;
    small.source_bytes = 1;
    CHECK(boat_plugin_load_race(&b, &small, &r, &d) == BOAT_PLUGIN_LIMIT);
    return 0;
}

static int reload_and_undo(void)
{
    BoatPluginHost host;
    BoatPluginBundle b = original();
    CHECK(boat_plugin_host_start(&host, river()->definition, &river()->map, &b)
        && host.kind == BOAT_PLUGIN_KIND_RACE);
    const BoatPluginRevision *active = boat_plugin_host_active(&host);
    b = edit("River sprint", "Evening sprint", true);
    CHECK(boat_plugin_host_stage(&host, &b) && host.change == BOAT_PLUGIN_RENAMED);
    CHECK(boat_plugin_host_active(&host) == active);
    CHECK(boat_plugin_host_commit(&host)->race.digest == reference_race.digest);
    b = edit("cap_ticks = 10800", "cap_ticks = 12000", false);
    CHECK(boat_plugin_host_stage(&host, &b) && host.change == BOAT_PLUGIN_RESTART_SESSION);
    CHECK(boat_plugin_host_active(&host)->race.rules.cap_ticks == 10800);
    CHECK(boat_plugin_host_commit(&host)->race.rules.cap_ticks == 12000);
    CHECK(boat_plugin_host_undo(&host) && host.change == BOAT_PLUGIN_RESTART_SESSION);
    CHECK(boat_plugin_host_commit(&host)->race.digest == reference_race.digest);
    CHECK(boat_plugin_host_undo(&host) && host.change == BOAT_PLUGIN_RENAMED);
    CHECK(boat_plugin_host_commit(&host)->number == 1);
    for (unsigned i = 0; i < 40; ++i) {
        active = boat_plugin_host_active(&host);
        b = edit("half_width = 40", "half_width = 0", false);
        CHECK(!boat_plugin_host_stage(&host, &b));
        CHECK(boat_plugin_host_active(&host) == active);
        b = i % 2 ? original() : edit("River sprint", "Evening sprint", true);
        CHECK(boat_plugin_host_stage(&host, &b));
        CHECK(boat_plugin_host_commit(&host));
    }
    b = edit("cap_ticks = 10800", "cap_ticks = 12000", false);
    CHECK(boat_plugin_host_stage(&host, &b));
    ++host.base; // Stale candidate can never replace a newer active revision.
    CHECK(!boat_plugin_host_commit(&host));
    return 0;
}

static bool save_bundle(BoatPluginBundle b)
{
    const char *names[] = { "plugin.json", "main.lua" };
    const char *bytes[] = { b.manifest, b.source };
    size_t sizes[] = { b.manifest_size, b.source_size };
    for (unsigned i = 0; i < 2; ++i) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", BOAT_RACE_PLUGIN_TEST_DIR, names[i]);
        FILE *file = fopen(path, "wb");
        if (!file)
            return false;
        bool ok = fwrite(bytes[i], 1, sizes[i], file) == sizes[i];
        if (fclose(file) != 0 || !ok)
            return false;
    }
    return true;
}

static int trial_controls(void)
{
    static RacePlugin plugin;
    CHECK(save_bundle(original()));
    CHECK(race_plugin_open(&plugin, BOAT_RACE_PLUGIN_TEST_DIR, river(), 0));
    CHECK(save_bundle(edit("River sprint", "Evening sprint", true)));
    CHECK(!race_plugin_frame(&plugin, true, false, false, 1));
    CHECK(plugin.staged && boat_plugin_host_active(&plugin.host)->number == 2);
    CHECK(strstr(plugin.notice, "Evening sprint"));
    CHECK(save_bundle(edit("cap_ticks = 10800", "cap_ticks = 12000", false)));
    CHECK(!race_plugin_frame(&plugin, true, false, false, 2));
    CHECK(boat_plugin_host_active(&plugin.host)->race.rules.cap_ticks == 10800);
    CHECK(strstr(plugin.notice, "F7 restart run"));
    CHECK(race_plugin_frame(&plugin, false, false, true, 3));
    CHECK(boat_plugin_host_active(&plugin.host)->race.rules.cap_ticks == 12000);
    CHECK(!race_plugin_frame(&plugin, false, true, false, 4));
    CHECK(boat_plugin_host_held(&plugin.host));
    CHECK(race_plugin_frame(&plugin, false, false, true, 5));
    CHECK(boat_plugin_host_active(&plugin.host)->race.rules.cap_ticks == 10800);
    // A broken save keeps the run; a fixed save settles on an injected clock.
    CHECK(save_bundle(edit("speed = 12", "speed = 'fast'", false)));
    CHECK(!race_plugin_frame(&plugin, true, false, false, 6));
    CHECK(strstr(plugin.notice, "race.source.speed"));
    CHECK(save_bundle(edit("River sprint", "New race", true)));
    CHECK(!race_plugin_frame(&plugin, false, false, false, 7));
    CHECK(!plugin.staged);
    CHECK(!race_plugin_frame(&plugin, false, false, false, 7.25));
    CHECK(!plugin.staged);
    CHECK(!race_plugin_frame(&plugin, false, false, false, 7.75));
    CHECK(plugin.staged && strstr(plugin.notice, "New race"));
    CHECK(!race_plugin_frame(&plugin, false, false, false, 8));
    CHECK(!plugin.staged);
    CHECK(save_bundle(edit("height = 1.6", "height = 1.5", false)));
    CHECK(!race_plugin_frame(&plugin, true, false, false, 9));
    CHECK(boat_plugin_host_held(&plugin.host));
    CHECK(!race_plugin_frame(&plugin, false, true, false, 10));
    CHECK(!boat_plugin_host_held(&plugin.host));
    // F7 without an already held candidate must not approve a newly detected save.
    CHECK(save_bundle(edit("cap_ticks = 10800", "cap_ticks = 12000", false)));
    CHECK(!race_plugin_frame(&plugin, false, false, true, 11));
    CHECK(!race_plugin_frame(&plugin, false, false, true, 12));
    CHECK(boat_plugin_host_held(&plugin.host));
    CHECK(boat_plugin_host_active(&plugin.host)->race.rules.cap_ticks == 10800);
    CHECK(race_plugin_frame(&plugin, false, false, true, 13));
    return 0;
}

static BoatFleet fleets[2];
static BoatRace races[2];
static int physical_parity(void)
{
    BoatRaceRules rules = boat_race_default_rules();
    CHECK(boat_race_init(&races[0], &rules, boat_race_river_course()));
    CHECK(boat_race_init(&races[1], &reference_race.rules, &reference_race.course));
    for (unsigned n = 0; n < 2; ++n) {
        CHECK(boat_fleet_init(&fleets[n], river(), (BoatWater) { 0 }, BOAT_OBJECT_LIFE_TICKS,
            BOAT_OBJECT_DEFAULT_HEIGHT));
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            CHECK(boat_race_join(&races[n], &fleets[n], i));
    }
    unsigned finishes = 0;
    for (unsigned tick = 0; tick < 200 * BOAT_TICK_HZ; ++tick) {
        BoatInput inputs[BOAT_FLEET_SLOTS] = { 0 };
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
            if (fleets[0].state.slots[i].active)
                inputs[i] = boat_fleet_bot_race_input(&fleets[0], &races[0].course,
                    races[0].entrants[i].gates, races[0].entrants[i].status == BOAT_RACE_FINISHED,
                    i);
        BoatRaceOutput events[2];
        for (unsigned n = 0; n < 2; ++n) {
            CHECK(boat_fleet_step(&fleets[n], inputs));
            boat_race_observe(&races[n], &fleets[n], &events[n]);
        }
        CHECK(boat_fleet_hash(&fleets[0].state) == boat_fleet_hash(&fleets[1].state));
        const BoatRace *a = &races[0], *b = &races[1];
        CHECK(a->phase == b->phase && a->phase_end == b->phase_end && a->go_tick == b->go_tick
            && a->deadline == b->deadline && a->round == b->round && a->placed == b->placed);
        CHECK(events[0].count == events[1].count);
        for (unsigned i = 0; i < events[0].count; ++i) {
            const BoatRaceEvent *x = &events[0].events[i], *y = &events[1].events[i];
            CHECK(x->tick == y->tick && x->time == y->time && x->kind == y->kind
                && x->slot == y->slot && x->value == y->value);
            finishes += x->kind == BOAT_RACE_FINISH;
        }
        for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
            const BoatRaceEntrant *x = &a->entrants[i], *y = &b->entrants[i];
            CHECK(x->identity == y->identity && x->finish == y->finish
                && x->recoveries == y->recoveries && x->status == y->status && x->place == y->place
                && x->gates == y->gates);
        }
        if (a->phase == BOAT_RACE_RESULTS) {
            CHECK(finishes >= 3);
            printf("four-boat parity: %u ticks, %u finishes, hash=%016llx\n", tick + 1, finishes,
                (unsigned long long)boat_fleet_hash(&fleets[0].state));
            return 0;
        }
    }
    CHECK(false);
    return 1;
}

static const TestCase cases[] = { TEST_CASE(reference_matches_compiled), TEST_CASE(rejects),
    TEST_CASE(reload_and_undo), TEST_CASE(trial_controls), SLOW_CASE(physical_parity) };
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--list"))
        return test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    BoatPluginDiagnostic d;
    if (boat_plugin_read("plugins/river-sprint", NULL, &files, &d) != BOAT_PLUGIN_OK)
        return 1;
    BoatPluginBundle b = original();
    if (boat_plugin_load_race(&b, NULL, &reference_race, &d) != BOAT_PLUGIN_OK) {
        fprintf(stderr, "%s: %s\n", d.field, d.message);
        return 1;
    }
    int result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    test_worlds_close();
    return result;
}
