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
#include "waterfront_traffic.h"

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

static BoatWater calm(void);
static const BoatWorldResources *outer(void);

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

// L2.2: the two-berth package installs its layout, holds a physical edit for an explicit session
// restart, restores it through Undo, and rejects an unsafe berth without touching the active
// scene. The berth list is the one the physics/renderers borrow.
static BoatPluginFiles harbor_files;
static bool harbor_read;
static BoatPluginBundle harbor_edit(const char *from, const char *to)
{
    if (!harbor_read) {
        BoatPluginDiagnostic d;
        if (boat_plugin_read("plugins/harbor-life", NULL, &harbor_files, &d) != BOAT_PLUGIN_OK)
            exit(1);
        harbor_read = true;
    }
    static BoatPluginFiles edited;
    edited = harbor_files;
    char *at = strstr(edited.source, from);
    if (!at) {
        fprintf(stderr, "fragment not in harbor-life: %s\n", from);
        exit(1);
    }
    size_t from_size = strlen(from), to_size = strlen(to);
    memmove(at + to_size, at + from_size,
        edited.source_size - (size_t)(at - edited.source) - from_size);
    memcpy(at, to, to_size);
    edited.source_size = edited.source_size - from_size + to_size;
    return boat_plugin_bundle(&edited);
}

