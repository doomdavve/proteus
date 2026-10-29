/* Lua plugins P1 (docs/PLUGIN_PLAN.md): a session driven by the reference plugin's voyage matches
 * the compiled one tick for tick; staged revisions are checked against the outer map and
 * classified; a rename keeps all progress, a route change restarts the voyage and an exposure
 * change restarts the session; a failed stage never touches the active revision; Undo reinstalls
 * through the same rules; and save polling, on a virtual clock, stages each settled save once. */
#include "boat_plugin_host.h"
#include "boat_session.h"
#include "boat_steering.h"
#include "boat_test_cases.h"
#include "boat_throttle.h"
#include "raymath.h"
#include "wake_velocity_plugin.h"

#include <math.h>
#include <stddef.h>
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

#define REFERENCE "plugins/langedrag"

static BoatPluginFiles reference_files;
static bool reference_read;
static BoatWorldResources outer_world;
static bool outer_ready;

static const BoatPluginFiles *reference(void)
{
    if (!reference_read) {
        BoatPluginDiagnostic d;
        if (boat_plugin_read(REFERENCE, NULL, &reference_files, &d) != BOAT_PLUGIN_OK) {
            fprintf(stderr, "cannot read " REFERENCE ": %s: %s\n", d.file, d.message);
            exit(1);
        }
        reference_read = true;
    }
    return &reference_files;
}

static const BoatWorldResources *outer(void)
{
    if (!outer_ready) {
        if (!boat_world_resources_init(&outer_world, boat_location(BOAT_LOCATION_OUTER))) {
            fprintf(stderr, "cannot prepare the outer world\n");
            exit(1);
        }
        outer_ready = true;
    }
    return &outer_world;
}

// The reference package with FROM replaced by TO in its entry (or its manifest when MANIFEST).
static BoatPluginFiles edited_files;
static BoatPluginBundle edited(const char *from, const char *to, bool manifest)
{
    edited_files = *reference();
    char *text = manifest ? edited_files.manifest : edited_files.source;
    size_t *size = manifest ? &edited_files.manifest_size : &edited_files.source_size;
    size_t capacity = manifest ? sizeof(edited_files.manifest) : sizeof(edited_files.source);
    char *at = strstr(text, from);
    size_t from_size = strlen(from), to_size = strlen(to);
    if (!at || strstr(at + 1, from) || *size - from_size + to_size > capacity) {
        fprintf(stderr, "cannot edit \"%s\" once\n", from);
        exit(1);
    }
    memmove(at + to_size, at + from_size, *size - (size_t)(at - text) - from_size);
    memcpy(at, to, to_size);
    *size = *size - from_size + to_size;
    return boat_plugin_bundle(&edited_files);
}

static BoatPluginBundle original(void) { return boat_plugin_bundle(reference()); }

static BoatWater calm(void)
{
    return boat_world_test_water(boat_location(BOAT_LOCATION_OUTER), false, false, false);
}

static bool start(BoatPluginHost *host, BoatSession *session)
{
    boat_session_init(session, outer(), calm());
    session->presentation_enabled = false;
    BoatPluginBundle bundle = original();
    if (!boat_plugin_host_start(host, session->world.definition, &outer()->map, &bundle))
        return false;
    boat_session_restart_voyage(session, &boat_plugin_host_active(host)->voyage.route);
    return true;
}

static bool same_boat(const Boat *a, const Boat *b)
{
    return a->position.x == b->position.x && a->position.y == b->position.y
        && a->velocity.x == b->velocity.x && a->velocity.y == b->velocity.y
        && a->heading == b->heading && a->yaw_velocity == b->yaw_velocity && a->height == b->height
        && a->vertical_velocity == b->vertical_velocity && a->pitch == b->pitch
        && a->roll == b->roll && a->throttle == b->throttle && a->wheel == b->wheel
        && a->motion_state == b->motion_state;
}

// Progress, ignoring which route each voyage borrows.
static bool same_progress(BoatVoyage a, BoatVoyage b)
{
    a.route = b.route = NULL;
    return a.shelter == b.shelter && a.exposed == b.exposed && a.arrived == b.arrived
        && a.too_fast == b.too_fast && a.observed == b.observed && a.weather == b.weather
        && a.weather_started == b.weather_started && a.started == b.started
        && a.arrival == b.arrival && a.recoveries == b.recoveries && a.event == b.event
        && a.event_shelter == b.event_shelter && a.event_time == b.event_time;
}

static bool same_storm(BoatStorm a, BoatStorm b)
{
    return a.centre.x == b.centre.x && a.centre.y == b.centre.y && a.radius == b.radius
        && a.height == b.height && a.started == b.started && a.build == b.build;
}

static Vector2 sim(Vector2 east_north) { return (Vector2) { -east_north.x, east_north.y }; }

