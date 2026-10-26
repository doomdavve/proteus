/* Arcade wake contracts, actual hull jumps, bounded stress and complete GPU timing. */
#include "boat_gpu.h"
#include "boat_test_cases.h"
#include "boat_test_session.h"
#include "external/glad.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void track(BoatWakeObjects *w, const BoatSession *session)
{
    BoatWakeStern s = boat_wake_stern(&session->boat, &session->world, boat_session_time(session));
    CHECK(boat_wake_objects_track(
        w, 0, (Vector2) { (float)s.x, (float)s.z }, (float)hypot(s.vx, s.vz), (float)s.wet));
}

static BoatWakeObjects seed(void)
{
    BoatWakeObjects w;
    CHECK(boat_wake_objects_init(&w, BOAT_OBJECT_LIFE_TICKS, 1.8f));
    CHECK(boat_wake_objects_track(&w, 0, (Vector2) { -7.5f, 0 }, 15, 1));
    for (unsigned t = 1; t <= BOAT_OBJECT_EMIT_TICKS; ++t) {
        boat_wake_objects_advance(&w);
        CHECK(boat_wake_objects_track(
            &w, 0, (Vector2) { -7.5f + t * (15.0f / BOAT_OBJECT_EMIT_TICKS), 0 }, 30, 1));
    }
    CHECK(w.live == 1);
    for (unsigned t = 0; t < 120; ++t)
        boat_wake_objects_advance(&w);
    return w;
}

static int contracts(void)
{
    BoatWakeObjects a = seed(), b = a, sum = a;
    Vector2 p = { 2.8f, 2.8f };
    b.objects[0].direction = (Vector2) { 0, 1 };
    sum.objects[BOAT_OBJECT_PER_BOAT] = b.objects[0];
    sum.live = 2;
    BoatWakeObjectSample x = boat_wake_objects_query(&a, p);
    BoatWakeObjectSample y = boat_wake_objects_query(&b, p);
    BoatWakeObjectSample q = boat_wake_objects_query(&sum, p);
    CHECK(fabsf(q.height - x.height - y.height) < 1e-6 && q.height > 3);
    CHECK(Vector2Length(Vector2Subtract(q.gradient, Vector2Add(x.gradient, y.gradient))) < 1e-6);
    CHECK(fabsf(q.vertical_velocity - x.vertical_velocity - y.vertical_velocity) < 1e-6);
    float eps = .002f;
    for (unsigned i = 0; i < 40; ++i) {
        p = (Vector2) { -6 + i * .29f, 2 + i * .081f };
        q = boat_wake_objects_query(&sum, p);
        float dx = (boat_wake_objects_query(&sum, (Vector2) { p.x + eps, p.y }).height
                       - boat_wake_objects_query(&sum, (Vector2) { p.x - eps, p.y }).height)
            / (2 * eps);
        CHECK(fabsf(dx - q.gradient.x) < .002f);
        BoatWakeObjects before = sum, after = sum;
        --before.tick;
        ++after.tick;
        float dt = (boat_wake_objects_query(&after, p).height
                       - boat_wake_objects_query(&before, p).height)
            * 60;
        CHECK(fabsf(dt - q.vertical_velocity) < .003f);
    }
    /* Cancellation and remote queries never rewrite or normalize a crossing. */
    sum.objects[BOAT_OBJECT_PER_BOAT] = sum.objects[0];
    sum.objects[BOAT_OBJECT_PER_BOAT].amplitude *= -1;
    CHECK(boat_wake_objects_query(&sum, p).height == 0);
    q = boat_wake_objects_query(&a, p);
    (void)boat_wake_objects_query(&a, (Vector2) { 1000000, -1000000 });
    CHECK(boat_wake_objects_query(&a, p).height == q.height);
    uint64_t before_emitted = a.emitted;
    CHECK(!boat_wake_objects_track(&a, 4, p, 15, 1));
    CHECK(!boat_wake_objects_track(&a, 1, p, NAN, 1));
    CHECK(a.emitted == before_emitted);
    a.tick = a.objects[0].born + a.life_ticks - 1;
    CHECK(fabsf(boat_wake_objects_query(&a, (Vector2) { 0, 39 }).height) < .00005f);
    boat_wake_objects_advance(&a);
    CHECK(a.live == 0 && a.expired == 1 && boat_wake_objects_query(&a, p).height == 0);
    CHECK(!boat_wake_objects_init(&a, BOAT_OBJECT_LIFE_TICKS + 1, 1.8f));
    puts("object contracts: signed crossing >3m, derivatives, phase, expiry and validation pass");
    return 0;
}

