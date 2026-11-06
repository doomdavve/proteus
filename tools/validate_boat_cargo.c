#include "boat_cargo.h"
#include "raymath.h"

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

int main(int argc, char **argv)
{
    BoatWorldResources resources = { 0 };
    CHECK(boat_world_resources_init(&resources, boat_location(BOAT_LOCATION_BASIN)));
    BoatWorld world;
    boat_world_init(&world, &resources, boat_water_gentle());
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
        boat_cargo_motor(&s.crane, slew, sinf(i * .01f), cosf(i * .02f));
        boat_cargo_motor(&repeat.crane, slew, sinf(i * .01f), cosf(i * .02f));
        step(&s, &boat, &world, i);
        step(&repeat, &boat, &world, i);
        CHECK(memcmp(&s, &repeat, sizeof(s)) == 0);
        Vector3 anchor
            = Vector3Subtract(boat_cargo_tip(&s.crane), (Vector3) { 0, BOAT_CARGO_HALF, 0 });
        CHECK(Vector3Distance(s.cargo.position, anchor) <= s.crane.rope + .02f);
        CHECK(s.crane.reach >= 2 && s.crane.reach <= 7);
        CHECK(s.crane.rope >= .7f && s.crane.rope <= 5.2f);
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
            boat_cargo_motor(&s.crane, 0, 0, 1);
            step(&s, &boat, &world, i);
        }
        Vector3 target = { side * .65f, 1.15f, .65f };
        float angle = atan2f(target.x + 4, target.z - 3);
        float radius = hypotf(target.x + 4, target.z - 3);
        for (int i = 0; i < 1800; ++i) {
            boat_cargo_motor(&s.crane, Clamp(remainderf(angle - s.crane.angle, 2 * PI) * 3, -1, 1),
                Clamp((radius - s.crane.reach) * 2, -1, 1), 0);
            step(&s, &boat, &world, i);
        }
        for (int i = 0; i < 360; ++i) {
            boat_cargo_motor(&s.crane, 0, 0, -1);
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
    puts("cargo: deterministic lift, transfer, moving deck and recovery checks passed");
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