// The P0 reference is the compiled route, so a session on it must play the compiled outing. Both
// sessions take the same inputs from the fjord turn along the direct line through the exposed
// flank to Långedrag, with a forced recovery on the way; every tick compares the boat, the water
// and the voyage's progress, recovery and arrival.
static int compiled_parity(void)
{
    static const Vector2 path[] = { { -3300, -1550 }, { -3850, -2400 }, { -3850, -3150 } };
    BoatPluginHost host;
    BoatSession lua, compiled;
    CHECK(start(&host, &lua));
    boat_session_init(&compiled, outer(), calm());
    compiled.presentation_enabled = false;
    CHECK(compiled.voyage.route == &boat_voyage_outer && lua.voyage.route != &boat_voyage_outer);
    CHECK(
        boat_plugin_route_digest(lua.voyage.route) == boat_plugin_route_digest(&boat_voyage_outer));
    for (BoatSession *s = &lua; s; s = s == &lua ? &compiled : NULL) {
        s->boat.position = sim(path[0]);
        Vector2 first = Vector2Subtract(sim(path[1]), s->boat.position);
        s->boat.heading = atan2f(first.x, -first.y);
    }
    unsigned waypoint = 1, events = 0, exposed = 0;
    BoatVoyageEvent last = BOAT_VOYAGE_NONE;
    while (!compiled.voyage.arrived && compiled.tick < (uint64_t)(300 / BOAT_TICK_SECONDS)) {
        float distance;
        float bearing = boat_voyage_bearing(
            &compiled.voyage, compiled.boat.position, compiled.boat.heading, &distance);
        if (waypoint < 3) {
            BoatVoyage leg = compiled.voyage;
            BoatVoyageRoute towards = *compiled.voyage.route;
            towards.arrival_centre = sim(path[waypoint]);
            leg.route = &towards;
            float remaining;
            bearing = boat_voyage_bearing(
                &leg, compiled.boat.position, compiled.boat.heading, &remaining);
            if (remaining < 60)
                ++waypoint;
        }
        float target = distance > 250 ? 1.0f : distance > 120 ? .35f : .15f;
        BoatInput input = { .steering_rate
            = boat_steering_rate(compiled.boat.wheel, Clamp(bearing * 1.5f, -1, 1)),
            .throttle_rate = boat_throttle_rate(compiled.boat.throttle, target) };
        boat_session_step(&compiled, input);
        boat_session_step(&lua, input);
        if (compiled.tick == (uint64_t)(20 / BOAT_TICK_SECONDS)) {
            boat_session_recover(&compiled); // To the latest shelter, then on from the turn.
            boat_session_recover(&lua);
            compiled.boat.position = lua.boat.position = sim(path[0]);
            compiled.boat.heading = lua.boat.heading = atan2f(550, 850);
        }
        CHECK(same_boat(&compiled.boat, &lua.boat));
        CHECK(same_progress(compiled.voyage, lua.voyage));
        CHECK(same_storm(compiled.world.water.storm, lua.world.water.storm));
        if (compiled.voyage.event != last) {
            last = compiled.voyage.event;
            ++events;
        }
        exposed += compiled.voyage.exposed;
    }
    CHECK(lua.voyage.arrived && lua.voyage.recoveries == 1 && exposed > 0 && events >= 4);
    CHECK(!strcmp(boat_voyage_destination(lua.voyage.route), "LANGEDRAG"));
    printf("parity ticks=%llu arrival=%.2fs exposed_ticks=%u events=%u\n",
        (unsigned long long)lua.tick, lua.voyage.arrival, exposed, events);
    boat_session_destroy(&lua);
    boat_session_destroy(&compiled);
    return 0;
}

static int expect_change(const char *from, const char *to, bool manifest, BoatPluginChange change)
{
    BoatPluginBundle a = original(), b = edited(from, to, manifest);
    BoatPluginVoyage active, candidate;
    CHECK(boat_plugin_load_voyage(&a, NULL, &active, NULL) == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_load_voyage(&b, NULL, &candidate, NULL) == BOAT_PLUGIN_OK);
    BoatPluginChange got = boat_plugin_classify(&active, &candidate);
    if (got != change) {
        fprintf(stderr, "%s -> %s: %s, expected %s\n", from, to, boat_plugin_change_name(got),
            boat_plugin_change_name(change));
        return 1;
    }
    return 0;
}