static int harbor_life_scene(void)
{
    static WakePlugin p;
    BoatSession s;
    boat_session_init(&s, outer(), calm());
    s.presentation_enabled = false;
    CHECK(wake_plugin_open(&p, "plugins/harbor-life", &s, 0));
    const BoatPluginRevision *r1 = boat_plugin_host_active(&p.host);
    CHECK(r1->voyage.has_scene && r1->voyage.scene.berth_count == 2);
    CHECK(s.world.scene == &r1->voyage.scene && boat_session_view(&s).scene == s.world.scene);
    CHECK(s.world.scene->berths[1].z == 173);

    // A berth edit is a physical scene change: held until F7, then the caller restarts.
    BoatPluginBundle moved = harbor_edit("x = 13, z = 173", "x = 13, z = 179");
    CHECK(boat_plugin_host_stage(&p.host, &moved));
    CHECK(p.host.change == BOAT_PLUGIN_RESTART_SESSION);
    CHECK(s.world.scene == &r1->voyage.scene && r1->voyage.scene.berths[1].z == 173);
    CHECK(wake_plugin_frame(&p, &s, false, false, true, 1));
    CHECK(boat_plugin_host_active(&p.host)->number == 2);
    CHECK(s.world.scene == &boat_plugin_host_active(&p.host)->voyage.scene);
    CHECK(s.world.scene->berths[1].z == 179);
    boat_session_restart(&s, calm());
    CHECK(s.world.scene == &boat_plugin_host_active(&p.host)->voyage.scene);
    CHECK(s.world.scene->berths[1].z == 179);

    // Undo restores the whole previous layout through the same restart policy.
    CHECK(!wake_plugin_frame(&p, &s, false, true, false, 2));
    CHECK(strstr(p.detail, "F7 RESTART SESSION"));
    CHECK(wake_plugin_frame(&p, &s, false, false, true, 3));
    boat_session_restart(&s, calm());
    CHECK(boat_plugin_host_active(&p.host)->number == 1);
    CHECK(s.world.scene == &boat_plugin_host_active(&p.host)->voyage.scene);
    CHECK(s.world.scene->berths[1].z == 173);

    // A dock-module edit follows the same explicit restart and Undo policy.
    BoatPluginBundle moved_module = harbor_edit("x = 20, z = 170", "x = 20, z = 166");
    CHECK(boat_plugin_host_stage(&p.host, &moved_module));
    CHECK(p.host.change == BOAT_PLUGIN_RESTART_SESSION);
    CHECK(wake_plugin_frame(&p, &s, false, false, true, 4));
    boat_session_restart(&s, calm());
    CHECK(s.world.scene->module_count == 2 && s.world.scene->modules[0].z == 166);
    CHECK(!wake_plugin_frame(&p, &s, false, true, false, 5));
    CHECK(wake_plugin_frame(&p, &s, false, false, true, 6));
    boat_session_restart(&s, calm());
    CHECK(boat_plugin_host_active(&p.host)->number == 1 && s.world.scene->modules[0].z == 170);

    // An unsafe berth (ashore) is rejected and leaves R1 and its scene intact.
    BoatPluginBundle bad = harbor_edit("x = 13, z = 173", "x = 32, z = 173");
    CHECK(!boat_plugin_host_stage(&p.host, &bad));
    CHECK(p.host.diagnostic.status == BOAT_PLUGIN_SCHEMA);
    CHECK(!strcmp(p.host.diagnostic.field, "scene.marina.berths[2]"));
    CHECK(boat_plugin_host_active(&p.host)->number == 1
        && s.world.scene == &boat_plugin_host_active(&p.host)->voyage.scene);
    boat_session_destroy(&s);
    return 0;
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

// The reference package plus the L1.2 scene envelope. The manifest and entry are edited in
// separate buffers so a test can write any revision an editor would save.
static bool replace_once(
    char *text, size_t *size, size_t capacity, const char *from, const char *to)
{
    char *at = strstr(text, from);
    size_t from_size = strlen(from), to_size = strlen(to);
    if (!at || strstr(at + 1, from) || *size - from_size + to_size > capacity)
        return false;
    memmove(at + to_size, at + from_size, *size - (size_t)(at - text) - from_size);
    memcpy(at, to, to_size);
    *size = *size - from_size + to_size;
    return true;
}

static bool save_scene(const char *name, unsigned version)
{
    BoatPluginFiles files = *reference();
    char manifest_from[] = "\"capabilities\": [\"voyage.define\"]";
    char manifest_to[] = "\"capabilities\": [\"voyage.define\", \"scene.define\"]";
    char source_to[64];
    snprintf(source_to, sizeof(source_to), "return { scene = { version = %u },", version);
    if (!replace_once(files.manifest, &files.manifest_size, sizeof(files.manifest), manifest_from,
            manifest_to)
        || !replace_once(
            files.source, &files.source_size, sizeof(files.source), "return {", source_to))
        return false;
    if (name
        && !replace_once(
            files.manifest, &files.manifest_size, sizeof(files.manifest), "Langedrag voyage", name))
        return false;
    return write_text("plugin.json", files.manifest, files.manifest_size)
        && write_text("main.lua", files.source, files.source_size);
}

// L1.3: an accepted envelope is installed on the session/world/view at open and at every
// commit, survives Undo, and is removed when the plugin leaves. Compiled defaults remain the
// fallback for another independent session.
static int scene_binding(void)
{
    static WakePlugin p, other;
    BoatSession s, independent;
    boat_session_init(&s, outer(), calm());
    boat_session_init(&independent, outer(), calm());
    s.presentation_enabled = independent.presentation_enabled = false;
    CHECK(save_scene(NULL, 1));
    CHECK(wake_plugin_open(&p, BOAT_PLUGIN_SESSION_DIR, &s, 0));
    const BoatPluginRevision *active = boat_plugin_host_active(&p.host);
    CHECK(active->voyage.has_scene && !boat_scene_has_content(&active->voyage.scene));
    CHECK(s.world.scene == &active->voyage.scene);
    CHECK(boat_session_view(&s).scene == s.world.scene);
    CHECK(strstr(p.status, "R1 ACTIVE"));

    // A manifest-only rename applies immediately and rebinds the scene to the new slot.
    CHECK(save_scene("Langedrag scenes", 1));
    CHECK(!wake_plugin_frame(&p, &s, true, false, false, 1));
    active = boat_plugin_host_active(&p.host);
    CHECK(active->number == 2 && active->voyage.has_scene);
    CHECK(s.world.scene == &active->voyage.scene && boat_session_view(&s).scene == s.world.scene);

    // A broken envelope leaves the running scene and revision intact.
    CHECK(save_scene("Langedrag scenes", 2));
    CHECK(!wake_plugin_frame(&p, &s, true, false, false, 2));
    CHECK(p.alert && strstr(p.status, "scene.version") && strstr(p.status, "KEEPING R2"));
    CHECK(boat_plugin_host_active(&p.host)->number == 2
        && s.world.scene == &boat_plugin_host_active(&p.host)->voyage.scene);

    // Undo reinstalls R1 through the same classification and rebinding path.
    CHECK(!wake_plugin_frame(&p, &s, false, true, false, 3));
    CHECK(boat_plugin_host_active(&p.host)->number == 1);
    CHECK(s.world.scene == &boat_plugin_host_active(&p.host)->voyage.scene);

    // Independent sessions own their own scene storage; leaving one leaves the other intact.
    CHECK(save_scene(NULL, 1));
    CHECK(wake_plugin_open(&other, BOAT_PLUGIN_SESSION_DIR, &independent, 4));
    CHECK(independent.world.scene == &boat_plugin_host_active(&other.host)->voyage.scene);
    CHECK(independent.world.scene != s.world.scene);
    wake_plugin_close(&p, &s);
    CHECK(!s.world.scene && !boat_session_view(&s).scene);
    CHECK(independent.world.scene == &boat_plugin_host_active(&other.host)->voyage.scene);
    wake_plugin_close(&other, &independent);
    CHECK(!independent.world.scene && !boat_session_view(&independent).scene);

    // Re-enter cleanly after a close; the new revision owns a fresh scene binding.
    CHECK(wake_plugin_open(&p, BOAT_PLUGIN_SESSION_DIR, &s, 5));
    CHECK(s.world.scene == &boat_plugin_host_active(&p.host)->voyage.scene);
    wake_plugin_close(&p, &s);
    CHECK(!s.world.scene);
    boat_session_destroy(&independent);
    boat_session_destroy(&s);
    return 0;
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

// Drive native observation at exact virtual ticks; physics placement is independent of this
// adapter test. The production loop uses the identical before/after calls around each tick.
static void observe_objective(WakePlugin *p, BoatSession *s, unsigned tick, Vector2 position)
{
    wake_plugin_before_tick(p);
    boat_session_set_tick(s, tick);
    boat_voyage_step_events(
        &s->voyage, position, (Vector2) { 0 }, boat_session_time(s), &s->voyage_events);
    wake_plugin_after_tick(p, s);
}

static int lossless_events(void)
{
    BoatVoyageRoute route = boat_voyage_outer;
    route.shelters[1].position = route.shelters[2].position = route.arrival_centre;
    route.exposure.centre = route.arrival_centre;
    route.weather_range = 1;
    BoatVoyage v;
    boat_voyage_start(&v, &route, 0);
    v.observed = true;
    BoatVoyageEvents events;
    boat_voyage_step_events(&v, route.arrival_centre, (Vector2) { 0 }, 1, &events);
    CHECK(events.count == 5);
    CHECK(events.items[0].kind == BOAT_VOYAGE_SHELTER && events.items[0].shelter == 1);
    CHECK(events.items[1].kind == BOAT_VOYAGE_SHELTER && events.items[1].shelter == 2);
    CHECK(events.items[2].kind == BOAT_VOYAGE_WEATHER);
    CHECK(events.items[3].kind == BOAT_VOYAGE_EXPOSED);
    CHECK(events.items[4].kind == BOAT_VOYAGE_ARRIVED && v.arrived);
    CHECK(v.event == BOAT_VOYAGE_ARRIVED); // Retain the old latest-event presentation.
    boat_voyage_step_events(&v, (Vector2) { -1000, -1000 }, (Vector2) { 0 }, 2, &events);
    CHECK(events.count == 1 && events.items[0].kind == BOAT_VOYAGE_SHELTERED);
    boat_voyage_step_events(&v, (Vector2) { -1000, -1000 }, (Vector2) { 0 }, 3, &events);
    CHECK(!events.count);
    return 0;
}

static int live_objective(void)
{
    static WakePlugin p;
    BoatSession s;
    boat_session_init(&s, outer(), calm());
    CHECK(wake_plugin_open(&p, "plugins/shelter-tour", &s, 0));
    CHECK(p.objective.initialized && p.objective.hud[0].value == 0);
    const BoatVoyageRoute *r = s.voyage.route;
    observe_objective(&p, &s, 1, r->shelters[1].position);
    observe_objective(&p, &s, 2, r->shelters[2].position);
    observe_objective(&p, &s, 12, r->arrival_centre);
    CHECK(s.voyage.arrived && p.objective_pending);
    CHECK(p.objective.hud[0].value == 0 && p.objective.acknowledged == 0);
    CHECK(p.staged_objective.hud[0].value == 2);
    // A metadata-only rename at T keeps pending state/source ownership until T+1.
    static BoatPluginFiles renamed;
    renamed = p.files;
    char *label = strstr(renamed.manifest, "Shelter tour");
    CHECK(label);
    label[0] = 'T';
    BoatPluginBundle rename_bundle = boat_plugin_bundle(&renamed);
    CHECK(boat_plugin_host_stage(&p.host, &rename_bundle));
    CHECK(p.host.change == BOAT_PLUGIN_RENAMED);
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 1));
    CHECK(p.objective_pending && p.generation == 1);
    // Recovery at T after dispatch must not publish before T+1 or retire its newer notice.
    boat_session_recover(&s);
    wake_plugin_after_tick(&p, &s);
    CHECK(p.objective_pending && p.objective.hud[0].value == 0);
    wake_plugin_before_tick(&p);
    CHECK(!p.objective_pending && p.event_count == 1);
    CHECK(p.objective.acknowledged == p.sequence - 1);
    CHECK(!strcmp(p.objective.hud[0].label, "TOUR COMPLETE"));
    CHECK(!strcmp(p.events[0].kind, "voyage.recovered") && p.events[0].tick == 12);
    observe_objective(&p, &s, 24, r->arrival_centre);
    wake_plugin_before_tick(&p);
    CHECK(p.objective.acknowledged == p.sequence);
    // A new native voyage resets objective state; arriving directly remains successful natively.
    boat_session_restart_voyage(&s, r);
    wake_plugin_reset_objective(&p, &s);
    observe_objective(&p, &s, 36, r->arrival_centre);
    wake_plugin_before_tick(&p);
    CHECK(s.voyage.arrived && !strcmp(p.objective.hud[0].label, "TOUR MISSED"));
    // Repeated shelter entries after arrival cannot retroactively complete the tour.
    observe_objective(&p, &s, 37, r->shelters[1].position);
    observe_objective(&p, &s, 48, r->shelters[2].position);
    wake_plugin_before_tick(&p);
    CHECK(p.objective.hud[0].value == 0);
    boat_session_destroy(&s);
    return 0;
}