static int continuity(void)
{
    BoatWakeObjects w;
    CHECK(boat_wake_objects_init(&w, BOAT_OBJECT_LIFE_TICKS, 1.8f));
    CHECK(boat_wake_objects_track(&w, 0, (Vector2) { 0 }, 15, 1));
    for (unsigned tick = 1; tick <= 300; ++tick) {
        boat_wake_objects_advance(&w);
        BoatWakeObjects before = w;
        unsigned phase = tick % BOAT_OBJECT_EMIT_TICKS;
        float wet = phase >= BOAT_OBJECT_EMIT_TICKS - 6 || phase == 0 ? .1f : 1;
        CHECK(boat_wake_objects_track(&w, 0, (Vector2) { tick * .125f, 0 }, 15, wet));
        if (w.emitted != before.emitted) {
            // Birth must not insert a visible strip into the already-published surface.
            for (unsigned j = 0; j < 12; ++j) {
                Vector2 p = { tick * .125f - j, .3f * j };
                CHECK(fabsf(boat_wake_objects_query(&w, p).height
                          - boat_wake_objects_query(&before, p).height)
                    < 1e-6f);
            }
        }
    }
    float minimum = 3, change = 0;
    for (unsigned i = 0; i < 5; ++i) {
        minimum = fminf(minimum, w.objects[i].amplitude);
        if (i)
            change = fmaxf(change, fabsf(w.objects[i].amplitude - w.objects[i - 1].amplitude));
    }
    CHECK(w.emitted == 5 && minimum > 1.5f && change < .18f);
    uint64_t emitted = w.emitted;
    for (unsigned j = 0; j < 60; ++j) {
        boat_wake_objects_advance(&w);
        CHECK(boat_wake_objects_track(&w, 0, (Vector2) { 37.5f + j * .125f, 0 }, 15, 0));
    }
    CHECK(w.emitted == emitted); // Smoothing must never emit a wake from a dry boat.

    // The denser straight-running windows still partition one full-height crest.
    w = seed();
    w.objects[1] = w.objects[0];
    w.objects[1].position.x += 15;
    w.objects[1].born += BOAT_OBJECT_EMIT_TICKS;
    w.tick = w.objects[0].born + 240;
    float ripple = 0;
    for (unsigned i = 0; i <= 100; ++i) {
        float x = i * .15f;
        float age = 2 - x * w.objects[0].inverse_speed;
        float radius = .9f + 2.4f * age;
        float h
            = boat_wake_objects_query(&w, (Vector2) { x, sqrtf(radius * radius - .25f) }).height;
        ripple = fmaxf(ripple, fabsf(h - w.objects[0].amplitude));
    }
    CHECK(ripple < .00001f);
    float gradient_error = 0, time_error = 0;
    for (unsigned phase = 0; phase < 2; ++phase) {
        w = seed();
        w.tick = w.objects[0].born + (phase ? 1590 : 30);
        for (unsigned i = 0; i < 30; ++i) {
            Vector2 p = { -4 + i * .27f, (phase ? 32 : 1) + i * .05f };
            float eps = .002f;
            BoatWakeObjectSample q = boat_wake_objects_query(&w, p);
            float gx = (boat_wake_objects_query(&w, (Vector2) { p.x + eps, p.y }).height
                           - boat_wake_objects_query(&w, (Vector2) { p.x - eps, p.y }).height)
                / (2 * eps);
            gradient_error = fmaxf(gradient_error, fabsf(gx - q.gradient.x));
            BoatWakeObjects before = w, after = w;
            --before.tick;
            ++after.tick;
            float vt = (boat_wake_objects_query(&after, p).height
                           - boat_wake_objects_query(&before, p).height)
                * 60;
            time_error = fmaxf(time_error, fabsf(vt - q.vertical_velocity));
        }
    }
    CHECK(gradient_error < .003f && time_error < .02f);
    printf("continuity: brief wetness dips retain %.4gm, adjacent strength delta %.4gm, "
           "straight crest ripple %.4gm, growth/fade gradient error %.4g, time error %.4g\n",
        minimum, change, ripple, gradient_error, time_error);
    return 0;
}

static int stress(void)
{
    BoatWakeObjects w;
    CHECK(boat_wake_objects_init(&w, BOAT_OBJECT_LIFE_TICKS, 1.8f));
    float maximum = 0;
    for (unsigned tick = 0; tick < 120 * 180; ++tick) {
        boat_wake_objects_advance(&w);
        float t = tick / 120.0f;
        for (unsigned boat = 0; boat < 4; ++boat) {
            /* Widely separated world origins and repeated tight turns. */
            Vector2 p = { boat * 100000.0f + 15 * cosf(t), 15 * sinf(t) };
            CHECK(boat_wake_objects_track(&w, boat, p, 15, 1));
            float h = boat_wake_objects_query(&w, p).height;
            CHECK(isfinite(h));
            maximum = fmaxf(maximum, fabsf(h));
        }
        CHECK(w.live <= BOAT_OBJECT_CAPACITY);
    }
    CHECK(w.peak_live == BOAT_OBJECT_CAPACITY && w.expired > 600);
    printf("four-boat 180s loop stress: peak %u/%d objects, %llu expired, %zu bytes, local peak "
           "%.4gm\n",
        w.peak_live, BOAT_OBJECT_CAPACITY, (unsigned long long)w.expired, sizeof(w), maximum);
    return 0;
}