static int classification(void)
{
    BoatPluginVoyage a, b;
    BoatPluginBundle bundle = original();
    CHECK(boat_plugin_load_voyage(&bundle, NULL, &a, NULL) == BOAT_PLUGIN_OK);
    boat_plugin_voyage_copy(&b, &a);
    CHECK(boat_plugin_classify(&a, &b) == BOAT_PLUGIN_UNCHANGED);
    int failed = 0;
    // Display names only.
    failed |= expect_change("name = \"BRIDGE\"", "name = \"ALVSBORG\"", false, BOAT_PLUGIN_RENAMED);
    failed |= expect_change(
        "name = \"LANGEDRAG\"", "name = \"OLD HARBOUR\"", false, BOAT_PLUGIN_RENAMED);
    failed
        |= expect_change("\"Langedrag voyage\"", "\"Langedrag trip\"", true, BOAT_PLUGIN_RENAMED);
    // Comments and layout change no value.
    failed |= expect_change(
        "-- Facing out of the basin.", "-- Facing west.", false, BOAT_PLUGIN_UNCHANGED);
    // Route gameplay: positions, radii, order identity, arrival and the exposure threshold.
    failed
        |= expect_change("id = \"bridge\"", "id = \"alvsborg\"", false, BOAT_PLUGIN_RESTART_VOYAGE);
    failed |= expect_change("x = 650", "x = 655", false, BOAT_PLUGIN_RESTART_VOYAGE);
    failed |= expect_change("heading = 1.33", "heading = 1.3", false, BOAT_PLUGIN_RESTART_VOYAGE);
    failed |= expect_change("radius = 150", "radius = 120", false, BOAT_PLUGIN_RESTART_VOYAGE);
    failed |= expect_change("radius = 75", "radius = 90", false, BOAT_PLUGIN_RESTART_VOYAGE);
    failed |= expect_change("max_speed = 6", "max_speed = 4", false, BOAT_PLUGIN_RESTART_VOYAGE);
    failed |= expect_change(
        "exposed_fraction = 0.8", "exposed_fraction = 0.7", false, BOAT_PLUGIN_RESTART_VOYAGE);
    // The water: the exposure itself and when its weather begins.
    failed |= expect_change("height = 1.6", "height = 1.2", false, BOAT_PLUGIN_RESTART_SESSION);
    failed |= expect_change("build = 60", "build = 30", false, BOAT_PLUGIN_RESTART_SESSION);
    failed |= expect_change("radius = 700", "radius = 650", false, BOAT_PLUGIN_RESTART_SESSION);
    failed |= expect_change(
        "weather_range = 2500", "weather_range = 2000", false, BOAT_PLUGIN_RESTART_SESSION);
    // Another plugin.
    failed |= expect_change(
        "\"proteus.langedrag\"", "\"proteus.other\"", true, BOAT_PLUGIN_INCOMPATIBLE);
    CHECK(!failed);
    return 0;
}

static int location_checks(void)
{
    const BoatWorldDefinition *location = boat_location(BOAT_LOCATION_OUTER);
    const BoatMap *map = &outer()->map;
    // The nearest shore north of the bridge shelter, and a harbour edge within the arrival
    // radius of Långedrag's shelter that leaves no room to turn.
    Vector2 ashore = { 650, -850 }, edge = { 0 };
    while (boat_map_water(map, ashore, 8) && ashore.y < 2000)
        ashore.y += 10;
    bool found = false;
    for (int r = 10; r <= 70 && !found; r += 10)
        for (int a = 0; a < 36 && !found; ++a) {
            edge = (Vector2) { 3600 + r * cosf(a * PI / 18), -3370 + r * sinf(a * PI / 18) };
            edge = (Vector2) { roundf(edge.x), roundf(edge.y) };
            found = !boat_map_water(map, edge, 20);
        }
    CHECK(!boat_map_water(map, ashore, 8) && found);
    char shore[48], harbour[48];
    snprintf(shore, sizeof(shore), "x = %.0f, z = %.0f", ashore.x, ashore.y);
    snprintf(harbour, sizeof(harbour), "x = %.0f, z = %.0f", edge.x, edge.y);
    struct {
        const char *from, *to, *field;
        bool manifest;
    } cases[] = {
        { "x = -12, z = 60", "x = -12, z = 80", "voyage.shelters[1]", false },
        { "x = 650, z = -850", shore, "voyage.shelters[2]", false },
        { "x = 3595, z = -3370", harbour, "voyage.arrival", false },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        BoatPluginBundle bundle = edited(cases[i].from, cases[i].to, cases[i].manifest);
        BoatPluginVoyage voyage;
        BoatPluginDiagnostic d;
        if (boat_plugin_load_voyage(&bundle, NULL, &voyage, &d) != BOAT_PLUGIN_OK) {
            fprintf(stderr, "%s: %s %s\n", cases[i].to, d.field, d.message);
            return 1;
        }
        BoatPluginStatus status = boat_plugin_check_location(&voyage, location, &outer()->map, &d);
        printf("location %s: %s %s:%d [%s] %s\n", cases[i].to, boat_plugin_status_name(status),
            d.file, d.line, d.field, d.message);
        CHECK(status != BOAT_PLUGIN_OK && !strcmp(d.field, cases[i].field));
    }
    BoatPluginBundle bundle = original();
    BoatPluginVoyage voyage;
    CHECK(boat_plugin_load_voyage(&bundle, NULL, &voyage, NULL) == BOAT_PLUGIN_OK);
    CHECK(boat_plugin_check_location(&voyage, location, &outer()->map, NULL) == BOAT_PLUGIN_OK);
    return 0;
}