static int objective_revision_failure(void)
{
    static WakePlugin p;
    static BoatPluginFiles candidate;
    BoatSession s;
    boat_session_init(&s, outer(), calm());
    CHECK(wake_plugin_open(&p, "plugins/shelter-tour", &s, 0));
    uint64_t initial = boat_plugin_objective_digest(&p.objective);
    BoatPluginBundle denied = original();
    denied.source = p.files.source;
    denied.source_size = p.files.source_size;
    CHECK(!boat_plugin_host_stage(&p.host, &denied)); // Callbacks without grant.
    CHECK(p.host.diagnostic.status == BOAT_PLUGIN_SCHEMA);
    denied = boat_plugin_bundle(&p.files);
    denied.source = reference()->source;
    denied.source_size = reference()->source_size;
    CHECK(!boat_plugin_host_stage(&p.host, &denied)); // Grant without callbacks.
    CHECK(p.host.diagnostic.status == BOAT_PLUGIN_SCHEMA);
    candidate = p.files;
    int failed_size = snprintf(candidate.source, sizeof(candidate.source),
        "local p=(function() %.*s\nend)(); "
        "p.init=function() error('candidate init failure') end; return p",
        (int)p.files.source_size, p.files.source);
    CHECK(failed_size > 0 && (size_t)failed_size < sizeof(candidate.source));
    candidate.source_size = (size_t)failed_size;
    BoatPluginBundle failed = boat_plugin_bundle(&candidate);
    CHECK(!boat_plugin_host_stage(&p.host, &failed));
    CHECK(p.host.diagnostic.status == BOAT_PLUGIN_RUNTIME);
    CHECK(boat_plugin_host_active(&p.host)->number == 1 && p.generation == 1);
    CHECK(boat_plugin_objective_digest(&p.objective) == initial);
    candidate = p.files;
    // Only update fails; staging exercises init without changing active source/state.
    const char suffix[] = "\n-- new immutable behavior revision\n";
    memcpy(candidate.source + candidate.source_size, suffix, sizeof(suffix) - 1);
    candidate.source_size += sizeof(suffix) - 1;
    BoatPluginBundle bundle = boat_plugin_bundle(&candidate);
    CHECK(boat_plugin_host_stage(&p.host, &bundle));
    CHECK(p.host.change == BOAT_PLUGIN_RESTART_VOYAGE);
    const BoatPluginRevision *held = boat_plugin_host_held(&p.host);
    CHECK(held && held->source_size == candidate.source_size);
    memset(candidate.source, '!', candidate.source_size);
    CHECK(held->source[0] == '-'); // Not borrowed from the read/staging scratch buffer.
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 1));
    CHECK(p.generation == 2 && p.objective.hud[0].value == 0);
    CHECK(boat_plugin_host_active(&p.host)->number == 2);
    CHECK(!wake_plugin_frame(&p, &s, false, true, false, 2));
    CHECK(boat_plugin_host_held(&p.host));
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 3));
    CHECK(boat_plugin_host_active(&p.host)->number == 1 && p.generation == 3);
    // Inject a runtime failure into an accepted revision to exercise adapter quarantine.
    BoatPluginRevision *active = &p.host.slots[p.host.history[p.host.depth - 1]];
    const char bad[] = "return {init=function() return {},{} end, "
                       "update=function(c,s) c.random(); error('adapter failure') end}";
    memcpy(active->source, bad, sizeof(bad) - 1);
    active->source_size = sizeof(bad) - 1;
    uint64_t before = boat_plugin_objective_digest(&p.objective);
    observe_objective(&p, &s, 12, s.voyage.route->arrival_centre);
    CHECK(s.voyage.arrived && p.objective.quarantined && !p.objective_pending);
    CHECK(boat_plugin_objective_digest(&p.objective) == before);
    CHECK(p.event_count && p.objective.acknowledged == 0);
    CHECK(strstr(p.objective_diagnostic.message, "adapter failure"));
    boat_session_destroy(&s);
    return 0;
}

