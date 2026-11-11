#include "boat_cargo.h"
#include "raymath.h"
#include "waterfront_traffic.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Keep setup and checks active in Release CI as well as Debug.
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "cargo check failed at %d: %s\n", __LINE__, #condition);               \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

// Equality of independently initialized states must ignore C padding, which MSVC need not
// copy or zero during aggregate assignment. Rejected-operation checks below still compare
// an object's original bytes to verify it was not touched.
static bool same_vector(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

static bool same_crane(const BoatCargoCrane *a, const BoatCargoCrane *b)
{
    return same_vector(a->origin, b->origin) && a->heading == b->heading && a->angle == b->angle
        && a->reach == b->reach && a->rope == b->rope && a->preset == b->preset
        && a->hidden == b->hidden;
}

static bool same_sandbox(const BoatCargoSandbox *a, const BoatCargoSandbox *b)
{
    const BoatCargo *x = &a->cargo, *y = &b->cargo;
    if (x->state != y->state || !same_vector(x->position, y->position)
        || !same_vector(x->velocity, y->velocity) || !same_vector(x->deck_local, y->deck_local)
        || !same_vector(x->rotation, y->rotation) || x->settled != y->settled || x->pad != y->pad
        || x->deck_contact != y->deck_contact || x->delivered != y->delivered
        || x->receivers != y->receivers || x->support.kind != y->support.kind
        || x->support.pad != y->support.pad
        || !same_vector(x->support.deck_local, y->support.deck_local) || x->load != y->load
        || x->wet != y->wet || x->rest != y->rest || a->crane_count != b->crane_count
        || a->active != b->active || a->source != b->source || a->boat != b->boat
        || a->moored != b->moored || a->mooring != b->mooring || a->event_count != b->event_count)
        return false;
    for (unsigned i = 0; i < BOAT_SCENE_WORK_SITES; ++i)
        if (!same_crane(&a->cranes[i], &b->cranes[i]))
            return false;
    for (unsigned i = 0; i < BOAT_CARGO_EVENTS; ++i)
        if (a->events[i].kind != b->events[i].kind || a->events[i].site != b->events[i].site
            || a->events[i].load != b->events[i].load)
            return false;
    return true;
}

static int compare_cost(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void step(BoatCargoSandbox *s, Boat *b, BoatWorld *w, int tick)
{
    boat_cargo_step(s, b, w, boat_tick_seconds((uint64_t)tick));
    CHECK(isfinite(s->cargo.position.x) && isfinite(s->cargo.position.y));
    CHECK(isfinite(s->cargo.position.z) && Vector3Length(s->cargo.velocity) < 12.01f);
}

static void check_mooring_and_shadow(BoatWorld *world)
{
    BoatCargoSandbox s;
    Vector2 berth = { 10, 245 };
    boat_cargo_reset_at(&s, berth);
    s.moored = false;
    Boat b = { .position = { 8.2f, 249.3f },
        .heading = .5f,
        .velocity = { .2f, -.8f },
        .height = .15f,
        .pitch = .06f,
        .roll = -.08f,
        .throttle = .3f,
        .engine_response = .2f };
    CHECK(boat_cargo_moor_status(&s, &b).reason == BOAT_CARGO_MOOR_READY);
    Boat rejected = b;
    rejected.velocity.x = 2;
    CHECK(boat_cargo_moor_status(&s, &rejected).reason == BOAT_CARGO_MOOR_FAST);
    BoatCargoSandbox unchanged = s;
    CHECK(!boat_cargo_moor_toggle(&s, &rejected));
    CHECK(memcmp(&s, &unchanged, sizeof(s)) == 0);
    rejected = b;
    rejected.heading = PI;
    CHECK(boat_cargo_moor_status(&s, &rejected).reason == BOAT_CARGO_MOOR_HEADING);
    rejected = b;
    rejected.position.x = berth.x + .7f; // Quay side, outside the qualified capture zone.
    CHECK(boat_cargo_moor_status(&s, &rejected).reason == BOAT_CARGO_MOOR_DISTANCE);
    rejected = b;
    rejected.roll = PI;
    CHECK(boat_cargo_moor_status(&s, &rejected).reason == BOAT_CARGO_MOOR_UPRIGHT);
    CHECK(boat_cargo_moor_toggle(&s, &b));
    CHECK(!s.moored && s.mooring);
    s.cargo.state = BOAT_CARGO_SECURED;
    s.cargo.deck_local = (Vector3) { .4f, BOAT_CARGO_DECK_Y + BOAT_CARGO_HALF, .5f };
    CHECK(!boat_cargo_lash_toggle(&s, &b));
    for (int i = 0; i < 900 && !s.moored; ++i) {
        Boat previous = b;
        boat_cargo_moor_step(&s, &b);
        boat_cargo_step(&s, &b, world, boat_tick_seconds((uint64_t)i));
        CHECK(s.cargo.state == BOAT_CARGO_SECURED);
        CHECK(Vector3Distance(s.cargo.position, boat_pose_attachment(&b, s.cargo.deck_local))
            < .0001f);
        CHECK(Vector2Distance(previous.position, b.position) <= 1.5f * BOAT_STEP + .0001f);
        CHECK(fabsf(previous.heading - b.heading) <= .6f * BOAT_STEP + .0001f);
        CHECK(Vector2Distance(b.position, berth) <= Vector2Distance(previous.position, berth));
        CHECK(b.height == previous.height && b.pitch == previous.pitch && b.roll == previous.roll);
        CHECK(b.throttle == 0 && b.engine_response == 0);
        CHECK(b.wheel == 0 && b.engine_angle == 0);
    }
    CHECK(s.moored && !s.mooring && Vector2Distance(b.position, berth) == 0);
    CHECK(boat_cargo_moor_toggle(&s, &b));
    CHECK(boat_cargo_moor_toggle(&s, &b));
    CHECK(s.mooring);
    CHECK(boat_cargo_moor_toggle(&s, &b)); // Cancel before the next tick without moving anything.
    CHECK(!s.moored && !s.mooring && Vector2Distance(b.position, berth) == 0);
    CHECK(boat_cargo_moor_toggle(&s, &b));
    Vector2 before_capsize = b.position;
    b.roll = PI;
    boat_cargo_moor_step(&s, &b);
    CHECK(!s.mooring && !s.moored && Vector2Distance(b.position, before_capsize) == 0);
    b.roll = 0;
    b.velocity = (Vector2) { 1, -1 };
    b.throttle = 1;
    for (int i = 0; i < 180; ++i) {
        float speed = Vector2Length(b.velocity);
        boat_cargo_brake(&b);
        CHECK(Vector2Length(b.velocity) <= speed && b.velocity.x >= 0 && b.velocity.y <= 0);
    }
    CHECK(Vector2Length(b.velocity) == 0 && b.throttle == 0);
    b = (Boat) { .position = berth, .heading = .7f, .roll = .12f, .pitch = -.08f, .height = .2f };
    Vector3 receiver = boat_pose_attachment(&b, (Vector3) { .4f, BOAT_CARGO_DECK_Y, .4f });
    s.cargo.state = BOAT_CARGO_HANGING;
    s.cargo.deck_contact = false; // The reported regression: shadow must exist before touchdown.
    s.cargo.position = Vector3Add(receiver, (Vector3) { 0, BOAT_CARGO_HALF + 1.2f, 0 });
    BoatCargoShadow shadow = boat_cargo_shadow(&s, &b, world, 3, BOAT_CARGO_SHADOW_DECK);
    CHECK(shadow.count == 4 && fabsf(shadow.clearance - 1.2f) < .0001f);
    for (int i = 0; i < 4; ++i) {
        Vector3 local
            = Vector3Transform(Vector3Subtract(shadow.points[i], boat_pose_hull(&b).position),
                MatrixTranspose(boat_pose_rotation(&b)));
        CHECK(fabsf(local.y - (BOAT_CARGO_DECK_Y + .04f)) < .0001f);
        CHECK(fabsf(local.x) <= BOAT_CARGO_DECK_X + .0001f);
        CHECK(fabsf(local.z) <= BOAT_CARGO_DECK_Z + .0001f);
    }
    b = (Boat) { .position = berth };
    s.cargo.position = boat_pose_attachment(&b, (Vector3) { 1.5f, 2, 0 });
    shadow = boat_cargo_shadow(&s, &b, world, 3, BOAT_CARGO_SHADOW_DECK);
    CHECK(shadow.count == 4); // Partial footprint is clipped at the deck edge.
    for (int i = 0; i < 4; ++i)
        CHECK(shadow.points[i].x <= s.cranes[0].origin.x + BOAT_CARGO_DECK_X + .0001f);
    s.cargo.position.x += 2;
    CHECK(!boat_cargo_shadow(&s, &b, world, 3, BOAT_CARGO_SHADOW_DECK).count);
    s.cargo.position = Vector3Add(s.cranes[0].origin, (Vector3) { -5, 3, 0 });
    shadow = boat_cargo_shadow(&s, &b, world, 3, BOAT_CARGO_SHADOW_PAD);
    CHECK(shadow.count == 4 && fabsf(shadow.clearance - 1.6f) < .0001f);
    for (int i = 0; i < 4; ++i)
        CHECK(shadow.points[i].y == 1.04f);
    s.cargo.position = Vector3Add(s.cranes[0].origin, (Vector3) { 5, 3, 0 });
    shadow = boat_cargo_shadow(&s, &b, world, 3, BOAT_CARGO_SHADOW_WATER);
    CHECK(shadow.count == 4);
    for (int i = 0; i < 4; ++i) {
        Vector3 p = shadow.points[i];
        float expected
            = boat_water_query_borrowed(&world->water, (Vector2) { -p.x, p.z }, 3).height + .04f;
        CHECK(fabsf(p.y - expected) < .0001f);
    }
    s.cargo.position = Vector3Add(s.cranes[0].origin, (Vector3) { -5, 1.5f, 2.8f });
    s.cargo.state = BOAT_CARGO_FLOATING;
    for (int i = 0; i < 120; ++i)
        step(&s, &b, world, i);
    CHECK(s.cargo.state == BOAT_CARGO_PAD && fabsf(s.cargo.position.y - 1.4f) < .0001f);
}

// A turned receiving site behaves as the S0 site in its own frame: berth status and capture,
// assisted alignment to its heading, and a load set down on its pad.
static void check_turned_site(BoatWorld *world, const BoatSceneWorkSite *pair)
{
    BoatCargoSandbox s;
    CHECK(boat_cargo_reset_sites(&s, pair, 2));
    const BoatCargoCrane *receiver = &s.cranes[0];
    CHECK(receiver->heading == 1 && receiver->preset == BOAT_SCENE_WORK_RECEIVING_PLATFORM);
    Vector3 bow = boat_cargo_site_direction(receiver, (Vector3) { 0, 0, -1 });
    Boat probe = { .position = { -105, 0 }, .heading = 1 };
    Vector3 hull_bow = Vector3Subtract(boat_pose_attachment(&probe, (Vector3) { 0, 0, -1 }),
        boat_pose_attachment(&probe, (Vector3) { 0 }));
    CHECK(Vector3Distance(bow, hull_bow) < .0001f);
    Vector3 back
        = boat_cargo_site_local(receiver, boat_cargo_site_point(receiver, (Vector3) { 3, 1, -2 }));
    CHECK(Vector3Distance(back, (Vector3) { 3, 1, -2 }) < .0001f);
    // Same local approach as check_mooring_and_shadow, turned into the receiver's frame.
    CHECK(boat_cargo_moor_toggle(&s, &(Boat) { 0 }) && !s.moored);
    Vector3 approach = boat_cargo_site_point(receiver, (Vector3) { 1.8f, 0, 4.3f });
    Boat b
        = { .position = { -approach.x, approach.z }, .heading = 1.5f, .velocity = { .2f, -.8f } };
    BoatCargoMoorStatus status = boat_cargo_moor_status(&s, &b);
    CHECK(status.site == 0 && status.reason == BOAT_CARGO_MOOR_READY);
    CHECK(fabsf(status.heading_error + .5f) < .0001f);
    b.heading = 1.5f - PI;
    CHECK(boat_cargo_moor_status(&s, &b).reason == BOAT_CARGO_MOOR_HEADING);
    b.heading = 1.5f;
    CHECK(boat_cargo_moor_toggle(&s, &b) && s.active == 0 && s.mooring);
    for (int i = 0; i < 900 && !s.moored; ++i)
        boat_cargo_moor_step(&s, &b);
    CHECK(s.moored && b.heading == 1);
    CHECK(Vector2Distance(b.position, boat_cargo_berth(receiver)) == 0);
    // The receiving pad supports a released load like the source pad.
    s.cargo.state = BOAT_CARGO_FLOATING;
    s.cargo.position = boat_cargo_site_point(receiver, (Vector3) { -5, 1.5f, 2.8f });
    for (int i = 0; i < 120; ++i)
        step(&s, &b, world, i);
    CHECK(s.cargo.state == BOAT_CARGO_PAD && s.cargo.pad == 0);
    CHECK(fabsf(s.cargo.position.y - 1.4f) < .0001f);
    s.cargo.position = boat_cargo_site_point(receiver, (Vector3) { -5, 3, 0 });
    BoatCargoShadow shadow = boat_cargo_shadow(&s, &b, world, 3, BOAT_CARGO_SHADOW_PAD);
    CHECK(shadow.count == 4 && fabsf(shadow.clearance - 1.6f) < .0001f);
    for (int i = 0; i < 4; ++i) {
        Vector3 local = boat_cargo_site_local(receiver, shadow.points[i]);
        CHECK(fabsf(local.x + 5) <= .4801f && fabsf(local.z) <= .4801f);
    }
    // The hook belongs to the active crane, which aims in its own frame.
    Vector3 hook = Vector3Add(boat_cargo_tip(receiver), (Vector3) { 0, -receiver->rope, 0 });
    CHECK(Vector3Distance(boat_cargo_hook(&s), hook) == 0);
}

static unsigned take(BoatCargoSandbox *s, BoatCargoEventKind kind, unsigned site)
{
    BoatCargoEvent events[BOAT_CARGO_EVENTS];
    unsigned count = boat_cargo_take_events(s, events, BOAT_CARGO_EVENTS), matching = 0;
    for (unsigned i = 0; i < count; ++i) {
        CHECK(events[i].kind == kind && events[i].site == site);
        ++matching;
    }
    return matching;
}

// S1c: a delivery commits once per session; lifting it off again, losing and recovering the
// load, or capsizing never repeat it. Hidden sites have no berth or pad until enabled, and an
// occupied site refuses to build.
static void check_delivery_events(BoatWorld *world)
{
    const BoatSceneWorkSite sites[] = {
        { .id = "source", .preset = BOAT_SCENE_WORK_SERVICE_PLATFORM },
        { .id = "receiver", .preset = BOAT_SCENE_WORK_RECEIVING_PLATFORM, .x = 40 },
        { .id = "quay", .preset = BOAT_SCENE_WORK_SERVICE_PLATFORM, .x = 80, .hidden = true },
    };
    BoatCargoSandbox s;
    CHECK(boat_cargo_reset_sites(&s, sites, 3));
    CHECK(s.active == 0 && s.source == 0 && s.cranes[2].hidden && !s.event_count);
    BoatCargoSandbox only_hidden;
    CHECK(!boat_cargo_reset_sites(&only_hidden, sites + 1, 2)); // No visible service platform.
    Boat b = { 0 };
    const BoatCargoCrane *receiver = &s.cranes[1], *quay = &s.cranes[2];

    // A hidden site is not a berth, and its pad does not exist.
    CHECK(boat_cargo_moor_toggle(&s, &b) && !s.moored);
    Boat at_quay = { .position = boat_cargo_berth(quay) };
    CHECK(boat_cargo_moor_status(&s, &at_quay).site != 2);
    s.cargo.state = BOAT_CARGO_FLOATING;
    s.cargo.position = boat_cargo_site_point(quay, (Vector3) { -5, 1.5f, 0 });
    for (int i = 0; i < 120; ++i)
        step(&s, &b, world, i);
    CHECK(s.cargo.state == BOAT_CARGO_FLOATING && s.cargo.position.y < 1);
    CHECK(take(&s, BOAT_CARGO_EVENT_LOST, 0) == 1);

    // Delivery: one event once the released load rests on the receiving pad.
    s.cargo.position = boat_cargo_site_point(receiver, (Vector3) { -5, 1.6f, 0 });
    s.cargo.velocity = (Vector3) { 0 };
    for (int i = 0; i < 30; ++i)
        step(&s, &b, world, i);
    CHECK(s.cargo.state == BOAT_CARGO_PAD && !s.cargo.delivered && !s.event_count);
    for (int i = 30; i < 600; ++i)
        step(&s, &b, world, i);
    CHECK(s.cargo.delivered && take(&s, BOAT_CARGO_EVENT_DELIVERED, 1) == 1);
    // Lifted off and set down again: still one delivery.
    s.cargo.state = BOAT_CARGO_FLOATING;
    s.cargo.position.y += 1.5f;
    for (int i = 0; i < 600; ++i)
        step(&s, &b, world, i);
    CHECK(s.cargo.state == BOAT_CARGO_PAD && !s.event_count);

    // Loss in open water is reported at the first contact only.
    s.cargo.state = BOAT_CARGO_FLOATING;
    s.cargo.position = (Vector3) { -20, 2, 15 };
    for (int i = 0; i < 600; ++i)
        step(&s, &b, world, i);
    CHECK(s.cargo.wet && take(&s, BOAT_CARGO_EVENT_LOST, 0) == 1);

    // Recovery returns the load to its source pad and keeps the committed delivery.
    s.cargo.state = BOAT_CARGO_HANGING;
    CHECK(!boat_cargo_recover(&s) && !s.event_count);
    s.cargo.state = BOAT_CARGO_FLOATING;
    CHECK(boat_cargo_recover(&s) && take(&s, BOAT_CARGO_EVENT_RESET, 0) == 1);
    CHECK(s.cargo.state == BOAT_CARGO_PAD && s.cargo.pad == 0 && s.cargo.delivered);
    CHECK(Vector3Distance(
              s.cargo.position, boat_cargo_site_point(&s.cranes[0], (Vector3) { -5, 1.4f, 0 }))
        == 0);
    for (int i = 0; i < 240; ++i)
        step(&s, &b, world, i);
    CHECK(s.cargo.state == BOAT_CARGO_PAD && !s.event_count);

    // Lashing reports SECURED; capsizing loses the load once and delivers nothing.
    s.cargo = (BoatCargo) { .state = BOAT_CARGO_LOOSE,
        .deck_contact = true,
        .settled = 1,
        .delivered = true,
        .position = boat_pose_attachment(&b, (Vector3) { 0, BOAT_CARGO_DECK_Y + .4f, 0 }) };
    CHECK(boat_cargo_lash_toggle(&s, &b) && take(&s, BOAT_CARGO_EVENT_SECURED, 0) == 1);
    b.roll = PI;
    for (int i = 0; i < 600; ++i)
        step(&s, &b, world, i);
    CHECK(s.cargo.state == BOAT_CARGO_FLOATING && take(&s, BOAT_CARGO_EVENT_LOST, 0) == 1);
    b.roll = 0;

    // Building: refused while the boat or the load is inside, then idempotently refused.
    BoatCargoSandbox before = s;
    CHECK(boat_cargo_site_occupied(&s, 2, &at_quay));
    CHECK(!boat_cargo_site_enable(&s, 2, &at_quay) && memcmp(&s, &before, sizeof(s)) == 0);
    s.cargo.position = boat_cargo_site_point(quay, (Vector3) { -5, 0, 0 });
    CHECK(!boat_cargo_site_enable(&s, 2, &b) && s.cranes[2].hidden);
    CHECK(boat_cargo_recover(&s) && take(&s, BOAT_CARGO_EVENT_RESET, 0) == 1);
    CHECK(!boat_cargo_site_enable(&s, 1, &b) && !boat_cargo_site_enable(&s, 3, &b));
    CHECK(boat_cargo_site_enable(&s, 2, &b) && !s.cranes[2].hidden);
    CHECK(!boat_cargo_site_enable(&s, 2, &b));
    CHECK(boat_cargo_moor_status(&s, &at_quay).site == 2);
    s.cargo.state = BOAT_CARGO_FLOATING;
    s.cargo.position = boat_cargo_site_point(quay, (Vector3) { -5, 1.6f, 0 });
    for (int i = 0; i < 120; ++i)
        step(&s, &b, world, i);
    CHECK(s.cargo.state == BOAT_CARGO_PAD && s.cargo.pad == 2 && !s.event_count);

    // A new session clears the delivery and hides the site again.
    CHECK(boat_cargo_reset_sites(&s, sites, 3));
    CHECK(!s.cargo.delivered && s.cranes[2].hidden && !s.event_count);
}

// The crate's position in the boat's deck frame.
static Vector3 deck_frame(const Boat *b, Vector3 p)
{
    return Vector3Transform(Vector3Subtract(p, boat_pose_attachment(b, (Vector3) { 0 })),
        MatrixTranspose(boat_pose_rotation(b)));
}

// The deck tops a solid hull: a crate overhanging the edge rests on it but cannot be lashed,
// and one whose centre is past the edge is pushed out of the hull's side and falls beside the
// boat. Before this, both sank through the hull to the water (user report, 2026-11-07).
static void check_deck_edge(BoatWorld *world)
{
    const float level = BOAT_CARGO_DECK_Y + BOAT_CARGO_HALF;
    const Vector3 starts[] = { { BOAT_CARGO_DECK_X - .2f, level + .3f, 0 },
        { 0, level + .3f, -BOAT_CARGO_DECK_Z + .15f } };
    for (unsigned i = 0; i < 2; ++i) {
        BoatCargoSandbox s;
        boat_cargo_reset(&s);
        Boat b = { 0 };
        s.cargo.state = BOAT_CARGO_FLOATING;
        s.cargo.position = boat_pose_attachment(&b, starts[i]);
        for (int t = 0; t < 240; ++t)
            step(&s, &b, world, t);
        Vector3 local = deck_frame(&b, s.cargo.position);
        CHECK(s.cargo.state == BOAT_CARGO_LOOSE && s.cargo.deck_contact);
        CHECK(fabsf(local.y - level) < .01f && s.cargo.settled >= .5f);
        CHECK(!boat_cargo_lash_toggle(&s, &b) && s.cargo.state == BOAT_CARGO_LOOSE);
    }
    const Vector3 past[] = { { BOAT_CARGO_DECK_X + .2f, level + .3f, .3f },
        { -BOAT_CARGO_DECK_X - .1f, level + .3f, -.5f },
        { .2f, level + .3f, BOAT_CARGO_DECK_Z + .2f } };
    for (unsigned i = 0; i < 3; ++i) {
        BoatCargoSandbox s;
        boat_cargo_reset(&s);
        Boat b = { 0 };
        s.cargo.state = BOAT_CARGO_FLOATING;
        s.cargo.position = boat_pose_attachment(&b, past[i]);
        for (int t = 0; t < 240; ++t) {
            step(&s, &b, world, t);
            Vector3 local = deck_frame(&b, s.cargo.position);
            bool overlapping = fabsf(local.x) < BOAT_CARGO_DECK_X + BOAT_CARGO_HALF - .001f
                && fabsf(local.z) < BOAT_CARGO_DECK_Z + BOAT_CARGO_HALF - .001f;
            CHECK(!overlapping || local.y > level - .03f); // Never inside the hull.
        }
        CHECK(s.cargo.state == BOAT_CARGO_FLOATING && !s.cargo.deck_contact);
        CHECK(deck_frame(&b, s.cargo.position).y < BOAT_CARGO_DECK_Y);
    }
}

// The only queued event: KIND at SITE for LOAD.
static void take_one(BoatCargoSandbox *s, BoatCargoEventKind kind, unsigned site, unsigned load)
{
    BoatCargoEvent events[BOAT_CARGO_EVENTS];
    CHECK(boat_cargo_take_events(s, events, BOAT_CARGO_EVENTS) == 1);
    CHECK(events[0].kind == kind && events[0].site == site && events[0].load == load);
}

// Set the released load onto SITE's pad and let it rest there.
static void set_down(BoatCargoSandbox *s, Boat *b, BoatWorld *world, unsigned site)
{
    s->cargo.state = BOAT_CARGO_FLOATING;
    s->cargo.velocity = (Vector3) { 0 };
    s->cargo.position = boat_cargo_site_point(&s->cranes[site], (Vector3) { -5, 1.6f, 0 });
    for (int i = 0; i < 600; ++i)
        step(s, b, world, i);
    CHECK(s->cargo.state == BOAT_CARGO_PAD && s->cargo.pad == site);
}

// S1d: a supply uses up the delivered load and starts the next on a built service pad, its new
// source. A load delivers once at each receiver, so one set down at the wrong receiver can
// still reach the right one; events carry the load number.
static void check_supply(BoatWorld *world)
{
    const BoatSceneWorkSite sites[] = {
        { .id = "source", .preset = BOAT_SCENE_WORK_SERVICE_PLATFORM },
        { .id = "landing", .preset = BOAT_SCENE_WORK_RECEIVING_PLATFORM, .x = 40 },
        { .id = "quay", .preset = BOAT_SCENE_WORK_SERVICE_PLATFORM, .x = 80, .hidden = true },
        { .id = "jetty", .preset = BOAT_SCENE_WORK_RECEIVING_PLATFORM, .x = 120, .hidden = true },
    };
    BoatCargoSandbox s;
    CHECK(boat_cargo_reset_sites(&s, sites, 4) && s.cargo.load == 1);
    Boat b = { 0 };
    CHECK(boat_cargo_moor_toggle(&s, &b) && !s.moored);
    // Nothing to use up before a delivery.
    CHECK(!boat_cargo_supply_ready(&s) && !boat_cargo_supply(&s, 0));
    set_down(&s, &b, world, 1);
    take_one(&s, BOAT_CARGO_EVENT_DELIVERED, 1, 1);
    // Only a built service platform takes a supply.
    CHECK(boat_cargo_supply_ready(&s) && !boat_cargo_supply(&s, 2) && !boat_cargo_supply(&s, 1));
    CHECK(boat_cargo_site_enable(&s, 2, &b) && boat_cargo_site_enable(&s, 3, &b));
    CHECK(!boat_cargo_supply(&s, 3) && !boat_cargo_supply(&s, 4));
    // Never while the load is hooked, loose on deck or lashed.
    const BoatCargoState busy[] = { BOAT_CARGO_HANGING, BOAT_CARGO_LOOSE, BOAT_CARGO_SECURED };
    BoatCargo resting = s.cargo;
    for (unsigned i = 0; i < 3; ++i) {
        s.cargo.state = busy[i];
        BoatCargoSandbox before = s;
        CHECK(!boat_cargo_supply_ready(&s) && !boat_cargo_supply(&s, 2));
        CHECK(memcmp(&s, &before, sizeof(s)) == 0);
    }
    s.cargo = resting;
    CHECK(boat_cargo_supply(&s, 2) && !s.event_count);
    CHECK(s.cargo.load == 2 && s.cargo.state == BOAT_CARGO_PAD && s.cargo.pad == 2);
    CHECK(s.source == 2 && !s.cargo.delivered && !s.cargo.receivers);
    CHECK(Vector3Distance(
              s.cargo.position, boat_cargo_site_point(&s.cranes[2], (Vector3) { -5, 1.4f, 0 }))
        == 0);
    CHECK(!boat_cargo_supply(&s, 2)); // Load 2 is not delivered yet.
    // Recovery now returns load 2 to the quay.
    s.cargo.state = BOAT_CARGO_FLOATING;
    CHECK(boat_cargo_recover(&s) && s.cargo.pad == 2 && s.cargo.load == 2);
    take_one(&s, BOAT_CARGO_EVENT_RESET, 2, 2);
    // Load 2 at the landing commits there once, then still commits at the jetty.
    set_down(&s, &b, world, 1);
    take_one(&s, BOAT_CARGO_EVENT_DELIVERED, 1, 2);
    set_down(&s, &b, world, 1);
    CHECK(!s.event_count);
    set_down(&s, &b, world, 3);
    take_one(&s, BOAT_CARGO_EVENT_DELIVERED, 3, 2);
    set_down(&s, &b, world, 3);
    set_down(&s, &b, world, 1);
    CHECK(!s.event_count && s.cargo.receivers == (1u << 1 | 1u << 3));
    // A new session starts at load 1 on the first source, with both sites hidden again.
    CHECK(boat_cargo_reset_sites(&s, sites, 4));
    CHECK(s.cargo.load == 1 && s.source == 0 && s.cranes[2].hidden && s.cranes[3].hidden);
}

static bool snap_equal(const BoatCargoSnapshot *a, const BoatCargoSnapshot *b)
{
    if (a->crane_count != b->crane_count || a->active != b->active || a->load_count != b->load_count
        || a->moored != b->moored)
        return false;
    if (a->position.x != b->position.x || a->position.y != b->position.y
        || a->heading != b->heading)
        return false;
    for (unsigned i = 0; i < BOAT_SCENE_WORK_SITES; ++i)
        if (a->built[i] != b->built[i])
            return false;
    for (unsigned i = 0; i < BOAT_CARGO_SNAPSHOT_LOADS; ++i) {
        const BoatCargoLoadSnapshot *x = &a->loads[i], *y = &b->loads[i];
        if (x->load != y->load || x->source != y->source || x->delivered != y->delivered
            || x->receivers != y->receivers || x->support.kind != y->support.kind
            || x->support.pad != y->support.pad
            || x->support.deck_local.x != y->support.deck_local.x
            || x->support.deck_local.y != y->support.deck_local.y
            || x->support.deck_local.z != y->support.deck_local.z)
            return false;
    }
    return true;
}

static bool restore_to(
    const BoatSceneWorkSite *sites, BoatCargoSandbox *s, Boat *b, const BoatCargoSnapshot *snapshot)
{
    return boat_cargo_restore(s, 0, sites, 4, snapshot, b);
}

static void expect_refused(
    const BoatSceneWorkSite *sites, BoatCargoSandbox *s, Boat *b, const BoatCargoSnapshot *snapshot)
{
    BoatCargoSandbox before_sandbox = *s;
    Boat before_boat = *b;
    CHECK(!restore_to(sites, s, b, snapshot));
    CHECK(memcmp(s, &before_sandbox, sizeof(*s)) == 0);
    CHECK(memcmp(b, &before_boat, sizeof(*b)) == 0);
}

// Snapshot/restore keeps the built sites, the stable load support and the boat pose, and refuses
// snapshots that do not describe a resumable session.
static void check_snapshot(BoatWorld *world)
{
    const BoatSceneWorkSite sites[] = {
        { .id = "source", .preset = BOAT_SCENE_WORK_SERVICE_PLATFORM },
        { .id = "landing", .preset = BOAT_SCENE_WORK_RECEIVING_PLATFORM, .x = 40 },
        { .id = "quay", .preset = BOAT_SCENE_WORK_SERVICE_PLATFORM, .x = 80, .hidden = true },
        { .id = "jetty", .preset = BOAT_SCENE_WORK_RECEIVING_PLATFORM, .x = 120, .hidden = true },
    };
    BoatCargoSandbox s;
    BoatCargoSandbox fresh = { 0 };
    Boat b = { 0 };
    Boat restored = { 0 };

    // 1. A fresh pad snapshot round-trips unchanged.
    CHECK(boat_cargo_reset_sites(&s, sites, 4));
    BoatCargoSnapshot pad = { 0 }, again = { 0 };
    boat_cargo_snapshot(&s, &b, &pad);
    CHECK(pad.crane_count == 4 && pad.active == 0 && pad.load_count == 1);
    CHECK(pad.loads[0].support.kind == BOAT_CARGO_SUPPORT_PAD && pad.loads[0].support.pad == 0);
    CHECK(pad.moored && pad.loads[0].load == 1);
    CHECK(pad.built[0] && pad.built[1] && !pad.built[2] && !pad.built[3]);
    CHECK(boat_cargo_reset_sites(&fresh, sites, 4));
    CHECK(restore_to(sites, &fresh, &restored, &pad));
    boat_cargo_snapshot(&fresh, &restored, &again);
    CHECK(snap_equal(&pad, &again));

    // 2. A hanging load records the pad it last rested on and resumes released there.
    CHECK(boat_cargo_reset_sites(&s, sites, 4) && s.moored);
    b = (Boat) { 0 };
    CHECK(boat_cargo_hook_toggle(&s) && s.cargo.state == BOAT_CARGO_HANGING);
    for (int i = 0; i < 12; ++i) {
        boat_cargo_motor(&s.cranes[0], 0, 0, 1);
        step(&s, &b, world, i);
    }
    CHECK(s.cargo.state == BOAT_CARGO_HANGING);
    BoatCargoSnapshot hanging = { 0 };
    boat_cargo_snapshot(&s, &b, &hanging);
    CHECK(hanging.loads[0].support.kind == BOAT_CARGO_SUPPORT_PAD);
    CHECK(hanging.loads[0].support.pad == hanging.loads[0].source);
    CHECK(boat_cargo_reset_sites(&fresh, sites, 4));
    restored = (Boat) { 0 };
    CHECK(restore_to(sites, &fresh, &restored, &hanging));
    CHECK(fresh.cargo.state == BOAT_CARGO_PAD);
    CHECK(Vector3Distance(fresh.cargo.position,
              boat_cargo_site_point(
                  &fresh.cranes[hanging.loads[0].support.pad], (Vector3) { -5, 1.4f, 0 }))
        == 0);
    CHECK(fresh.cargo.state != BOAT_CARGO_HANGING);

    // 3. A lashed load resumes secured, and unlashing keeps the deck as last support.
    CHECK(boat_cargo_reset_sites(&s, sites, 4));
    b = (Boat) { 0 };
    s.cargo = (BoatCargo) { .state = BOAT_CARGO_LOOSE,
        .deck_contact = true,
        .settled = 1,
        .deck_local = { .2f, BOAT_CARGO_DECK_Y + BOAT_CARGO_HALF, .3f },
        .load = 1 };
    CHECK(boat_cargo_lash_toggle(&s, &b) && s.cargo.state == BOAT_CARGO_SECURED);
    BoatCargoSnapshot secured = { 0 };
    boat_cargo_snapshot(&s, &b, &secured);
    CHECK(secured.loads[0].support.kind == BOAT_CARGO_SUPPORT_DECK);
    CHECK(Vector3Distance(secured.loads[0].support.deck_local, s.cargo.deck_local) == 0);
    CHECK(boat_cargo_reset_sites(&fresh, sites, 4));
    restored = (Boat) { 0 };
    CHECK(restore_to(sites, &fresh, &restored, &secured));
    CHECK(fresh.cargo.state == BOAT_CARGO_SECURED);
    CHECK(Vector3Distance(fresh.cargo.position,
              boat_pose_attachment(&restored, secured.loads[0].support.deck_local))
        < .0001f);
    CHECK(boat_cargo_lash_toggle(&fresh, &restored));
    CHECK(fresh.cargo.state == BOAT_CARGO_LOOSE);
    BoatCargoSnapshot loose = { 0 };
    boat_cargo_snapshot(&fresh, &restored, &loose);
    CHECK(loose.loads[0].support.kind == BOAT_CARGO_SUPPORT_DECK);
    CHECK(Vector3Distance(loose.loads[0].support.deck_local, secured.loads[0].support.deck_local)
        == 0);

    // 4. A floating load records its source pad and keeps the committed delivery.
    CHECK(boat_cargo_reset_sites(&s, sites, 4));
    b = (Boat) { 0 };
    set_down(&s, &b, world, 1);
    take_one(&s, BOAT_CARGO_EVENT_DELIVERED, 1, 1);
    s.cargo.state = BOAT_CARGO_FLOATING;
    BoatCargoSnapshot floating = { 0 };
    boat_cargo_snapshot(&s, &b, &floating);
    CHECK(floating.loads[0].support.kind == BOAT_CARGO_SUPPORT_PAD);
    CHECK(floating.loads[0].support.pad == 0);
    CHECK(floating.loads[0].delivered && floating.loads[0].receivers == (1u << 1));
    CHECK(boat_cargo_reset_sites(&fresh, sites, 4));
    restored = (Boat) { 0 };
    CHECK(restore_to(sites, &fresh, &restored, &floating));
    CHECK(fresh.cargo.state == BOAT_CARGO_PAD && fresh.cargo.pad == 0);
    CHECK(fresh.cargo.delivered && fresh.cargo.receivers == (1u << 1));

    // 5. A capsized or mooring boat resumes at the active berth.
    CHECK(boat_cargo_reset_sites(&s, sites, 4));
    s.moored = false;
    Boat capsized = { .motion_state = BOAT_CAPSIZED };
    BoatCargoSnapshot rest = { 0 };
    boat_cargo_snapshot(&s, &capsized, &rest);
    CHECK(rest.moored);
    CHECK(Vector2Distance(rest.position, boat_cargo_berth(&s.cranes[s.active])) == 0);
    s.moored = false;
    s.mooring = true;
    boat_cargo_snapshot(&s, &b, &rest);
    CHECK(rest.moored);

    // 6. A free boat keeps its pose and resumes still.
    CHECK(boat_cargo_reset_sites(&s, sites, 4));
    s.moored = false;
    Boat free_boat = { .position = { 12, 34 }, .heading = .5f };
    BoatCargoSnapshot free_snap = { 0 };
    boat_cargo_snapshot(&s, &free_boat, &free_snap);
    CHECK(!free_snap.moored);
    CHECK(Vector2Distance(free_snap.position, (Vector2) { 12, 34 }) == 0);
    CHECK(free_snap.heading == .5f);
    CHECK(boat_cargo_reset_sites(&fresh, sites, 4));
    restored = (Boat) { .velocity = { 1, 2 } };
    CHECK(restore_to(sites, &fresh, &restored, &free_snap));
    CHECK(!fresh.moored);
    CHECK(Vector2Distance(restored.position, (Vector2) { 12, 34 }) == 0);
    CHECK(restored.heading == .5f && Vector2Length(restored.velocity) == 0);

    // 7. Refused snapshots leave both populated outputs untouched.
    CHECK(boat_cargo_reset_sites(&s, sites, 4));
    b = (Boat) { 0 };
    BoatCargoSnapshot valid = { 0 }, bad = { 0 };
    boat_cargo_snapshot(&s, &b, &valid);
    bad = valid;
    bad.crane_count = 3;
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.active = 4;
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.built[bad.active] = false;
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.loads[0].source = 2; // Hidden site.
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.loads[0].support.pad = 2; // Hidden pad.
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.loads[0].receivers = 1u << 4; // Beyond CRANE_COUNT.
    bad.loads[0].delivered = true;
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.loads[0].receivers = 1u << 0; // A service platform.
    bad.loads[0].delivered = true;
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.loads[0].delivered = true; // No receiver to match.
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.moored = false;
    bad.position.x = NAN;
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.heading = NAN;
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.loads[0].support.kind = BOAT_CARGO_SUPPORT_DECK;
    bad.loads[0].support.deck_local = (Vector3) { 1, 1.15f, .3f }; // Outside the deck.
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.load_count = 0;
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.loads[0].load = 0;
    expect_refused(sites, &s, &b, &bad);
    bad = valid;
    bad.built[1] = false; // Visible in sites but not built.
    expect_refused(sites, &s, &b, &bad);

    // 8. Built flags survive; a new reset sandbox has the hidden sites absent again.
    CHECK(boat_cargo_reset_sites(&s, sites, 4));
    b = (Boat) { 0 };
    set_down(&s, &b, world, 1);
    take_one(&s, BOAT_CARGO_EVENT_DELIVERED, 1, 1);
    CHECK(boat_cargo_site_enable(&s, 2, &b) && boat_cargo_site_enable(&s, 3, &b));
    CHECK(boat_cargo_supply(&s, 2));
    BoatCargoSnapshot built = { 0 };
    boat_cargo_snapshot(&s, &b, &built);
    CHECK(built.built[2] && built.built[3]);
    CHECK(boat_cargo_reset_sites(&fresh, sites, 4));
    CHECK(fresh.cranes[2].hidden && fresh.cranes[3].hidden);
    restored = (Boat) { 0 };
    CHECK(restore_to(sites, &fresh, &restored, &built));
    CHECK(!fresh.cranes[2].hidden && !fresh.cranes[3].hidden);
    CHECK(fresh.source == 2 && fresh.cargo.load == 2);
    CHECK(Vector3Distance(fresh.cargo.position,
              boat_cargo_site_point(&fresh.cranes[2], (Vector3) { -5, 1.4f, 0 }))
        == 0);

    // 9. Two restores stay identical and never re-deliver the committed load.
    CHECK(boat_cargo_reset_sites(&s, sites, 4));
    b = (Boat) { 0 };
    set_down(&s, &b, world, 1);
    take_one(&s, BOAT_CARGO_EVENT_DELIVERED, 1, 1);
    BoatCargoSnapshot delivered = { 0 };
    boat_cargo_snapshot(&s, &b, &delivered);
    BoatCargoSandbox first = { 0 }, second = { 0 };
    Boat first_boat = { 0 }, second_boat = { 0 };
    CHECK(restore_to(sites, &first, &first_boat, &delivered));
    CHECK(restore_to(sites, &second, &second_boat, &delivered));
    for (int i = 0; i < 1200; ++i) {
        float slew = (i / 180) % 2 ? 1 : -1;
        boat_cargo_motor(&first.cranes[0], slew, sinf(i * .01f), cosf(i * .02f));
        boat_cargo_motor(&second.cranes[0], slew, sinf(i * .01f), cosf(i * .02f));
        step(&first, &first_boat, world, i);
        step(&second, &second_boat, world, i);
        CHECK(same_sandbox(&first, &second));
        CHECK(first.event_count == 0 && first.cargo.receivers == (1u << 1));
    }
}

int main(int argc, char **argv)
{
    BoatWorldResources resources = { 0 };
    CHECK(boat_world_resources_init(&resources, boat_location(BOAT_LOCATION_BASIN)));
    BoatWorld world;
    boat_world_init(&world, &resources, boat_water_gentle());
    const BoatCargoWorkboat *flat = boat_cargo_workboat(0);
    CHECK(flat->deck_x == BOAT_CARGO_DECK_X && flat->thrust == 3.1f);
    CHECK(boat_cargo_workboat(99) == boat_cargo_workboat(0));
    check_mooring_and_shadow(&world);
    check_delivery_events(&world);
    check_supply(&world);
    check_snapshot(&world);
    check_deck_edge(&world);
    Boat boat = { 0 };
    BoatCargoSandbox s, repeat;
    boat_cargo_reset(&s);
    repeat = s;
    CHECK(!boat_cargo_lash_toggle(&s, &boat));
    CHECK(boat_cargo_hook_toggle(&s));
    CHECK(!boat_cargo_moor_toggle(&s, &boat));
    repeat = s;
    for (int i = 0; i < 12000; ++i) {
        float slew = (i / 180) % 2 ? 1 : -1;
        boat_cargo_motor(&s.cranes[0], slew, sinf(i * .01f), cosf(i * .02f));
        boat_cargo_motor(&repeat.cranes[0], slew, sinf(i * .01f), cosf(i * .02f));
        step(&s, &boat, &world, i);
        step(&repeat, &boat, &world, i);
        CHECK(same_sandbox(&s, &repeat));
        Vector3 anchor
            = Vector3Subtract(boat_cargo_tip(&s.cranes[0]), (Vector3) { 0, BOAT_CARGO_HALF, 0 });
        CHECK(Vector3Distance(s.cargo.position, anchor) <= s.cranes[0].rope + .02f);
        CHECK(s.cranes[0].reach >= 2 && s.cranes[0].reach <= 7);
        CHECK(s.cranes[0].rope >= .7f && s.cranes[0].rope <= 5.2f);
    }
    // Detachment is atomic and cannot move or accelerate the load.
    Vector3 pos = s.cargo.position, velocity = s.cargo.velocity;
    CHECK(boat_cargo_hook_toggle(&s));
    CHECK(Vector3Distance(pos, s.cargo.position) == 0);
    CHECK(Vector3Distance(velocity, s.cargo.velocity) == 0);
    for (int i = 0; i < 1200; ++i)
        step(&s, &boat, &world, i);
    // Actual crane -> deck -> detached -> lashed transfer, without seeding deck state.
    world.water = (BoatWater) { 0 };
    for (int side = -1; side <= 1; side += 2) {
        boat_cargo_reset(&s);
        CHECK(boat_cargo_hook_toggle(&s));
        for (int i = 0; i < 360; ++i) {
            boat_cargo_motor(&s.cranes[0], 0, 0, 1);
            step(&s, &boat, &world, i);
        }
        Vector3 target = { side * .65f, 1.15f, .65f };
        float angle = atan2f(target.x + 4, target.z - 3);
        float radius = hypotf(target.x + 4, target.z - 3);
        for (int i = 0; i < 1800; ++i) {
            boat_cargo_motor(&s.cranes[0],
                Clamp(remainderf(angle - s.cranes[0].angle, 2 * PI) * 3, -1, 1),
                Clamp((radius - s.cranes[0].reach) * 2, -1, 1), 0);
            step(&s, &boat, &world, i);
        }
        for (int i = 0; i < 360; ++i) {
            boat_cargo_motor(&s.cranes[0], 0, 0, -1);
            step(&s, &boat, &world, i);
        }
        CHECK(s.cargo.deck_contact && s.cargo.state == BOAT_CARGO_HANGING);
        CHECK(boat_cargo_hook_toggle(&s));
        for (int i = 0; i < 120; ++i)
            step(&s, &boat, &world, i);
        CHECK(boat_cargo_lash_toggle(&s, &boat));
        CHECK(boat_cargo_moor_toggle(&s, &boat));
    }
    // A moving/wave-posed prescribed deck. Use both lateral placements.
    for (int side = -1; side <= 1; side += 2) {
        boat_cargo_reset(&s);
        s.cargo.state = BOAT_CARGO_LOOSE;
        s.cargo.position = (Vector3) { side * .65f, 1.16f, .7f };
        world.water = (BoatWater) { 0 };
        for (int i = 0; i < 120; ++i)
            step(&s, &boat, &world, i);
        CHECK(s.cargo.state == BOAT_CARGO_LOOSE && s.cargo.settled > .5f);
        CHECK(boat_cargo_lash_toggle(&s, &boat));
        CHECK(!boat_cargo_hook_toggle(&s));
        CHECK(boat_cargo_moor_toggle(&s, &boat));
        for (int i = 0; i < 1200; ++i) {
            boat.position = (Vector2) { sinf(i * .01f), cosf(i * .01f) };
            boat.heading = i * .002f;
            boat.roll = sinf(i * .02f) * .1f;
            boat.pitch = cosf(i * .01f) * .08f;
            boat.yaw_velocity = .24f;
            step(&s, &boat, &world, i);
            CHECK(Vector3Distance(s.cargo.position, boat_pose_attachment(&boat, s.cargo.deck_local))
                < .00001f);
        }
        boat.velocity = (Vector2) { 1, 0 };
        CHECK(!boat_cargo_lash_toggle(&s, &boat));
        boat.velocity = (Vector2) { 0 };
        Vector3 expected = boat_pose_motion(&boat, s.cargo.deck_local).velocity;
        CHECK(boat_cargo_lash_toggle(&s, &boat));
        CHECK(Vector3Distance(expected, s.cargo.velocity) < .00001f);
        boat = (Boat) { 0 };
    }
    // No settling shortcut outside the supported footprint, or on an inverted deck.
    boat_cargo_reset(&s);
    s.cargo.state = BOAT_CARGO_LOOSE;
    s.cargo.position = (Vector3) { 1.4f, 1.15f, 0 };
    for (int i = 0; i < 600; ++i)
        step(&s, &boat, &world, i);
    CHECK(s.cargo.state == BOAT_CARGO_FLOATING);
    CHECK(!boat_cargo_lash_toggle(&s, &boat));
    boat_cargo_reset(&s);
    s.cargo.state = BOAT_CARGO_SECURED;
    s.cargo.deck_local = (Vector3) { .5f, 1.15f, 0 };
    boat.roll = PI;
    step(&s, &boat, &world, 0);
    CHECK(s.cargo.state == BOAT_CARGO_FLOATING);
    boat_cargo_reset(&s);
    CHECK(s.cargo.state == BOAT_CARGO_PAD && s.moored);
    // Empty traces are untouched; secured placements give opposite heel signs.
    Boat empty = { .velocity = { 2, 3 } }, before = empty;
    boat_cargo_load_effect(&s, &before, &empty);
    CHECK(memcmp(&before, &empty, sizeof(empty)) == 0);
    s.cargo.state = BOAT_CARGO_SECURED;
    for (int side = -1; side <= 1; side += 2) {
        Boat loaded = empty;
        loaded.velocity.x += 1;
        s.cargo.deck_local.x = side * .7f;
        boat_cargo_load_effect(&s, &before, &loaded);
        CHECK(loaded.velocity.x < 3 && loaded.velocity.x > 2);
        CHECK(loaded.roll_velocity * side < 0);
    }
    // Translation must preserve the fixed basin behaviour at a mapped work-site origin.
    BoatCargoSandbox translated;
    boat = (Boat) { 0 };
    boat_cargo_reset(&s);
    boat_cargo_reset_at(&translated, (Vector2) { 10, 245 });
    CHECK(boat_cargo_hook_toggle(&s) && boat_cargo_hook_toggle(&translated));
    Boat shifted = { .position = { 10, 245 } };
    for (int i = 0; i < 2400; ++i) {
        boat_cargo_motor(&s.cranes[0], .1f, 0, i < 240 ? 1 : 0);
        boat_cargo_motor(&translated.cranes[0], .1f, 0, i < 240 ? 1 : 0);
        step(&s, &boat, &world, i);
        step(&translated, &shifted, &world, i);
        CHECK(Vector3Distance(s.cargo.position,
                  Vector3Subtract(translated.cargo.position, translated.cranes[0].origin))
            < .02f);
    }
    CHECK(boat_cargo_hook_toggle(&translated));
    CHECK(boat_cargo_moor_toggle(&translated, &shifted));
    CHECK(!boat_cargo_moor_toggle(&translated, &boat));
    CHECK(boat_cargo_moor_toggle(&translated, &shifted));
    // A floating crate stays on compiled water when it reaches an actual harbour bank.
    BoatWorldResources outer = { 0 };
    CHECK(boat_world_resources_init(&outer, boat_location(BOAT_LOCATION_OUTER)));
    BoatWorld mapped;
    boat_world_init(&mapped, &outer, (BoatWater) { 0 });
    float edge = 10;
    while (edge < 80 && boat_map_water(&outer.map, (Vector2) { edge + .1f, 245 }, .5f))
        edge += .1f;
    CHECK(edge > 10 && edge < 80);
    boat_cargo_reset_at(&translated, shifted.position);
    translated.cargo.state = BOAT_CARGO_FLOATING;
    translated.cargo.position = (Vector3) { -edge, .12f, 245 };
    translated.cargo.velocity.x = -12;
    for (int i = 0; i < 240; ++i) {
        step(&translated, &shifted, &mapped, i);
        CHECK(boat_map_water(&outer.map,
            (Vector2) { -translated.cargo.position.x, translated.cargo.position.z }, .5f));
    }
    // S1a: a package site resets exactly like the S0 origin and passes its staging survey.
    BoatSceneWorkSite site = { .id = "eriksberg", .x = 10, .z = 245 };
    BoatCargoSandbox from_site, from_origin;
    CHECK(boat_cargo_reset_sites(&from_site, &site, 1));
    boat_cargo_reset_at(&from_origin, (Vector2) { 10, 245 });
    CHECK(same_sandbox(&from_site, &from_origin));
    CHECK(boat_cargo_site_clear(&outer.map, NULL, boat_water_gentle(), &site));
    BoatSceneWorkSite near = { .id = "near", .x = 20, .z = 250 };
    BoatSceneWorkSite far = { .id = "far", .x = -105, .z = 0 };
    CHECK(!boat_cargo_sites_apart(&site, &near) && boat_cargo_sites_apart(&site, &far));
    // Heading zero keeps S1a's axis-aligned separation; a quarter turn swaps the long axis.
    BoatSceneWorkSite beside = { .id = "beside", .x = 10 + 14.5f, .z = 245 };
    CHECK(boat_cargo_sites_apart(&site, &beside));
    beside.heading = PI / 2;
    CHECK(!boat_cargo_sites_apart(&site, &beside));
    CHECK(boat_cargo_site_near(&site, (Vector2) { 10 + 11.5f, 245 }, 1));
    CHECK(!boat_cargo_site_near(&site, (Vector2) { 10 - 4.5f, 245 }, 1));
    CHECK(!boat_cargo_reset_sites(&from_site, &site, 0));
    // S1b: a receiving platform needs a service platform to start from.
    BoatSceneWorkSite pair[2] = {
        { .id = "receiver", .preset = BOAT_SCENE_WORK_RECEIVING_PLATFORM, .x = -105, .heading = 1 },
        site
    };
    CHECK(!boat_cargo_reset_sites(&from_site, pair, 1));
    CHECK(boat_cargo_reset_sites(&from_site, pair, 2));
    CHECK(from_site.active == 1 && from_site.moored && from_site.cargo.pad == 1);
    CHECK(same_crane(&from_site.cranes[1], &from_origin.cranes[0]));
    CHECK(Vector3Distance(from_site.cargo.position, from_origin.cargo.position) == 0);
    check_turned_site(&mapped, pair);
    // Traffic alone blocks a site: a present empty vessel list clears the same water.
    static BoatSceneDefinition quiet = { .version = BOAT_SCENE_VERSION, .traffic_present = true };
    bool traffic_blocks = false;
    for (int actor = 0; actor < WATERFRONT_TRAFFIC_ALL && !traffic_blocks; ++actor)
        for (double t = 0; t < waterfront_traffic_period(actor) && !traffic_blocks; t += 20) {
            Vector3 p = waterfront_traffic_motion(actor, t).position;
            BoatSceneWorkSite lane = { .id = "lane", .x = -p.x - 2, .z = p.z - 2 };
            traffic_blocks = boat_cargo_site_clear(&outer.map, &quiet, boat_water_gentle(), &lane)
                && !boat_cargo_site_clear(&outer.map, NULL, boat_water_gentle(), &lane);
        }
    CHECK(traffic_blocks);
    boat_world_resources_destroy(&outer);
    puts("cargo: deterministic lift, transfer, moving deck, translated site and recovery checks "
         "passed");
    // Optional measurement only: virtual ticks, no timing assertion or wall-clock wait.
    if (argc == 2 && !strcmp(argv[1], "--benchmark")) {
        boat = (Boat) { 0 };
        boat_cargo_reset(&s);
        CHECK(boat_cargo_hook_toggle(&s));
        double costs[10000], sum = 0;
        for (int i = 0; i < 10000; ++i) {
            struct timespec begin, end;
            timespec_get(&begin, TIME_UTC);
            step(&s, &boat, &world, i);
            timespec_get(&end, TIME_UTC);
            costs[i]
                = (double)(end.tv_sec - begin.tv_sec) * 1e6 + (end.tv_nsec - begin.tv_nsec) * .001;
            sum += costs[i];
        }
        qsort(costs, 10000, sizeof(*costs), compare_cost);
        printf("one hanging cargo tick (timer overhead included): mean %.3f / p95 %.3f / p99 %.3f "
               "us\n",
            sum / 10000, costs[9499], costs[9899]);
    }
    boat_world_resources_destroy(&resources);
    return 0;
}