// Drive a few ticks with the boat placed at P and moving at V.
static void observe(BoatSession *session, Vector2 p, Vector2 v)
{
    for (int i = 0; i < 3; ++i) {
        session->boat.position = p;
        session->boat.velocity = v;
        boat_session_step(session, (BoatInput) { 0 });
    }
}

static int rename_keeps_progress(void)
{
    BoatPluginHost host;
    BoatSession session;
    CHECK(start(&host, &session));
    const BoatVoyageRoute *route = session.voyage.route;
    observe(&session, route->shelters[1].position, (Vector2) { 0 });
    observe(&session, route->exposure.centre, (Vector2) { 5, 0 }); // Exposed; weather begins.
    session.voyage.shelter = 2;
    boat_session_recover(&session);
    BoatVoyage before = session.voyage;
    Boat boat = session.boat;
    BoatStorm storm = session.world.water.storm;
    uint64_t tick = session.tick;
    CHECK(before.shelter == 2 && before.weather && before.recoveries == 1);

    BoatPluginBundle bundle = edited("name = \"EAST SHORE\"", "name = \"VINGA LEE\"", false);
    CHECK(boat_plugin_host_stage(&host, &bundle) && host.change == BOAT_PLUGIN_RENAMED);
    CHECK(boat_plugin_host_held(&host)->number == 2 && boat_plugin_host_active(&host)->number == 1);
    const BoatPluginRevision *revision = boat_plugin_host_commit(&host);
    CHECK(revision && revision->number == 2);
    boat_session_rebind_voyage(&session, &revision->voyage.route);
    CHECK(session.voyage.route == &revision->voyage.route
        && session.voyage_route == session.voyage.route);
    CHECK(same_progress(before, session.voyage) && same_boat(&boat, &session.boat));
    CHECK(same_storm(storm, session.world.water.storm) && session.tick == tick);
    CHECK(!strcmp(session.voyage.route->shelters[2].name, "VINGA LEE"));
    // A later restart begins the renamed revision.
    boat_session_restart(&session, calm());
    CHECK(session.voyage.route == &revision->voyage.route && session.voyage.shelter == 0);
    boat_session_destroy(&session);
    return 0;
}

// Restart the voyage with ROUTE_EDIT applied, from a session in STATE.
static int restart_from(const char *label, Vector2 place, Vector2 velocity, unsigned shelter,
    bool arrive, const char *from, const char *to)
{
    BoatPluginHost host;
    BoatSession session;
    CHECK(start(&host, &session));
    const BoatVoyageRoute *old = session.voyage.route;
    observe(&session, old->exposure.centre, (Vector2) { 5, 0 }); // The weather begins.
    if (arrive)
        observe(&session, old->arrival_centre, (Vector2) { 1, 0 });
    session.voyage.shelter = shelter;
    observe(&session, place, velocity);
    BoatVoyage before = session.voyage;
    Boat boat = session.boat;
    BoatStorm storm = session.world.water.storm;
    BoatPluginBundle bundle = edited(from, to, false);
    CHECK(boat_plugin_host_stage(&host, &bundle) && host.change == BOAT_PLUGIN_RESTART_VOYAGE);
    const BoatPluginRevision *revision = boat_plugin_host_commit(&host);
    CHECK(revision);
    boat_session_restart_voyage(&session, &revision->voyage.route);
    const BoatVoyageRoute *route = session.voyage.route;
    double now = boat_session_time(&session);
    // Progress starts again now; the boat, clock and grown weather are untouched.
    CHECK(route == &revision->voyage.route && session.voyage.shelter == 0);
    CHECK(!session.voyage.arrived && !session.voyage.exposed && session.voyage.recoveries == 0);
    CHECK(session.voyage.started == now && session.voyage.event == BOAT_VOYAGE_NONE);
    CHECK(session.voyage.weather && session.voyage.weather_started == before.weather_started);
    CHECK(same_boat(&boat, &session.boat) && same_storm(storm, session.world.water.storm));
    // Observation resumes on the next tick with the ordinary rules.
    session.boat.position = place;
    session.boat.velocity = velocity;
    boat_session_step(&session, (BoatInput) { 0 });
    CHECK(session.voyage.shelter < route->shelter_count);
    bool inside_arrival = Vector2Distance(place, route->arrival_centre) <= route->arrival_radius
        && Vector2Length(session.boat.velocity) <= route->arrival_speed;
    CHECK(session.voyage.arrived == inside_arrival);
    // Recovery uses the new route's shelters.
    boat_session_recover(&session);
    const BoatVoyageShelter *s = &route->shelters[session.voyage.shelter];
    CHECK(session.boat.position.x == s->position.x && session.boat.position.y == s->position.y);
    printf("restart %s: was shelter %u%s%s; now shelter %u%s, %u shelters\n", label, before.shelter,
        before.exposed ? " exposed" : "", before.arrived ? " arrived" : "", session.voyage.shelter,
        session.voyage.arrived ? " arrived" : "", route->shelter_count);
    boat_session_destroy(&session);
    return 0;
}