static int jumps(void)
{
    BoatWakeObjects world = seed(), flat;
    CHECK(boat_wake_objects_init(&flat, BOAT_OBJECT_LIFE_TICKS, 1.8f));
    BoatSession receiver;
    physics_session(&receiver, BOAT_LOCATION_BASIN);
    receiver.boat.position = (Vector2) { 0, 14 };
    receiver.boat.velocity = (Vector2) { 0, -16 };
    receiver.boat.throttle = receiver.boat.engine_response = .7f;
    receiver.boat.planing = 1;
    receiver.world.water.objects = &world;
    BoatSession calm = receiver;
    calm.world.water.objects = &flat;
    float height = 0, lift = 0;
    unsigned dry = 0;
    for (unsigned t = 0; t < 600; ++t) {
        boat_wake_objects_advance(&world);
        boat_wake_objects_advance(&flat);
        boat_session_step(&receiver, (BoatInput) { 0 });
        boat_session_step(&calm, (BoatInput) { 0 });
        height = fmaxf(height, receiver.boat.height - calm.boat.height);
        lift = fmaxf(lift, receiver.boat.vertical_velocity);
        dry += boat_wake_stern(&receiver.boat, &receiver.world, boat_session_time(&receiver)).wet
            == 0;
        CHECK(isfinite(receiver.boat.height) && fabsf(receiver.boat.height) < 8);
    }
    printf("real hull crossing: height above calm %.4gm, launch %.4gm/s, dry stern %u ticks\n",
        height, lift, dry);
    CHECK(height > .6f && lift > 2 && dry > 10);
    for (unsigned t = 0; t < 2400; ++t) {
        boat_wake_objects_advance(&world);
        float neutral
            = Clamp(-receiver.boat.throttle / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1);
        boat_session_step(&receiver, (BoatInput) { 0, neutral, 0, 0 });
        CHECK(isfinite(receiver.boat.height) && fabsf(receiver.boat.height) < 8);
    }
    CHECK(world.live == 0 && fabsf(receiver.boat.vertical_velocity) < .2f);
    return 0;
}

static float diagonal_jump(bool crossing)
{
    BoatWakeObjects world = seed(), flat;
    CHECK(boat_wake_objects_init(&flat, BOAT_OBJECT_LIFE_TICKS, 1.8f));
    if (crossing) {
        world.objects[BOAT_OBJECT_PER_BOAT] = world.objects[0];
        world.objects[BOAT_OBJECT_PER_BOAT].direction = (Vector2) { 0, 1 };
        world.live = 2;
    }
    BoatSession boat;
    physics_session(&boat, BOAT_LOCATION_BASIN);
    boat.boat.position = (Vector2) { 12, 12 };
    boat.boat.heading = -PI / 4;
    boat.boat.velocity = (Vector2) { -11.3137f, -11.3137f };
    boat.boat.throttle = boat.boat.engine_response = .7f;
    boat.boat.planing = 1;
    boat.world.water.objects = &world;
    BoatSession calm = boat;
    calm.world.water.objects = &flat;
    float peak = 0;
    for (unsigned tick = 0; tick < 720; ++tick) {
        boat_wake_objects_advance(&world);
        boat_wake_objects_advance(&flat);
        boat_session_step(&boat, (BoatInput) { 0 });
        boat_session_step(&calm, (BoatInput) { 0 });
        CHECK(isfinite(boat.boat.height) && fabsf(boat.boat.height) < 40);
        peak = fmaxf(peak, boat.boat.height - calm.boat.height);
    }
    return peak;
}

static int local_fleet(void)
{
    BoatWakeObjects world;
    CHECK(boat_wake_objects_init(&world, BOAT_OBJECT_LIFE_TICKS, 1.8f));
    BoatSession fleet[4];
    unsigned contacts = 0, flights = 0;
    for (unsigned i = 0; i < 4; ++i) {
        physics_session(&fleet[i], BOAT_LOCATION_BASIN);
        fleet[i].boat.position = (Vector2) { (i % 2 ? 1 : -1) * 12, (i / 2 ? 1 : -1) * 12 };
        fleet[i].boat.heading = i * PI / 2;
        fleet[i].world.water.objects = &world;
    }
    // Guarantee an attainable head-on encounter before the shared-wake loop opens out.
    for (unsigned i = 0; i < 2; ++i) {
        fleet[i].boat.position = (Vector2) { i ? 8 : -8, 0 };
        fleet[i].boat.heading = i ? -PI / 2 : PI / 2;
        fleet[i].boat.velocity = (Vector2) { i ? -8 : 8, 0 };
    }
    for (unsigned tick = 0; tick < 4800; ++tick) {
        boat_wake_objects_advance(&world);
        for (unsigned i = 0; i < 4; ++i) {
            Boat *boat = &fleet[i].boat;
            bool airborne = boat->motion_state == BOAT_AIRBORNE;
            boat_session_step(&fleet[i],
                (BoatInput) { tick < 180 ? 0 : (i % 2 ? -1 : 1) * .7f, tick < 240 ? 1 : 0, 0, 0 });
            flights += !airborne && boat->motion_state == BOAT_AIRBORNE;
            CHECK(isfinite(boat->height) && isfinite(boat->heading));
            BoatWakeStern stern
                = boat_wake_stern(boat, &fleet[i].world, boat_session_time(&fleet[i]));
            CHECK(boat_wake_objects_track(&world, i, (Vector2) { (float)stern.x, (float)stern.z },
                (float)hypot(stern.vx, stern.vz), (float)stern.wet));
        }
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = i + 1; j < 4; ++j)
                contacts
                    += boat_collide_pair(&fleet[i].boat, 1 + .2f * i, &fleet[j].boat, 1 + .2f * j);
        CHECK(world.live <= BOAT_OBJECT_CAPACITY);
    }
    CHECK(world.emitted > 4 && world.peak_live > 4 && contacts > 0);
    printf("local four-boat play: emitted %llu, peak live %u, contacts %u, flights %u\n",
        (unsigned long long)world.emitted, world.peak_live, contacts, flights);
    return 0;
}