static int objective_overflow(void)
{
    static WakePlugin p;
    BoatSession s;
    boat_session_init(&s, outer(), calm());
    CHECK(wake_plugin_open(&p, "plugins/shelter-tour", &s, 0));
    observe_objective(&p, &s, 12, s.voyage.route->shelters[0].position);
    CHECK(p.objective_pending);
    for (unsigned i = 0; i <= BOAT_PLUGIN_EVENTS; ++i) {
        boat_session_recover(&s);
        wake_plugin_after_tick(&p, &s);
    }
    CHECK(p.objective.quarantined && p.event_count == BOAT_PLUGIN_EVENTS);
    CHECK(p.objective.acknowledged == 0 && !p.objective_pending);
    CHECK(strstr(p.objective_diagnostic.message, "overflow"));
    observe_objective(&p, &s, 24, s.voyage.route->arrival_centre);
    CHECK(s.voyage.arrived);
    boat_session_destroy(&s);
    return 0;
}

static int objective_tick_parity(void)
{
    static WakePlugin p, replay;
    BoatSession a, b, repeated;
    boat_session_init(&a, outer(), calm());
    boat_session_init(&b, outer(), calm());
    boat_session_init(&repeated, outer(), calm());
    clock_t launch_started = clock();
    CHECK(wake_plugin_open(&p, "plugins/shelter-tour", &a, 0));
    CHECK(wake_plugin_open(&replay, "plugins/shelter-tour", &repeated, 0));
    clock_t launch_cost = clock() - launch_started, callback_cost = 0;
    for (unsigned tick = 1; tick <= 1200; ++tick) {
        BoatInput input = { .throttle_rate = tick < 120 ? 1 : 0 };
        wake_plugin_before_tick(&p);
        wake_plugin_before_tick(&replay);
        CHECK(boat_session_step(&a, input));
        CHECK(boat_session_step(&b, input));
        CHECK(boat_session_step(&repeated, input));
        clock_t callback_started = tick % 12 == 0 ? clock() : 0;
        wake_plugin_after_tick(&p, &a);
        wake_plugin_after_tick(&replay, &repeated);
        if (tick % 12 == 0)
            callback_cost += clock() - callback_started;
        CHECK(same_boat(&a.boat, &b.boat));
        CHECK(same_progress(a.voyage, b.voyage));
        CHECK(same_storm(a.world.water.storm, b.world.water.storm));
        CHECK(boat_plugin_objective_digest(&p.objective)
            == boat_plugin_objective_digest(&replay.objective));
        CHECK(p.objective_pending == replay.objective_pending);
    }
    printf("live_hash=%016llx lua_peak=%zu instructions=%llu host_bytes=%zu adapter_bytes=%zu\n",
        (unsigned long long)boat_plugin_objective_digest(&p.objective),
        p.objective_diagnostic.cost.peak_bytes,
        (unsigned long long)p.objective_diagnostic.cost.instructions, sizeof(BoatPluginHost),
        sizeof(WakePlugin));
    printf("launch_pair_cpu_ms=%.3f callbacks_200_cpu_ms=%.3f\n",
        1000.0 * launch_cost / CLOCKS_PER_SEC, 1000.0 * callback_cost / CLOCKS_PER_SEC);
    boat_session_destroy(&a);
    boat_session_destroy(&b);
    boat_session_destroy(&repeated);
    return 0;
}