static int restart_voyage_policy(void)
{
    const BoatVoyageRoute *r = &boat_voyage_outer;
    int failed = 0;
    // Inside the bridge shelter's radius when its radius changes.
    failed |= restart_from("in shelter", r->shelters[1].position, (Vector2) { 0 }, 1, false,
        "radius = 150", "radius = 160");
    // Exposed when the threshold changes.
    failed |= restart_from("exposed", r->exposure.centre, (Vector2) { 4, 0 }, 1, false,
        "exposed_fraction = 0.8", "exposed_fraction = 0.6");
    // After arriving, still in the harbour at a crawl, when the arrival speed changes: the new
    // objective completes on the next tick.
    failed |= restart_from("arrived", r->arrival_centre, (Vector2) { 1, 0 }, 3, true,
        "max_speed = 6", "max_speed = 5");
    // At the last of four shelters when the route loses one: the index cannot survive.
    failed |= restart_from("fewer shelters", r->shelters[2].position, (Vector2) { 0 }, 2, false,
        "{ id = \"east_shore\", name = \"EAST SHORE\", x = 3250, z = -2000,\n"
        "        heading = 0.86, radius = 200 },\n",
        "");
    CHECK(!failed);
    return 0;
}

static int restart_session_policy(void)
{
    BoatPluginHost host;
    BoatSession session;
    CHECK(start(&host, &session));
    observe(&session, boat_voyage_outer.exposure.centre, (Vector2) { 5, 0 });
    CHECK(session.world.water.storm.height == 1.6f);
    BoatPluginBundle bundle = edited("height = 1.6", "height = 0.8", false);
    CHECK(boat_plugin_host_stage(&host, &bundle) && host.change == BOAT_PLUGIN_RESTART_SESSION);
    const BoatPluginRevision *revision = boat_plugin_host_commit(&host);
    CHECK(revision);
    // The application restarts the world with the new revision (wake_velocity's full restart).
    session.voyage_route = &revision->voyage.route;
    boat_session_restart(&session, calm());
    CHECK(session.tick == 0 && session.voyage.route == &revision->voyage.route);
    CHECK(session.world.water.storm.radius == 0); // Calm again until the weather begins.
    observe(&session, revision->voyage.route.exposure.centre, (Vector2) { 5, 0 });
    CHECK(session.world.water.storm.height == 0.8f);
    boat_session_destroy(&session);
    return 0;
}

static int failed_stage_keeps_active(void)
{
    BoatPluginHost host;
    BoatSession session;
    CHECK(start(&host, &session));
    const BoatPluginRevision *active = boat_plugin_host_active(&host);
    BoatPluginVoyage copy;
    boat_plugin_voyage_copy(&copy, &active->voyage);
    // A held restart is dropped by a later broken save.
    BoatPluginBundle held = edited("radius = 150", "radius = 120", false);
    CHECK(boat_plugin_host_stage(&host, &held) && boat_plugin_host_held(&host));
    struct {
        const char *from, *to;
        BoatPluginStatus status;
        int line;
    } broken[] = {
        { "return {", "return {{", BOAT_PLUGIN_SYNTAX, 0 },
        { "voyage = {", "voyage = error('half-saved') or {", BOAT_PLUGIN_RUNTIME, 5 },
        { "radius = 75", "radius = 0", BOAT_PLUGIN_SCHEMA, 0 },
        { "x = 650, z = -850", "x = 650, z = -550", BOAT_PLUGIN_SCHEMA, 0 }, // Ashore.
        { "\"outer\"", "\"river\"", BOAT_PLUGIN_MANIFEST, 0 },
    };
    for (size_t i = 0; i < sizeof(broken) / sizeof(broken[0]); ++i) {
        BoatPluginBundle bundle = edited(broken[i].from, broken[i].to, i == 4);
        CHECK(!boat_plugin_host_stage(&host, &bundle));
        const BoatPluginDiagnostic *d = &host.diagnostic;
        printf("rejected: %s %s:%d [%s] %s\n", boat_plugin_status_name(d->status), d->file, d->line,
            d->field, d->message);
        CHECK(d->status == broken[i].status && d->cost.leaked_bytes == 0);
        CHECK(!broken[i].line || d->line == broken[i].line);
        CHECK(!boat_plugin_host_held(&host) && !boat_plugin_host_commit(&host));
        CHECK(boat_plugin_host_active(&host) == active && active->number == 1);
        CHECK(!memcmp(&active->voyage, &copy, offsetof(BoatPluginVoyage, route)));
        CHECK(boat_plugin_route_digest(&active->voyage.route) == copy.digest);
        CHECK(session.voyage.route == &active->voyage.route);
    }
    // The running revision still plays.
    observe(&session, active->voyage.route.shelters[1].position, (Vector2) { 0 });
    CHECK(session.voyage.shelter == 1);
    boat_session_destroy(&session);
    return 0;
}