// The full grid, or with SAMPLE only the fastest approach to the youngest one and four crests
// from every angle (flights, upright landings and capsizes).
static unsigned gameplay_envelope(float crest_height, bool sample)
{
    unsigned cases = 0, flights = 0, landings = 0, capsizes = 0;
    float peak = 0, peak_lift = 0;
    const float speeds[] = { 6, 12, 18 }, angles[] = { 0, PI / 4, PI / 2 };
    const unsigned ages[] = { 120, 480, 960 };
    for (unsigned sources = 1; sources <= 4; sources += sample ? 3 : 1)
        for (unsigned speed = sample ? 2 : 0; speed < 3; ++speed)
            for (unsigned angle = 0; angle < 3; ++angle)
                for (unsigned age = 0; age < (sample ? 1 : 3); ++age) {
                    BoatWakeObjects world = seed();
                    world.crest_height = crest_height;
                    world.objects[0].amplitude *= crest_height / 1.8f;
                    world.tick = world.objects[0].born + ages[age];
                    for (unsigned source = 1; source < sources; ++source) {
                        world.objects[source * BOAT_OBJECT_PER_BOAT] = world.objects[0];
                        if (source % 2)
                            world.objects[source * BOAT_OBJECT_PER_BOAT].direction
                                = (Vector2) { 0, 1 };
                    }
                    world.live = sources;
                    BoatSession boat;
                    physics_session(&boat, BOAT_LOCATION_BASIN);
                    float radius = .9f + 2.4f * ages[age] * BOAT_STEP + 8;
                    boat.boat.position
                        = (Vector2) { sinf(angles[angle]) * radius, cosf(angles[angle]) * radius };
                    boat.boat.heading = -angles[angle];
                    boat.boat.velocity = (Vector2) { -speeds[speed] * sinf(angles[angle]),
                        -speeds[speed] * cosf(angles[angle]) };
                    boat.world.water.objects = &world;
                    bool airborne = false;
                    for (unsigned tick = 0; tick < 4200; ++tick) {
                        boat_wake_objects_advance(&world);
                        boat_session_step(&boat, (BoatInput) { 0 });
                        CHECK(isfinite(boat.boat.height) && isfinite(boat.boat.pitch)
                            && isfinite(boat.boat.roll));
                        airborne |= boat.boat.motion_state == BOAT_AIRBORNE;
                        peak = fmaxf(peak, boat.boat.height);
                        peak_lift = fmaxf(peak_lift, boat.boat.vertical_velocity);
                        // Calm water after the last crest expires: a boat at rest stays so.
                        if (world.live == 0 && boat.boat.motion_state != BOAT_AIRBORNE
                            && fabsf(boat.boat.vertical_velocity) < .01f
                            && fabsf(boat.boat.pitch_velocity) < .01f
                            && fabsf(boat.boat.roll_velocity) < .01f)
                            break;
                    }
                    float up = cosf(boat.boat.pitch) * cosf(boat.boat.roll);
                    CHECK(world.live == 0);
                    CHECK(boat.boat.motion_state == BOAT_CAPSIZED
                        || (boat.boat.motion_state == BOAT_WATERBORNE && up > .5f));
                    CHECK(fabsf(boat.boat.vertical_velocity) < .2f);
                    flights += airborne;
                    landings += boat.boat.landings > 0;
                    capsizes += boat.boat.motion_state == BOAT_CAPSIZED;
                    ++cases;
                }
    CHECK(flights > 0 && landings > 0 && capsizes > 0);
    printf(
        "gameplay envelope (%.2gm): %u cases, %u flights, %u landed, %u capsized; peak %.4gm, lift "
        "%.4gm/s\n",
        crest_height, cases, flights, landings, capsizes, peak, peak_lift);
    return capsizes;
}