// Use the production revision host and T/T+1 adapter, including reload while a due
// transition is staged. The source is immutable candidate bytes, exactly like a save.
static int objective_timers_reload(void)
{
    static WakePlugin p;
    static BoatPluginFiles files;
    BoatSession s;
    boat_session_init(&s, outer(), calm());
    CHECK(wake_plugin_open(&p, "plugins/shelter-tour", &s, 0));
    files = p.files;
    int n = snprintf(files.manifest, sizeof(files.manifest),
        "{\"id\":\"proteus.shelter-tour\",\"name\":\"Timer test\",\"api\":1,"
        "\"kind\":\"voyage\",\"entry\":\"main.lua\",\"location\":\"outer\","
        "\"capabilities\":[\"voyage.define\",\"objective.hud\",\"objective.timers\"],"
        "\"state_version\":1}");
    CHECK(n > 0 && (size_t)n < sizeof(files.manifest));
    files.manifest_size = (size_t)n;
    n = snprintf(files.source, sizeof(files.source),
        "local p=(function() %.*s\nend)(); "
        "p.init=function(c) return {fired=0}, {{kind='timer.set',generation=c.generation,"
        "id='reminder',tick=c.tick+13}} end; "
        "p.update=function(c,s) s.fired=s.fired+#c.timers; return s, "
        "{{kind='hud.set',generation=c.generation,id='timer',label='FIRED',value=s.fired}} end;"
        "return p",
        (int)p.files.source_size, p.files.source);
    CHECK(n > 0 && (size_t)n < sizeof(files.source));
    files.source_size = (size_t)n;
    BoatPluginBundle bundle = boat_plugin_bundle(&files);
    CHECK(boat_plugin_host_stage(&p.host, &bundle));
    CHECK(p.host.change == BOAT_PLUGIN_RESTART_VOYAGE);
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 1));
    CHECK(p.objective.timers_enabled && p.objective.timer_count == 1);
    const BoatVoyageRoute *route = s.voyage.route;
    observe_objective(&p, &s, 12, route->shelters[0].position);
    wake_plugin_before_tick(&p);
    CHECK(p.objective.timer_count == 1 && p.objective.hud[0].value == 0);
    observe_objective(&p, &s, 24, route->shelters[0].position);
    CHECK(p.objective_pending && p.objective.timer_count == 1);
    CHECK(!p.staged_objective.timer_count && p.staged_objective.hud[0].value == 1);
    // Metadata rename preserves committed timers and their pending consumption.
    char *name = strstr(files.manifest, "Timer test");
    CHECK(name);
    name[0] = 'R';
    CHECK(boat_plugin_host_stage(&p.host, &bundle));
    CHECK(p.host.change == BOAT_PLUGIN_RENAMED);
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 2));
    CHECK(p.objective_pending && p.objective.timer_count == 1);
    wake_plugin_before_tick(&p);
    CHECK(!p.objective.timer_count && p.objective.hud[0].value == 1);
    // Behavior edit waits for explicit restart; native clock is already at 24.
    files.source[files.source_size++] = ' ';
    bundle = boat_plugin_bundle(&files);
    CHECK(boat_plugin_host_stage(&p.host, &bundle));
    CHECK(p.host.change == BOAT_PLUGIN_RESTART_VOYAGE);
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 3));
    CHECK(p.objective.timer_count == 1 && p.objective.timers[0].tick == 37);
    unsigned generation = p.generation;
    observe_objective(&p, &s, 48, s.voyage.route->shelters[0].position);
    CHECK(p.objective_pending);
    // Undo code restarts with a fresh generation, dropping the due pending output.
    CHECK(!wake_plugin_frame(&p, &s, false, true, false, 4));
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 5));
    CHECK(p.generation == generation + 1 && !p.objective_pending);
    CHECK(p.objective.timer_count == 1 && p.objective.timers[0].tick == 61);
    CHECK(!p.objective.hud_count);
    uint64_t before = boat_plugin_objective_digest(&p.objective);
    // Failed init cannot replace the active timer or grant.
    const char *failure = "return {init=function() error('timer candidate') end,"
                          "update=function(c,s) return s,{} end}";
    n = snprintf(files.source, sizeof(files.source),
        "local p=(function() %.*s\nend)(); local q=(function() %s end)(); "
        "p.init=q.init; return p",
        (int)p.files.source_size, p.files.source, failure);
    CHECK(n > 0 && (size_t)n < sizeof(files.source));
    files.source_size = (size_t)n;
    bundle = boat_plugin_bundle(&files);
    CHECK(!boat_plugin_host_stage(&p.host, &bundle));
    CHECK(boat_plugin_objective_digest(&p.objective) == before);
    observe_objective(&p, &s, 72, s.voyage.route->shelters[0].position);
    CHECK(p.objective_pending && !p.staged_objective.timer_count);
    for (unsigned i = 0; i <= BOAT_PLUGIN_EVENTS; ++i) {
        boat_session_recover(&s);
        wake_plugin_after_tick(&p, &s);
    }
    CHECK(p.objective.quarantined && !p.objective_pending);
    CHECK(p.objective.timer_count == 1 && boat_plugin_objective_digest(&p.objective) == before);
    wake_plugin_before_tick(&p);
    CHECK(p.objective.timer_count == 1); // Discarded staged output never consumes a due timer.
    boat_session_destroy(&s);
    return 0;
}

// Wrap immutable accepted source to author migration candidates without changing the route.
static bool migration_bundle(
    BoatPluginFiles *f, const BoatPluginFiles *base, unsigned version, const char *callback)
{
    *f = *base;
    int n = snprintf(f->manifest, sizeof(f->manifest),
        "{\"id\":\"proteus.shelter-tour\",\"name\":\"Migration\",\"api\":1,"
        "\"kind\":\"voyage\",\"entry\":\"main.lua\",\"location\":\"outer\","
        "\"capabilities\":[\"voyage.define\",\"objective.hud\"],\"state_version\":%u}",
        version);
    if (n <= 0 || (size_t)n >= sizeof(f->manifest))
        return false;
    f->manifest_size = (size_t)n;
    n = snprintf(f->source, sizeof(f->source),
        "local p=(function() %.*s\nend)(); p.migrate=%s; return p", (int)base->source_size,
        base->source, callback);
    if (n <= 0 || (size_t)n >= sizeof(f->source))
        return false;
    f->source_size = (size_t)n;
    return true;
}