static int undo_history(void)
{
    BoatPluginHost host;
    BoatSession session;
    CHECK(start(&host, &session));
    // r2 renames, r3 changes the route.
    BoatPluginBundle renamed = edited("name = \"BRIDGE\"", "name = \"ALVSBORG\"", false);
    CHECK(boat_plugin_host_stage(&host, &renamed));
    const BoatPluginRevision *r = boat_plugin_host_commit(&host);
    boat_session_rebind_voyage(&session, &r->voyage.route);
    BoatPluginBundle moved = edited("radius = 150", "radius = 120", false);
    CHECK(boat_plugin_host_stage(&host, &moved) && host.change == BOAT_PLUGIN_RESTART_VOYAGE);
    // Staging the same bytes again keeps the held number.
    moved = edited("radius = 150", "radius = 120", false);
    CHECK(boat_plugin_host_stage(&host, &moved) && boat_plugin_host_held(&host)->number == 3);
    r = boat_plugin_host_commit(&host);
    CHECK(r && r->number == 3 && host.depth == 3);
    boat_session_restart_voyage(&session, &r->voyage.route);
    observe(&session, r->voyage.route.shelters[1].position, (Vector2) { 0 });

    // Undo r3 -> r2 restarts the voyage under the same rule; Discard drops a held undo.
    CHECK(boat_plugin_host_undo(&host) && host.change == BOAT_PLUGIN_RESTART_VOYAGE);
    CHECK(boat_plugin_host_held(&host)->number == 2);
    boat_plugin_host_discard(&host);
    CHECK(!boat_plugin_host_commit(&host) && boat_plugin_host_active(&host)->number == 3);
    CHECK(boat_plugin_host_undo(&host));
    r = boat_plugin_host_commit(&host);
    CHECK(r && r->number == 2 && host.depth == 2);
    boat_session_restart_voyage(&session, &r->voyage.route);
    CHECK(session.voyage.route->shelters[1].radius == 150 && session.voyage.shelter == 0);
    // Undo r2 -> r1 is a rename: progress is kept.
    observe(&session, r->voyage.route.shelters[1].position, (Vector2) { 0 });
    CHECK(boat_plugin_host_undo(&host) && host.change == BOAT_PLUGIN_RENAMED);
    r = boat_plugin_host_commit(&host);
    CHECK(r && r->number == 1);
    boat_session_rebind_voyage(&session, &r->voyage.route);
    CHECK(session.voyage.shelter == 1 && !strcmp(session.voyage.route->shelters[1].name, "BRIDGE"));
    CHECK(!boat_plugin_host_undo(&host) && !boat_plugin_host_held(&host));

    // Many revisions: the history keeps BOAT_PLUGIN_UNDO prior ones, and no commit ever writes
    // the slot the session borrows.
    char name[24];
    for (int i = 0; i < 3 * BOAT_PLUGIN_UNDO; ++i) {
        snprintf(name, sizeof(name), "name = \"DOCK %d\"", i);
        BoatPluginBundle bundle = edited("name = \"DOCK\"", name, false);
        const BoatVoyageRoute *borrowed = session.voyage.route;
        uint64_t digest = boat_plugin_route_digest(borrowed);
        CHECK(boat_plugin_host_stage(&host, &bundle) && host.change == BOAT_PLUGIN_RENAMED);
        CHECK(boat_plugin_route_digest(borrowed) == digest);
        r = boat_plugin_host_commit(&host);
        CHECK(r && &r->voyage.route != borrowed);
        boat_session_rebind_voyage(&session, &r->voyage.route);
        CHECK(host.depth <= BOAT_PLUGIN_UNDO + 1);
    }
    unsigned undone = 0;
    while (boat_plugin_host_undo(&host)) {
        r = boat_plugin_host_commit(&host);
        boat_session_rebind_voyage(&session, &r->voyage.route);
        ++undone;
    }
    CHECK(undone == BOAT_PLUGIN_UNDO && session.voyage.shelter == 1);
    snprintf(name, sizeof(name), "DOCK %d", 2 * BOAT_PLUGIN_UNDO - 1);
    CHECK(!strcmp(session.voyage.route->shelters[0].name, name));
    boat_session_destroy(&session);
    return 0;
}