static int air_roll_control(void)
{
    BoatWakeObjects water;
    CHECK(boat_wake_objects_init(&water, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
    BoatSession left, right, recover;
    physics_session(&right, BOAT_LOCATION_BASIN);
    right.world.water.objects = &water;
    right.boat.height = 200; // Isolate several seconds of dry controls from landing forces.
    right.boat.wheel = 1;
    right.boat.velocity = (Vector2) { 3, -7 };
    left = right;
    left.boat.wheel = -1;
    recover = right;
    recover.boat.wheel = 0;
    recover.boat.roll = 70 * DEG2RAD;
    recover.boat.roll_velocity = 2;
    BoatSession recover_held = recover;
    recover_held.boat.wheel = 1;
    for (unsigned tick = 0; tick < 240; ++tick) {
        boat_session_step(&recover_held, (BoatInput) { 1, 0, 0, 0 });
        boat_session_step(&right, (BoatInput) { 1, 0, 0, 0 });
        boat_session_step(&left, (BoatInput) { -1, 0, 0, 0 });
        boat_session_step(&recover, (BoatInput) { 0 });
    }
    CHECK(right.boat.motion_state == BOAT_AIRBORNE && left.boat.motion_state == BOAT_AIRBORNE);
    CHECK(fabsf(right.boat.roll) < .0001f && fabsf(left.boat.roll) < .0001f);
    CHECK(right.boat.yaw_velocity > 0 && left.boat.yaw_velocity < 0);
    CHECK(fabsf(recover_held.boat.roll - recover.boat.roll) < .0001f);
    CHECK(fabsf(recover_held.boat.roll_velocity - recover.boat.roll_velocity) < .0001f);
    CHECK(fabsf(right.boat.roll + left.boat.roll) < .0001f);
    CHECK(fabsf(recover.boat.roll) < 10 * DEG2RAD && fabsf(recover.boat.roll_velocity) < .2f);
    CHECK(right.boat.velocity.x == 3 && right.boat.velocity.y == -7);
    CHECK(fabsf(right.boat.height - left.boat.height) < .0001f);
    for (unsigned tick = 0; tick < 360; ++tick)
        boat_session_step(&right, (BoatInput) { 0 });
    CHECK(fabsf(right.boat.roll_velocity) < .3f);

    BoatSession inverted;
    physics_session(&inverted, BOAT_LOCATION_BASIN);
    inverted.world.water.objects = &water;
    inverted.boat.roll = PI;
    for (unsigned tick = 0; tick < 120; ++tick)
        boat_session_step(&inverted, (BoatInput) { 0 });
    CHECK(inverted.boat.motion_state == BOAT_CAPSIZED);
    puts("arcade air control: held turns stay upright; automatic recovery, yaw, momentum and "
         "capsize pass");
    return 0;
}

static BoatWakeObjects curved_world(void)
{
    BoatWakeObjects world;
    CHECK(boat_wake_objects_init(&world, BOAT_OBJECT_LIFE_TICKS, 1.8f));
    for (unsigned tick = 0; tick <= 960; ++tick) {
        if (tick)
            boat_wake_objects_advance(&world);
        float angle = tick * BOAT_STEP * 1.2f;
        CHECK(boat_wake_objects_track(
            &world, 0, (Vector2) { 10 * cosf(angle), 10 * sinf(angle) }, 12, 1));
    }
    unsigned measured = 0;
    for (unsigned i = 0; i < BOAT_OBJECT_PER_BOAT; ++i) {
        BoatWakeObject object = world.objects[i];
        if (!object.amplitude || world.tick - object.born < 120)
            continue;
        BoatWakeObjects isolated = world;
        memset(isolated.objects, 0, sizeof(isolated.objects));
        isolated.objects[0] = object;
        Vector2 normal = { -object.direction.y, object.direction.x };
        float age = (world.tick - object.born) * BOAT_STEP;
        float r0 = .9f + 2.4f * age, r1 = r0 + 2.4f;
        float v0 = sqrtf(r0 * r0 - .25f), v1 = sqrtf(r1 * r1 - .25f);
        Vector2 old = Vector2Add(object.position, Vector2Scale(normal, v0));
        Vector2 next = Vector2Add(object.position, Vector2Scale(normal, v1));
        CHECK(Vector2Distance(old, next) > 2.4f && Vector2Distance(old, next) < 2.44f);
        isolated.tick += 120;
        CHECK(boat_wake_objects_query(&isolated, next).height
            > boat_wake_objects_query(&isolated, old).height * 2);
        ++measured;
    }
    CHECK(measured >= 10);
    printf("tight-turn stationary camera: %u individual crests travel 2.40..2.44m per second\n",
        measured);
    return world;
}

static void curved_captures(BoatGpu *gpu, bool visible)
{
    BoatWakeObjects world = curved_world();
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat.boat.position = (Vector2) { 0 };
    boat.world.water.objects = &world;
    BoatCamera camera = { .distance = 40, .height = 32, .fixed_fov = true };
    for (unsigned frame = 0; frame <= 120; ++frame) {
        if (frame) {
            boat_wake_objects_advance(&world);
            boat_wake_objects_advance(&world);
        }
        boat_session_set_tick(&boat, world.tick);
        boat_gpu_draw(gpu, SESSION_VIEW(&boat), &boat.presentation, &boat.world, camera);
        CHECK(!gpu->failed);
        if (frame == 0 || frame == 60 || frame == 120) {
            Image image = LoadImageFromTexture(gpu->target.texture);
            ImageFlipVertical(&image);
            CHECK(ExportImage(image, TextFormat("build/wake-curve-%u-gpu.png", frame)));
            UnloadImage(image);
        }
        if (visible) {
            BeginDrawing();
            ClearBackground(BLACK);
            DrawTexturePro(gpu->target.texture, (Rectangle) { 0, 0, 320, -200 },
                (Rectangle) { 0, 0, 960, 600 }, (Vector2) { 0 }, 0, WHITE);
            DrawText("TIGHT TURN / STATIONARY CAMERA", 12, 12, 20, WHITE);
            EndDrawing();
        }
    }
}

static int compare_time(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void shader_pool_checks(
    const BoatWakeObjects *w, float probe_z, const BoatWakeObjects *traffic)
{
    const char *paths[]
        = { "assets/shaders/waterfront_water.vs", "assets/shaders/waterfront_water.fs" };
    const char *vertex = "#version 330\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
                         "gl_Position=vec4(2*p-1,0,1);}";
    GLuint tex, fb, vao, records_texture;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, NULL);
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    float records[BOAT_OBJECT_CAPACITY * 16] = { 0 };
    float *parameters = records + BOAT_OBJECT_CAPACITY * 8;
    int players = boat_wake_objects_pack(w, records, parameters);
    int count = players;
    if (traffic)
        count += boat_wake_objects_pack(traffic, records + players * 4, parameters + players * 4);
    glGenTextures(1, &records_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, records_texture);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA32F, BOAT_OBJECT_CAPACITY * 2, 2, 0, GL_RGBA, GL_FLOAT, records);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glActiveTexture(GL_TEXTURE0);
    glViewport(0, 0, 1, 1);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    for (unsigned stage = 0; stage < 2; ++stage) {
        char *source = LoadFileText(paths[stage]);
        CHECK(source);
        char *start = strstr(source, "// BEGIN OBJECT RECIPE"),
             *end = strstr(source, "// BEGIN NATIVE WAKE FIELD");
        CHECK(start && end && end > start);
        size_t capacity = (size_t)(end - start) + 500;
        char *code = malloc(capacity);
        CHECK(code);
        int written = snprintf(code, capacity,
            "#version 330\nout vec4 color; uniform vec2 probe; uniform int playerWakeCount;"
            "uniform vec4 playerWakeOrigins[64],playerWakeParameters[64];\n%.*s\n"
            "void main(){vec3 wake=playerWake(probe);color=vec4(wake,%s);}",
            (int)(end - start), start, stage ? "objectCrestFoam" : "1.0");
        CHECK(written >= 0 && (size_t)written < capacity);
        Shader shader = LoadShaderFromMemory(vertex, code);
        CHECK(shader.id && shader.id != rlGetShaderIdDefault());
        free(code);
        UnloadFileText(source);
        glUseProgram(shader.id);
        glUniform1i(GetShaderLocation(shader, "playerWakeCount"), count);
        glUniform1f(GetShaderLocation(shader, "objectWakeLife"), w->life_ticks / 120.0f);
        glUniform1i(GetShaderLocation(shader, "objectWakePlayerCount"), players);
        glUniform1f(GetShaderLocation(shader, "objectWakeTrafficLife"),
            traffic ? traffic->life_ticks / 120.0f : 0);
        glUniform1i(GetShaderLocation(shader, "objectWakeData"), 1);
        glUniform1i(GetShaderLocation(shader, "objectWakeEnabled"), 1);
        double error = 0, scale = 0, component_error[3] = { 0 }, component_scale[3] = { 0 };
        for (int z = -18; z <= 18; ++z)
            for (int x = -15; x <= 15; x += 2) {
                Vector2 p = { x + .173f, z + .391f + probe_z };
                BoatWakeObjectSample q = boat_wake_objects_query(w, p);
                if (traffic) {
                    BoatWakeObjectSample extra = boat_wake_objects_query(traffic, p);
                    q.height += extra.height;
                    q.gradient = Vector2Add(q.gradient, extra.gradient);
                    q.crest_foam += extra.crest_foam;
                }
                glUniform2f(GetShaderLocation(shader, "probe"), -p.x, p.y);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                float result[4];
                glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, result);
                if (stage)
                    CHECK(fabsf(result[3] - q.crest_foam) < .00003f);
                float ref[] = { q.height, -q.gradient.x, q.gradient.y };
                for (unsigned j = 0; j < 3; ++j) {
                    error += pow(result[j] - ref[j], 2);
                    scale += ref[j] * ref[j];
                    component_error[j] += pow(result[j] - ref[j], 2);
                    component_scale[j] += ref[j] * ref[j];
                }
            }
        printf("object production %s recipe (%d objects) relative L2 %.8g\n", stage ? "FS" : "VS",
            count, sqrt(error / scale));
        printf("height %.8g, gx %.8g, gz %.8g\n", sqrt(component_error[0] / component_scale[0]),
            sqrt(component_error[1] / component_scale[1]),
            sqrt(component_error[2] / component_scale[2]));
        CHECK(sqrt(error / scale) < .0001);
        UnloadShader(shader);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glBindVertexArray(0);
    glDeleteVertexArrays(1, &vao);
    glDeleteFramebuffers(1, &fb);
    glDeleteTextures(1, &tex);
    glDeleteTextures(1, &records_texture);
}

static void shader_checks(const BoatWakeObjects *w, float probe_z)
{
    shader_pool_checks(w, probe_z, NULL);
}

static void runtime(bool graphics, bool visible)
{
    BoatWakeObjects w;
    CHECK(boat_wake_objects_init(&w, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat.presentation_enabled = graphics; // Only the GPU review draws the flag and particles.
    boat.world.water.objects = &w;
    BoatGpu gpu = { 0 };
    double cold_draw_ms = 0;
    if (graphics) {
        SetConfigFlags(visible ? 0 : FLAG_WINDOW_HIDDEN);
        InitWindow(visible ? 960 : 320, visible ? 600 : 200, "Arcade wake review");
        if (visible)
            SetTargetFPS(60);
        CHECK(IsWindowReady() && boat_gpu_init(&gpu, false));
        double cold_begin = GetTime();
        boat_gpu_draw(
            &gpu, SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT);
        glFinish();
        cold_draw_ms = (GetTime() - cold_begin) * 1000;
    }
    double physics[120], render[120], frames[120];
    unsigned count = 0, dry = 0;
    float peak = 0;
    for (unsigned frame = 0; frame < 1200; ++frame) {
        if (visible && WindowShouldClose())
            break;
        double begin = graphics ? GetTime() : (double)clock() / CLOCKS_PER_SEC;
        for (unsigned j = 0; j < 2; ++j) {
            boat_wake_objects_advance(&w);
            float wheel = frame > 210 ? 1 : 0;
            boat_session_step(&boat,
                (BoatInput) { Clamp((wheel - boat.boat.wheel) / BOAT_STEP, -1, 1), 1, 0, 0 });
            track(&w, &boat);
            CHECK(isfinite(boat.boat.height) && fabsf(boat.boat.height) < 8);
            dry += boat_wake_stern(&boat.boat, &boat.world, boat_session_time(&boat)).wet == 0;
            peak = fmaxf(peak, boat.boat.height);
        }
        double stepped = graphics ? GetTime() : (double)clock() / CLOCKS_PER_SEC;
        if (graphics) {
            boat_gpu_draw(
                &gpu, SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT);
            CHECK(!gpu.failed);
            glFinish();
            if (visible) {
                BeginDrawing();
                ClearBackground(BLACK);
                DrawTexturePro(gpu.target.texture, (Rectangle) { 0, 0, 320, -200 },
                    (Rectangle) { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() },
                    (Vector2) { 0 }, 0, WHITE);
                DrawText("WAKE OBJECTS / SCRIPTED JUMP REVIEW", 12, 12, 20, WHITE);
                EndDrawing();
            }
        }
        double ended = graphics ? GetTime() : (double)clock() / CLOCKS_PER_SEC;
        if (frame >= 1080) {
            physics[count] = 1000 * (stepped - begin);
            render[count] = 1000 * (ended - stepped);
            frames[count++] = 1000 * (ended - begin);
        }
    }
    if (count != 120) {
        printf("Desktop review closed after %llu physics ticks.\n", (unsigned long long)w.tick);
        if (graphics) {
            boat_gpu_destroy(&gpu);
            CloseWindow();
        }
        return;
    }
    qsort(physics, 120, sizeof(double), compare_time);
    qsort(render, 120, sizeof(double), compare_time);
    qsort(frames, 120, sizeof(double), compare_time);
    printf("actual self-wake loops: peak hull %.4gm, dry stern %u ticks, %u live, %zu bytes, "
           "state %d, landings %u\n",
        peak, dry, w.live, sizeof(w), boat.boat.motion_state, boat.boat.landings);
    if (graphics)
        printf("startup first draw (outside warmed sample): %.4f ms\n", cold_draw_ms);
    printf("120 complete frames: physics median %.4f/p95 %.4f ms, renderer median %.4f ms, "
           "total median %.4f/p95 %.4f/max %.4f ms\n",
        physics[60], physics[113], render[60], frames[60], frames[113], frames[119]);
    CHECK(w.live > 0 && w.peak_live <= BOAT_OBJECT_CAPACITY);
    if (graphics) {
        float expected[BOAT_OBJECT_CAPACITY * 8] = { 0 }, actual[BOAT_OBJECT_CAPACITY * 8];
        boat_wake_objects_pack(&w, expected, expected + BOAT_OBJECT_CAPACITY * 4);
        CHECK(gpu.scene.object_wakes.id != 0);
        CHECK(gpu.scene.object_wakes.width == BOAT_OBJECT_CAPACITY);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gpu.scene.object_wakes.id);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, actual);
        CHECK(memcmp(expected, actual, sizeof(expected)) == 0);
        puts("production object-record upload matches current CPU world");
        BoatWakeObjects scenery = seed();
        boat.world.water.traffic_objects = &scenery;
        boat_gpu_draw(
            &gpu, SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT);
        CHECK(!gpu.failed && gpu.scene.object_wakes.width == BOAT_OBJECT_CAPACITY * 2);
        float paired[BOAT_OBJECT_CAPACITY * 16] = { 0 }, uploaded[BOAT_OBJECT_CAPACITY * 16];
        float *parameters = paired + BOAT_OBJECT_CAPACITY * 8;
        int players = boat_wake_objects_pack(&w, paired, parameters);
        boat_wake_objects_pack(&scenery, paired + players * 4, parameters + players * 4);
        glBindTexture(GL_TEXTURE_2D, gpu.scene.object_wakes.id);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, uploaded);
        CHECK(memcmp(paired, uploaded, sizeof(paired)) == 0);
        boat.world.water.traffic_objects = NULL;
        boat_gpu_draw(
            &gpu, SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT);
        CHECK(!gpu.failed && gpu.scene.object_wakes.width == BOAT_OBJECT_CAPACITY);
        puts("independent scenery upload and texture resize round trip match CPU pools");
        Image image = LoadImageFromTexture(gpu.target.texture);
        ImageFlipVertical(&image);
        CHECK(ExportImage(image, "build/wake-objects-gpu.png"));
        UnloadImage(image);
        BoatWakeObjects probe = seed();
        BoatWakeObject first = probe.objects[0];
        shader_checks(&probe, 0);
        probe.tick = first.born + 30;
        shader_checks(&probe, 0); // Birth/growth includes spatial envelope derivatives.
        probe.tick = first.born + 1590;
        shader_checks(&probe, 30); // Same derivatives while a long wake fades.
        probe.tick = first.born + 120;
        probe.objects[BOAT_OBJECT_PER_BOAT] = probe.objects[0];
        probe.objects[BOAT_OBJECT_PER_BOAT].direction = (Vector2) { .6f, .8f };
        probe.objects[0].amplitude = 0;
        shader_checks(&probe, 0);
        probe.objects[0] = first;
        shader_checks(&probe, 0);
        BoatWakeObject rotated = probe.objects[BOAT_OBJECT_PER_BOAT];
        for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
            probe.objects[i] = first;
            probe.objects[i].position = (Vector2) { 1000 + i * 10, 1000 };
        }
        probe.objects[BOAT_OBJECT_CAPACITY - 2] = first;
        probe.objects[BOAT_OBJECT_CAPACITY - 1] = rotated;
        shader_checks(&probe, 0); // Full record table, with local pieces in its final slots.
        BoatWakeObjects traffic = seed();
        traffic.life_ticks = 960;
        shader_pool_checks(&probe, 0, &traffic); // Independent source in records beyond slot 127.
        curved_captures(&gpu, visible);
        boat_gpu_destroy(&gpu);
        CloseWindow();
    }
}