static int live_migration(void)
{
    static WakePlugin p, second;
    static BoatPluginFiles f;
    static WakePluginPreview checkpoint;
    BoatSession s;
    boat_session_init(&s, outer(), calm());
    CHECK(wake_plugin_open(&p, "plugins/shelter-tour", &s, 0));
    CHECK(!wake_plugin_open(&second, "plugins/shelter-tour", &s, 0));
    observe_objective(&p, &s, 12, s.voyage.route->shelters[1].position);
    CHECK(p.objective_pending && p.staged_objective.hud[0].value == 1);
    uint64_t before = boat_plugin_objective_digest(&p.objective);
    uint64_t pending = boat_plugin_objective_digest(&p.staged_objective);
    unsigned queued = p.event_count;
    CHECK(migration_bundle(
        &f, &p.files, 2, "function(c,s) c.random(); error('migration rejected') end"));
    BoatPluginBundle bundle = boat_plugin_bundle(&f);
    CHECK(boat_plugin_host_stage(&p.host, &bundle));
    CHECK(p.host.change == BOAT_PLUGIN_MIGRATE);
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 1));
    CHECK(boat_plugin_host_active(&p.host)->number == 1 && p.generation == 1);
    CHECK(p.objective_pending && boat_plugin_objective_digest(&p.objective) == before);
    CHECK(boat_plugin_objective_digest(&p.staged_objective) == pending && p.event_count == queued);
    CHECK(migration_bundle(&f, &p.files, 2,
        "function(c,s) assert((c.previous_state_version==1 or c.previous_state_version==3) and "
        "c.state_version==2); "
        "s.bridge=false; return s,{{kind='hud.remove',generation=c.generation,id='tour'},"
        "{kind='hud.set',generation=c.generation,id='new',label='MIGRATED',value=7}} end"));
    bundle = boat_plugin_bundle(&f);
    CHECK(boat_plugin_host_stage(&p.host, &bundle));
    Boat boat = s.boat;
    BoatVoyage voyage = s.voyage;
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 2));
    CHECK(p.generation == 2 && p.objective.state_version == 2 && !p.objective_pending);
    CHECK(same_boat(&s.boat, &boat) && same_progress(s.voyage, voyage) && s.tick == 12);
    CHECK(p.event_count == queued && p.objective.acknowledged == 0);
    CHECK(p.objective.hud_count == 1 && !strcmp(p.objective.hud[0].id, "new"));
    observe_objective(&p, &s, 24, s.voyage.route->shelters[1].position);
    wake_plugin_before_tick(&p);
    CHECK(p.objective.acknowledged == p.sequence && p.objective.hud_count == 2);
    CHECK(wake_plugin_preview_save(&checkpoint, &p, &s));
    // Undo old code migrates current state after native time has advanced.
    CHECK(migration_bundle(&f, &p.files, 3, "function(c,s) return s,{} end"));
    bundle = boat_plugin_bundle(&f);
    CHECK(boat_plugin_host_stage(&p.host, &bundle));
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 2));
    CHECK(p.generation == 3 && p.objective.state_version == 3);
    observe_objective(&p, &s, 36, s.voyage.route->shelters[2].position);
    CHECK(p.objective_pending);
    voyage = s.voyage;
    CHECK(!wake_plugin_frame(&p, &s, false, true, false, 2));
    CHECK(p.host.change == BOAT_PLUGIN_MIGRATE);
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 2));
    CHECK(p.generation == 4 && p.objective.state_version == 2 && !p.objective_pending);
    CHECK(s.tick == 36 && same_progress(voyage, s.voyage));
    // An invalid retirement batch cannot remove an already committed row.
    before = boat_plugin_objective_digest(&p.objective);
    CHECK(migration_bundle(&f, &p.files, 3,
        "function(c,s) return s,{{kind='hud.remove',generation=c.generation,id='new'},"
        "{kind='hud.set',generation=1,id='bad',label='BAD',value=1}} end"));
    bundle = boat_plugin_bundle(&f);
    CHECK(boat_plugin_host_stage(&p.host, &bundle));
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 3));
    CHECK(boat_plugin_objective_digest(&p.objective) == before && p.objective.hud_count == 1);
    // Old code has no migration: Undo explicitly resets alongside native voyage progress.
    CHECK(!wake_plugin_frame(&p, &s, false, true, false, 4)); // Discard failed candidate.
    CHECK(!wake_plugin_frame(&p, &s, false, true, false, 5));
    CHECK(p.host.change == BOAT_PLUGIN_RESTART_VOYAGE);
    CHECK(!wake_plugin_frame(&p, &s, false, false, true, 6));
    CHECK(p.generation == 5 && p.objective.state_version == 1 && !p.event_count);
    CHECK(wake_plugin_preview_restore(&checkpoint, &p, &s, NULL, NULL));
    CHECK(p.generation == 2 && p.objective.state_version == 2 && s.tick == 24);
    CHECK(boat_plugin_host_active(&p.host)->voyage.manifest.state_version == 2);
    CHECK(boat_plugin_host_undo(&p.host) && p.host.change == BOAT_PLUGIN_RESTART_VOYAGE);
    boat_session_destroy(&s);
    return 0;
}

static int resource_composition(void)
{
    BoatPluginObjective a = { .hud_count = 1 }, b = { .hud_count = 1 };
    snprintf(a.hud[0].id, sizeof(a.hud[0].id), "same-local-id");
    b.hud[0] = a.hud[0];
    BoatPluginHudOwner owners[] = { { "first", &a }, { "second", &b } };
    BoatPluginHudRow rows[2] = { 0 };
    unsigned count = 99;
    CHECK(boat_plugin_hud_compose(owners, 2, rows, 2, &count));
    CHECK(count == 2 && !strcmp(rows[0].owner, "first") && !strcmp(rows[1].owner, "second"));
    BoatPluginHudRow saved[2];
    memcpy(saved, rows, sizeof(rows));
    owners[1].owner = "first";
    CHECK(!boat_plugin_hud_compose(owners, 2, rows, 2, &count));
    CHECK(count == 2 && !memcmp(saved, rows, sizeof(rows)));
    owners[1].owner = "second";
    CHECK(!boat_plugin_hud_compose(owners, 2, rows, 1, &count));
    CHECK(count == 2 && !memcmp(saved, rows, sizeof(rows)));
    return 0;
}

// Compare declared values only: C struct assignment need not preserve padding bytes (MSVC).
static bool same_wakes(const BoatWakeObjects *a, const BoatWakeObjects *b)
{
    if (a->tick != b->tick || a->emitted != b->emitted || a->expired != b->expired
        || a->life_ticks != b->life_ticks || a->live != b->live || a->peak_live != b->peak_live
        || a->crest_height != b->crest_height)
        return false;
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        const BoatWakeObject *x = &a->objects[i], *y = &b->objects[i];
        if (x->position.x != y->position.x || x->position.y != y->position.y
            || x->direction.x != y->direction.x || x->direction.y != y->direction.y
            || x->length != y->length || x->amplitude != y->amplitude
            || x->inverse_speed != y->inverse_speed || x->born != y->born)
            return false;
    }
    for (unsigned i = 0; i < BOAT_OBJECT_BOATS; ++i) {
        const BoatWakeObjectEmitter *x = &a->emitters[i], *y = &b->emitters[i];
        if (x->start.x != y->start.x || x->start.y != y->start.y || x->started != y->started
            || x->tracked != y->tracked || x->next != y->next || x->strength != y->strength
            || x->strength_sum != y->strength_sum || x->initialized != y->initialized)
            return false;
    }
    return true;
}