// Save polling on a virtual clock: a poll every BOAT_PLUGIN_POLL, and a save is staged once it
// has stayed unchanged for BOAT_PLUGIN_SETTLE, however many writes it took.
static int save_polling(void)
{
    BoatPluginWatch watch;
    boat_plugin_watch_start(&watch, 1, 10);
    CHECK(!boat_plugin_watch_due(&watch, 10.1) && boat_plugin_watch_due(&watch, 10.25));
    CHECK(!boat_plugin_watch_due(&watch, 10.3) && boat_plugin_watch_due(&watch, 10.5));
    CHECK(!boat_plugin_watch_settled(&watch, 1, 10.5)); // Unchanged since start.
    // An editor writing the manifest, then the entry, then the entry again.
    CHECK(!boat_plugin_watch_settled(&watch, 2, 11.0));
    CHECK(!boat_plugin_watch_settled(&watch, 3, 11.25));
    CHECK(!boat_plugin_watch_settled(&watch, 4, 11.5));
    CHECK(!boat_plugin_watch_settled(&watch, 4, 11.75)); // 0.25 s: not settled yet.
    CHECK(boat_plugin_watch_settled(&watch, 4, 12.0));
    CHECK(
        !boat_plugin_watch_settled(&watch, 4, 12.25) && !boat_plugin_watch_settled(&watch, 4, 20));
    // Saving the staged bytes back after an intermediate edit does not stage them again...
    CHECK(
        !boat_plugin_watch_settled(&watch, 5, 21) && !boat_plugin_watch_settled(&watch, 4, 21.25));
    CHECK(!boat_plugin_watch_settled(&watch, 4, 22));
    // ...and an explicit reload marks what it staged.
    boat_plugin_watch_staged(&watch, 6);
    CHECK(!boat_plugin_watch_settled(&watch, 6, 23));
    // A read failure is keyed by its reason, so a persistent one is reported once.
    BoatPluginFiles files = *reference();
    BoatPluginDiagnostic missing = { .status = BOAT_PLUGIN_IO, .file = "main.lua" };
    snprintf(missing.message, sizeof(missing.message), "cannot be opened");
    uint64_t good = boat_plugin_read_key(BOAT_PLUGIN_OK, &files, NULL);
    uint64_t gone = boat_plugin_read_key(BOAT_PLUGIN_IO, NULL, &missing);
    CHECK(good != gone && good == boat_plugin_read_key(BOAT_PLUGIN_OK, reference(), NULL));
    files.source[10] ^= 1;
    CHECK(boat_plugin_read_key(BOAT_PLUGIN_OK, &files, NULL) != good);
    CHECK(!boat_plugin_watch_settled(&watch, gone, 30)
        && boat_plugin_watch_settled(&watch, gone, 30.5));
    CHECK(!boat_plugin_watch_settled(&watch, gone, 31));
    return 0;
}

// Ninety-six reload cycles of good, renamed and broken bytes: every load's Lua state is released
// and the active revision and its borrower stay consistent; destroying the session afterwards
// leaves nothing borrowing the host.
static int repeated_reloads(void)
{
    BoatPluginHost host;
    BoatSession session;
    CHECK(start(&host, &session));
    for (int i = 0; i < 96; ++i) {
        BoatPluginBundle bundle = i % 3 == 0 ? edited("return {", "return {{", false)
            : i % 3 == 1                     ? edited("name = \"DOCK\"", "name = \"QUAY\"", false)
                                             : original();
        bool staged = boat_plugin_host_stage(&host, &bundle);
        CHECK(staged == (i % 3 != 0) && host.diagnostic.cost.leaked_bytes == 0);
        if (staged) {
            CHECK(host.change == BOAT_PLUGIN_RENAMED);
            const BoatPluginRevision *r = boat_plugin_host_commit(&host);
            CHECK(r);
            boat_session_rebind_voyage(&session, &r->voyage.route);
        }
        CHECK(session.voyage.route == &boat_plugin_host_active(&host)->voyage.route);
        boat_session_step(&session, (BoatInput) { 0 });
    }
    CHECK(!strcmp(session.voyage.route->shelters[0].name, "DOCK"));
    boat_session_destroy(&session);
    return 0;
}

static bool write_text(const char *name, const char *text, size_t size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", BOAT_PLUGIN_SESSION_DIR, name);
    FILE *file = fopen(path, "wb");
    if (!file)
        return false;
    bool ok = fwrite(text, 1, size, file) == size;
    return fclose(file) == 0 && ok;
}

// Save the reference package with one edit, as an author's editor would.
static bool save(const char *from, const char *to)
{
    BoatPluginBundle b = from ? edited(from, to, false) : original();
    return write_text("plugin.json", b.manifest, b.manifest_size)
        && write_text("main.lua", b.source, b.source_size);
}