static int diagonal_jumps(void)
{
    float single = diagonal_jump(false), crossed = diagonal_jump(true);
    printf("same diagonal approach: one wake %.4gm, crossing wakes %.4gm above calm\n", single,
        crossed);
    CHECK(crossed > single * 1.2f);
    return 0;
}

static int gameplay_envelope_sample(void)
{
    (void)gameplay_envelope(1.8f, true);
    return 0;
}

static int gameplay_envelopes(void)
{
    unsigned tall_capsizes = gameplay_envelope(1.8f, false);
    unsigned default_capsizes = gameplay_envelope(BOAT_OBJECT_DEFAULT_HEIGHT, false);
    CHECK(tall_capsizes < 43 && default_capsizes < tall_capsizes);
    return 0;
}

static int curved_crests(void)
{
    (void)curved_world();
    return 0;
}

static int cpu_runtime(void)
{
    runtime(false, false);
    return 0;
}

static const TestCase cases[] = {
    TEST_CASE(contracts),
    TEST_CASE(continuity),
    TEST_CASE(stress),
    TEST_CASE(jumps),
    TEST_CASE(diagonal_jumps),
    TEST_CASE(air_roll_control),
    TEST_CASE(gameplay_envelope_sample),
    SLOW_CASE(gameplay_envelopes),
    SLOW_CASE(local_fleet),
    TEST_CASE(curved_crests),
    TEST_CASE(cpu_runtime),
};

// --gpu (hidden window) and --desktop (visible review) run only the GPU runtime; the CPU
// cases are separate CTest entries.
int main(int argc, char **argv)
{
    SetTraceLogLevel(LOG_WARNING);
    bool visible = argc == 2 && strcmp(argv[1], "--desktop") == 0;
    int result = 0;
    if (visible || (argc == 2 && strcmp(argv[1], "--gpu") == 0))
        runtime(true, visible);
    else
        result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    test_worlds_close();
    return result;
}