static bool same_presentation(const BoatPresentation *a, const BoatPresentation *b)
{
    if (a->flag.flutter_phase != b->flag.flutter_phase || a->flag.initialized != b->flag.initialized
        || a->camera_heading != b->camera_heading || a->next_particle != b->next_particle
        || a->emission != b->emission || a->wake_emission != b->wake_emission
        || a->wake_distance != b->wake_distance || a->spray_time != b->spray_time
        || a->propeller_time != b->propeller_time || a->landing_strength != b->landing_strength
        || a->landing_age != b->landing_age || a->landing_serial != b->landing_serial
        || a->seen_landings != b->seen_landings || a->fall_speed != b->fall_speed)
        return false;
    for (unsigned i = 0; i < BOAT_FLAG_POINTS; ++i) {
        Vector3 x = a->flag.surface.points[i], y = b->flag.surface.points[i];
        Vector3 u = a->flag.previous[i], v = b->flag.previous[i];
        if (x.x != y.x || x.y != y.y || x.z != y.z || u.x != v.x || u.y != v.y || u.z != v.z)
            return false;
    }
    for (unsigned i = 0; i < BOAT_WATER_PARTICLES; ++i) {
        const BoatWaterParticle *x = &a->water[i], *y = &b->water[i];
        if (x->position.x != y->position.x || x->position.y != y->position.y
            || x->velocity.x != y->velocity.x || x->velocity.y != y->velocity.y
            || x->height != y->height || x->lift != y->lift || x->age != y->age
            || x->lifetime != y->lifetime || x->size != y->size || x->spray != y->spray
            || x->wash != y->wash || x->variation != y->variation)
            return false;
    }
    return true;
}

static bool same_trace(const WakePlugin *a, const WakePlugin *b)
{
    if (a->trace_count != b->trace_count)
        return false;
    for (unsigned i = 0; i < a->trace_count; ++i) {
        const WakePluginTrace *x = &a->trace[i], *y = &b->trace[i];
        if (x->tick != y->tick || x->revision != y->revision || x->events != y->events
            || x->transition != y->transition || x->status != y->status || x->before != y->before
            || x->after != y->after || x->command_count != y->command_count)
            return false;
        for (unsigned j = 0; j < x->command_count; ++j) {
            const BoatPluginCommand *u = &x->commands[j], *v = &y->commands[j];
            if (strcmp(u->kind, v->kind) || strcmp(u->id, v->id) || strcmp(u->label, v->label)
                || u->value != v->value || u->generation != v->generation)
                return false;
        }
    }
    return true;
}

// Deliberately change only known padding to exercise the Windows failure on every ABI that
// has these gaps. Semantic differences must still fail the same equality check below.
static void poison_wake_padding(BoatWakeObjects *w)
{
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        unsigned char *bytes = (unsigned char *)&w->objects[i];
        for (size_t j = offsetof(BoatWakeObject, inverse_speed) + sizeof(float);
            j < offsetof(BoatWakeObject, born); ++j)
            bytes[j] ^= 0xff;
    }
    for (unsigned i = 0; i < BOAT_OBJECT_BOATS; ++i) {
        unsigned char *bytes = (unsigned char *)&w->emitters[i];
        for (size_t j = offsetof(BoatWakeObjectEmitter, initialized) + sizeof(bool);
            j < sizeof(BoatWakeObjectEmitter); ++j)
            bytes[j] ^= 0xff;
    }
}