// wake_velocity's --plugin glue on real files: F5 (RELOAD), F6 (UNDO) and F7 (APPLY) as the
// frame loop passes them, its save polling on a virtual clock, and its panel text.
static int game_controls(void)
{
    static WakePlugin plugin;
    BoatSession session;
    boat_session_init(&session, outer(), calm());
    session.presentation_enabled = false;
    CHECK(save(NULL, NULL));
    CHECK(wake_plugin_open(&plugin, BOAT_PLUGIN_SESSION_DIR, &session, 0));
    const BoatVoyageRoute *r1 = session.voyage.route;
    CHECK(r1 == &boat_plugin_host_active(&plugin.host)->voyage.route && plugin.watching);
    observe(&session, r1->shelters[1].position, (Vector2) { 0 });
    CHECK(session.voyage.shelter == 1);

    // F5 after a rename applies it with progress kept.
    CHECK(save("name = \"BRIDGE\"", "name = \"ALVSBORG\""));
    CHECK(!wake_plugin_frame(&plugin, &session, true, false, false, 1));
    CHECK(
        session.voyage.shelter == 1 && !strcmp(session.voyage.route->shelters[1].name, "ALVSBORG"));
    CHECK(strstr(plugin.status, "R2 ACTIVE") && !plugin.alert);
    // A route change waits for F7, then restarts the voyage.
    CHECK(save("radius = 150", "radius = 120"));
    CHECK(!wake_plugin_frame(&plugin, &session, true, false, false, 2));
    CHECK(session.voyage.shelter == 1 && session.voyage.route->shelters[1].radius == 150);
    CHECK(strstr(plugin.detail, "F7 RESTART VOYAGE") && plugin.alert);
    CHECK(!wake_plugin_frame(&plugin, &session, false, false, true, 3));
    CHECK(session.voyage.shelter == 0 && session.voyage.route->shelters[1].radius == 120);
    CHECK(strstr(plugin.status, "R3 ACTIVE"));
    // An exposure change asks the caller for a session restart.
    CHECK(save("height = 1.6", "height = 1.2"));
    CHECK(!wake_plugin_frame(&plugin, &session, true, false, false, 4));
    CHECK(strstr(plugin.detail, "F7 RESTART SESSION"));
    CHECK(wake_plugin_frame(&plugin, &session, false, false, true, 5));
    CHECK(session.voyage_route->exposure.height == 1.2f
        && session.voyage.route != session.voyage_route);
    boat_session_restart(&session, calm());
    CHECK(session.voyage.route == session.voyage_route);
    // F6 holds the previous revision; F6 again discards it.
    CHECK(!wake_plugin_frame(&plugin, &session, false, true, false, 6));
    CHECK(strstr(plugin.status, "(UNDO)") && strstr(plugin.detail, "F7 RESTART SESSION"));
    CHECK(!wake_plugin_frame(&plugin, &session, false, true, false, 7));
    CHECK(strstr(plugin.status, "DISCARDED") && session.voyage_route->exposure.height == 1.2f);
    // A broken save names its file, line and field and keeps the running revision.
    CHECK(save("max_speed = 6", "max_speed = 'six'"));
    CHECK(!wake_plugin_frame(&plugin, &session, true, false, false, 8));
    CHECK(plugin.alert && strstr(plugin.status, "voyage.arrival.max_speed"));
    CHECK(strstr(plugin.status, "KEEPING R4"));
    // A save is staged by polling once it has settled, with no key. It is the reference with a
    // new start name, so against R4 it also changes the water: held, never applied silently.
    CHECK(save("name = \"DOCK\"", "name = \"QUAY\""));
    CHECK(!wake_plugin_frame(&plugin, &session, false, false, false, 8.3)); // Poll: changed.
    CHECK(!wake_plugin_frame(&plugin, &session, false, false, false, 8.4)); // Not yet due.
    CHECK(strstr(plugin.status, "KEEPING R4"));
    CHECK(!wake_plugin_frame(&plugin, &session, false, false, false, 8.7)); // Due and settled.
    CHECK(strstr(plugin.status, "R5 CHANGES THE EXPOSED WATER"));
    CHECK(session.voyage_route->exposure.height == 1.2f);
    CHECK(!wake_plugin_frame(&plugin, &session, false, false, false, 9.5)); // Not staged again.
    CHECK(wake_plugin_frame(&plugin, &session, false, false, true, 10));
    boat_session_restart(&session, calm());
    CHECK(!strcmp(session.voyage.route->shelters[0].name, "QUAY"));
    CHECK(session.voyage.route->exposure.height == 1.6f);
    // A rename of that save is applied by polling at once.
    CHECK(save("name = \"DOCK\"", "name = \"PIER\""));
    CHECK(!wake_plugin_frame(&plugin, &session, false, false, false, 10.5));
    CHECK(!wake_plugin_frame(&plugin, &session, false, false, false, 11));
    CHECK(!strcmp(session.voyage.route->shelters[0].name, "PIER"));
    CHECK(strstr(plugin.status, "R6 ACTIVE, renamed"));
    boat_session_destroy(&session);
    return 0;
}

static const TestCase cases[] = {
    TEST_CASE(classification),
    TEST_CASE(location_checks),
    TEST_CASE(rename_keeps_progress),
    TEST_CASE(restart_voyage_policy),
    TEST_CASE(restart_session_policy),
    TEST_CASE(failed_stage_keeps_active),
    TEST_CASE(undo_history),
    TEST_CASE(save_polling),
    TEST_CASE(repeated_reloads),
    TEST_CASE(game_controls),
    SLOW_CASE(compiled_parity),
};

int main(int argc, char **argv)
{
    int result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    if (outer_ready)
        boat_world_resources_destroy(&outer_world);
    return result;
}