static int preview_seek(void)
{
    static WakePlugin p, resumed;
    static WakePluginPreview checkpoint;
    static WakePluginReviewTick ticks[240];
    BoatSession s, replay;
    BoatWakeObjects objects, restored, traffic, restored_traffic;
    CHECK(boat_wake_objects_init(&objects, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
    boat_session_init(&s, outer(), calm());
    boat_session_init(&replay, outer(), calm());
    s.world.water.objects = &objects;
    CHECK(waterfront_traffic_wakes_init(&traffic, boat_tick_seconds(12)));
    s.world.water.traffic_objects = &traffic;
    CHECK(wake_plugin_open(&p, "plugins/shelter-tour", &s, 0));
    // Save at a boundary with uncommitted output and queued notices.
    observe_objective(&p, &s, 12, s.voyage.route->shelters[1].position);
    objects.tick = s.tick;
    CHECK(p.objective_pending && wake_plugin_preview_save(&checkpoint, &p, &s));
    for (unsigned i = 0; i < 240; ++i) {
        ticks[i].input.throttle_rate = i < 100 ? 1 : 0;
        ticks[i].input.steering_rate = i < 100 ? .2f : -.2f;
        ticks[i].recover = i == 17;
    }
    CHECK(wake_plugin_preview_seek(
        &checkpoint, &resumed, &replay, &restored, &restored_traffic, ticks, 240));
    Boat final_boat = replay.boat;
    BoatVoyage final_voyage = replay.voyage;
    BoatPresentation final_presentation = replay.presentation;
    BoatWakeObjects final_wakes = restored;
    uint64_t final_hash = boat_plugin_objective_digest(&resumed.objective);
    uint64_t staged_hash = boat_plugin_objective_digest(&resumed.staged_objective);
    // Compare seek with the production pre-step wakes / physics / stern / plugin ordering.
    for (unsigned i = 0; i < 240; ++i) {
        if (ticks[i].recover) {
            boat_session_recover(&s);
            wake_plugin_after_tick(&p, &s);
        }
        wake_plugin_before_tick(&p);
        boat_wake_objects_advance(&objects);
        CHECK(waterfront_traffic_wakes_step(&traffic, boat_session_time(&s) + BOAT_TICK_SECONDS));
        CHECK(boat_session_step(&s, ticks[i].input));
        BoatWakeStern stern = boat_wake_stern(&s.boat, &s.world, boat_session_time(&s));
        CHECK(boat_wake_objects_track(&objects, 0, (Vector2) { (float)stern.x, (float)stern.z },
            (float)hypot(stern.vx, stern.vz), (float)stern.wet));
        wake_plugin_after_tick(&p, &s);
    }
    CHECK(same_boat(&s.boat, &replay.boat) && same_progress(s.voyage, replay.voyage));
    poison_wake_padding(&restored);
    CHECK(same_wakes(&objects, &restored));
    ++restored.emitters[0].tracked;
    CHECK(!same_wakes(&objects, &restored));
    --restored.emitters[0].tracked;
    CHECK(same_wakes(&traffic, &restored_traffic));
    CHECK(boat_plugin_objective_digest(&p.objective) == final_hash);
    CHECK(boat_plugin_objective_digest(&p.staged_objective) == staged_hash);
    CHECK(p.event_count == resumed.event_count && p.sequence == resumed.sequence);
    CHECK(same_trace(&p, &resumed));
    // Destroy source ownership before restore: checkpoint owns all revision source/strings.
    memset(&p, 0, sizeof(p));
    CHECK(wake_plugin_preview_seek(
        &checkpoint, &resumed, &replay, &restored, &restored_traffic, ticks, 0));
    CHECK(replay.tick == 12 && resumed.objective_pending && !resumed.watching);
    CHECK(replay.voyage.route == &boat_plugin_host_active(&resumed.host)->voyage.route);
    CHECK(!strcmp(replay.voyage.route->shelters[1].name, "BRIDGE"));
    CHECK(wake_plugin_preview_seek(
        &checkpoint, &resumed, &replay, &restored, &restored_traffic, ticks, 240));
    CHECK(same_boat(&final_boat, &replay.boat) && same_progress(final_voyage, replay.voyage));
    CHECK(same_presentation(&final_presentation, &replay.presentation));
    CHECK(same_wakes(&final_wakes, &restored) && restored.tick == 252);
    CHECK(final_hash == boat_plugin_objective_digest(&resumed.objective));
    CHECK(staged_hash == boat_plugin_objective_digest(&resumed.staged_objective));
    CHECK(resumed.trace_count == WAKE_PLUGIN_TRACE && resumed.trace[15].command_count == 1);
    FILE *inspection = tmpfile();
    CHECK(inspection);
    wake_plugin_inspect(&resumed, inspection);
    rewind(inspection);
    char inspected[16384];
    size_t n = fread(inspected, 1, sizeof(inspected) - 1, inspection);
    inspected[n] = 0;
    fclose(inspection);
    CHECK(strstr(inspected, "schema=1") && strstr(inspected, "state=")
        && strstr(inspected, "command hud.set") && strstr(inspected, "trace tick="));
    replay.record_runs = true;
    CHECK(
        !wake_plugin_preview_restore(&checkpoint, &resumed, &replay, &restored, &restored_traffic));
    replay.record_runs = false;
    boat_session_destroy(&s);
    boat_session_destroy(&replay);
    return 0;
}

static int memory_accounting(void)
{
    BoatPluginFiles files;
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_read("plugins/shelter-tour", NULL, &files, &d) == BOAT_PLUGIN_OK);
    BoatPluginHost *host = calloc(1, sizeof(*host));
    CHECK(host);
    BoatPluginBundle bundle = boat_plugin_bundle(&files);
    CHECK(boat_plugin_host_start(host, boat_location(BOAT_LOCATION_OUTER), &outer()->map, &bundle));
    for (unsigned i = 0; i < 32; ++i) {
        char source[BOAT_PLUGIN_SOURCE_MAX];
        int size = snprintf(source, sizeof(source), "%.*s\n-- revision %u\n",
            (int)files.source_size, files.source, i);
        CHECK(size > 0 && (size_t)size < sizeof(source));
        memset(source + size, ' ', sizeof(source) - (size_t)size);
        bundle.source = source;
        bundle.source_size = sizeof(source);
        CHECK(boat_plugin_host_stage(host, &bundle) && host->held);
        CHECK(host->diagnostic.cost.peak_bytes > 0 && !host->diagnostic.cost.leaked_bytes);
        BoatPluginHostMemory m = boat_plugin_host_memory(host);
        CHECK(m.reserved_bytes == sizeof(*host));
        CHECK(m.retained_revisions == host->depth + 1);
        size_t expected = bundle.source_size;
        for (unsigned j = 0; j < host->depth; ++j)
            expected += host->slots[host->history[j]].source_size;
        CHECK(m.retained_source_bytes == expected);
        CHECK(boat_plugin_host_commit(host));
    }
    CHECK(host->depth == BOAT_PLUGIN_UNDO + 1);
    BoatPluginHostMemory m = boat_plugin_host_memory(host);
    CHECK(boat_plugin_host_undo(host));
    CHECK(boat_plugin_host_memory(host).retained_source_bytes == m.retained_source_bytes);
    CHECK(boat_plugin_host_memory(host).retained_revisions == m.retained_revisions);
    CHECK(boat_plugin_host_commit(host));
    CHECK(boat_plugin_host_memory(host).retained_revisions == BOAT_PLUGIN_UNDO);
    bundle.source = "invalid !";
    bundle.source_size = strlen(bundle.source);
    CHECK(!boat_plugin_host_stage(host, &bundle));
    CHECK(boat_plugin_host_memory(host).retained_revisions == BOAT_PLUGIN_UNDO);
    free(host);
    return 0;
}

static const TestCase cases[] = {
    TEST_CASE(memory_accounting),
    TEST_CASE(live_migration),
    TEST_CASE(resource_composition),
    TEST_CASE(preview_seek),
    TEST_CASE(objective_timers_reload),
    SLOW_CASE(objective_tick_parity),
    TEST_CASE(lossless_events),
    TEST_CASE(live_objective),
    TEST_CASE(objective_revision_failure),
    TEST_CASE(objective_overflow),
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
    TEST_CASE(scene_binding),
    TEST_CASE(harbor_life_scene),
    SLOW_CASE(compiled_parity),
};

int main(int argc, char **argv)
{
    int result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    if (outer_ready)
        boat_world_resources_destroy(&outer_world);
    return result;
}
