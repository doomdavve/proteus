#include "boat_map.h"
#include "boat_test_session.h"
#include "boat_view.h"
#include "waterfront_harbor.h"
#include "waterfront_traffic.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "Traffic check failed at %d: %s\n", __LINE__, #test);                  \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int check_vertical_contacts(void)
{
    BoatWater water = { 0 };
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        WaterfrontVesselMotion ship = waterfront_traffic_motion(i, 40);
        Vector2 p = { -ship.position.x, ship.position.z };
        CHECK(waterfront_traffic_contact_height(p, 0, 2, 1.6f, .75f, -.4f, 2, water, 40, NULL));
        CHECK(!waterfront_traffic_contact_height(p, 0, 2, 1.6f, .75f, 30, 32, water, 40, NULL));
        CHECK(!waterfront_traffic_contact_height(p, 0, 2, 1.6f, .75f, -30, -28, water, 40, NULL));
    }
    const WaterfrontBerth *berths = waterfront_harbor_berths();
    for (int i = 0; i < WATERFRONT_HARBOR_BOATS; ++i) {
        Vector2 p = { -berths[i].position.x, berths[i].position.z };
        CHECK(waterfront_harbor_contact_height(p, .35f, -.4f, 2, water, 0, NULL));
        CHECK(!waterfront_harbor_contact_height(p, .35f, 30, 32, water, 0, NULL));
        BoatModelSpec spec = waterfront_harbor_spec(i);
        if (spec.mast_height > 5)
            CHECK(waterfront_harbor_contact_height(p, .35f, 4, 5, water, 0, NULL));
    }
    // Quay remains solid above a low pontoon's clearance height.
    CHECK(waterfront_harbor_contact_height((Vector2) { 27.55f, 182 }, .35f, 3, 4, water, 0, NULL));
    CHECK(!waterfront_harbor_contact_height((Vector2) { 20, 182 }, .35f, 3, 4, water, 0, NULL));
    return 0;
}

static bool near(Vector3 a, Vector3 b, float tolerance)
{
    return Vector3Distance(a, b) < tolerance;
}

static bool footprints_overlap(
    Vector2 a, float ah, float al, float ab, Vector2 b, float bh, float bl, float bb)
{
    // Independent projection of the four physical rectangle vertices onto both frames.
    Vector2 points[2][4], axes[4];
    float headings[] = { ah, bh }, lengths[] = { al, bl }, beams[] = { ab, bb };
    Vector2 centers[] = { a, b };
    for (int hull = 0; hull < 2; ++hull) {
        Vector2 right = { cosf(headings[hull]), -sinf(headings[hull]) };
        Vector2 forward = { -sinf(headings[hull]), -cosf(headings[hull]) };
        axes[hull * 2] = right;
        axes[hull * 2 + 1] = forward;
        for (int corner = 0; corner < 4; ++corner)
            points[hull][corner] = Vector2Add(centers[hull],
                Vector2Add(Vector2Scale(right, (corner & 1 ? 1 : -1) * beams[hull] * .5f),
                    Vector2Scale(forward, (corner & 2 ? 1 : -1) * lengths[hull] * .5f)));
    }
    for (int i = 0; i < 4; ++i) {
        float low[2] = { INFINITY, INFINITY }, high[2] = { -INFINITY, -INFINITY };
        for (int hull = 0; hull < 2; ++hull)
            for (int corner = 0; corner < 4; ++corner) {
                float value = Vector2DotProduct(points[hull][corner], axes[i]);
                low[hull] = fminf(low[hull], value);
                high[hull] = fmaxf(high[hull], value);
            }
        if (high[0] <= low[1] || high[1] <= low[0])
            return false;
    }
    return true;
}

static bool hulls_clear(const Boat *boat, double time)
{
    // Player geometry is offset 0.2 m toward its 2.0 m bow / 1.6 m stern.
    Vector2 center = { -boat->position.x - .2f * sinf(boat->heading),
        boat->position.y - .2f * cosf(boat->heading) };
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        BoatModelSpec s = waterfront_traffic_spec(i);
        WaterfrontVesselMotion m = waterfront_traffic_motion(i, time);
        if (footprints_overlap(center, boat->heading, 3.6f, 1.5f,
                (Vector2) { m.position.x, m.position.z }, m.heading, s.length, s.beam))
            return false;
    }
    return boat_map_legacy_water(boat->position, .5f);
}

// Exercise the boating snapshot as well as the authored paths: both renderers must
// receive the same flock, and moving stimuli must not introduce frame-history dependence.
// Day-long sessions keep exact route periodicity, smooth motion, and contact/render agreement.
static int check_long_session(void)
{
    const double epochs[] = { 3600.3, 86400.3, 30 * 86400.3 };
    WaterfrontTrafficFrame frame;
    WaterfrontSceneFrame gulls = { 0 }, next = { 0 };
    Vector3 far = { 10000, 0, 10000 };
    for (int e = 0; e < 3; ++e) {
        double time = epochs[e];
        waterfront_traffic_evaluate(boat_water_gentle(), time, &frame);
        for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
            double period = waterfront_traffic_period(i);
            WaterfrontVesselMotion m = waterfront_traffic_motion(i, time);
            WaterfrontVesselMotion lap
                = waterfront_traffic_motion(i, time - floor(time / period) * period);
            CHECK(near(m.position, lap.position, .001f) && near(m.velocity, lap.velocity, .001f));
            Vector3 finite_difference
                = Vector3Scale(Vector3Subtract(waterfront_traffic_motion(i, time + .01).position,
                                   waterfront_traffic_motion(i, time - .01).position),
                    50);
            CHECK(near(m.velocity, finite_difference, .03f));
            // Rendered pose, moving contact, and route sample use one time evaluation.
            const WaterfrontVesselState *vessel = &frame.vessels[i];
            CHECK(!memcmp(&vessel->motion, &m, sizeof(m)));
            CHECK(
                vessel->pose.position.x == m.position.x && vessel->pose.position.z == m.position.z);
            WaterfrontTrafficContact contact;
            CHECK(waterfront_traffic_contact((Vector2) { -m.position.x, m.position.z }, m.heading,
                2, 1.6f, .75f, time, &contact));
            CHECK(contact.vessel == i);
        }
        for (int j = 0; j < frame.wake_count; ++j)
            CHECK(boat_map_legacy_water(
                (Vector2) { -frame.wakes[j].pose.position.x, frame.wakes[j].pose.position.z },
                .5f));
        waterfront_boating_gulls(time, far, (Vector3) { 0 }, &gulls);
        waterfront_boating_gulls(time + 1 / 120.0, far, (Vector3) { 0 }, &next);
        for (int i = 0; i < gulls.gull_count; ++i) {
            float step = Vector3Distance(gulls.gulls[i][0].position, next.gulls[i][0].position);
            // Fastest followers fly about 13 m/s; float time would freeze or jump at 30 days.
            CHECK(step > 1e-4f && step < .25f);
            CHECK(fabsf(gulls.gulls[i][1].rotation.x - next.gulls[i][1].rotation.x) < .2f);
        }
    }
    return 0;
}

static int check_gulls(void)
{
    WaterfrontSceneFrame previous = { 0 }, frame = { 0 }, sought = { 0 };
    Vector3 far = { 10000, 0, 10000 };
    waterfront_boating_gulls(0, far, (Vector3) { 0 }, &previous);
    for (int tick = 0; tick <= 2400; ++tick) {
        float time = tick / 20.0f;
        waterfront_boating_gulls(time, far, (Vector3) { 0 }, &frame);
        waterfront_boating_gulls(120 - time, far, (Vector3) { 0 }, &sought);
        waterfront_boating_gulls(time, far, (Vector3) { 0 }, &sought);
        CHECK(frame.gull_count == WATERFRONT_HARBOR_GULLS);
        for (int i = 0; i < frame.gull_count; ++i) {
            Vector3 p = frame.gulls[i][0].position;
            CHECK(isfinite(p.x) && isfinite(p.y) && isfinite(p.z) && p.y > 1);
            CHECK(Vector3Distance(p, previous.gulls[i][0].position) < 1.5f);
            for (int j = i / 8 * 8; j < i; ++j)
                CHECK(Vector3Distance(p, frame.gulls[j][0].position) > 2);
            for (int part = 0; part < WATERFRONT_GULL_PARTS; ++part) {
                Transform3D a = frame.gulls[i][part], b = sought.gulls[i][part];
                CHECK(near(a.position, b.position, 1e-6f) && near(a.rotation, b.rotation, 1e-6f));
                CHECK(near(a.position, p, 1e-6f));
                CHECK(isfinite(a.rotation.x) && isfinite(a.rotation.y) && isfinite(a.rotation.z));
            }
        }
        previous = frame;
    }
    // A player crosses the nearby flock at speed, testing the avoidance envelope's
    // entry/exit rather than only undisturbed flight.
    for (int tick = 0; tick <= 800; ++tick) {
        float time = tick / 40.0f;
        waterfront_boating_gulls(
            time, (Vector3) { -25 + time * 4, 0, 38 }, (Vector3) { 4, 0, 0 }, &frame);
        if (tick)
            for (int i = 0; i < frame.gull_count; ++i)
                CHECK(
                    Vector3Distance(frame.gulls[i][0].position, previous.gulls[i][0].position) < 1);
        previous = frame;
    }
    waterfront_boating_gulls(3, far, (Vector3) { 0 }, &frame);
    Vector3 stimulus = frame.gulls[0][0].position;
    stimulus.y = 0;
    waterfront_boating_gulls(3, stimulus, (Vector3) { 0 }, &sought);
    CHECK(sought.gulls[0][0].position.y > frame.gulls[0][0].position.y + 2);
    float idle_height = sought.gulls[0][0].position.y;
    waterfront_boating_gulls(3, stimulus, (Vector3) { 8, 0, 0 }, &sought);
    CHECK(sought.gulls[0][0].position.y > idle_height + 1);
    BoatSession boat;
    boat_session_init(&boat, boat_location(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
    boat_scene_frame(
        SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT, &frame);
    waterfront_boating_gulls(boat_session_time(&boat), frame.player.hull.position,
        (Vector3) { -boat.boat.velocity.x, 0, boat.boat.velocity.y }, &sought);
    CHECK(frame.gull_count == WATERFRONT_HARBOR_GULLS);
    CHECK(!memcmp(frame.gulls, sought.gulls, sizeof(frame.gulls)));
    boat_session_init(&boat, boat_location(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_scene_frame(
        SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT, &frame);
    CHECK(frame.gull_count == 0);
    puts("harbor_gulls=ok birds=32 continuity=120s seeks=ok player_stimuli=ok snapshot=ok");
    return 0;
}

int main(void)
{
    CHECK(check_vertical_contacts() == 0);
    if (check_gulls() || check_long_session())
        return 1;
    boat_map_legacy_destroy();
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        BoatModelSpec spec = waterfront_traffic_spec(i);
        const Mesh3D *mesh = waterfront_traffic_mesh(i);
        CHECK(mesh && mesh->triangle_count > 0 && mesh->triangle_count <= BOAT_MODEL_MAX_TRIANGLES);
        float period = waterfront_traffic_period(i);
        for (int tick = 0; tick <= (int)ceilf(period * 12); ++tick) {
            float time = tick / 12.0f;
            WaterfrontVesselMotion m = waterfront_traffic_motion(i, time);
            float speed = Vector3Length(m.velocity);
            CHECK(speed > .5f && speed < 6.2f && fabsf(m.yaw_rate) < .2f);
            Transform3D pose
                = { .position = m.position, .rotation = { 0, m.heading, 0 }, .scale = { 1, 1, 1 } };
            for (int corner = 0; corner < 4; ++corner) {
                Vector3 p = boat_model_point(pose,
                    (Vector3) { (corner & 1 ? 1 : -1) * (spec.beam * .5f + 1.5f), 0,
                        (corner & 2 ? 1 : -1) * (spec.length * .5f + 1.5f) });
                CHECK(boat_map_legacy_water((Vector2) { -p.x, p.z }, .5f));
                CHECK(!waterfront_harbor_contact((Vector2) { -p.x, p.z }, .5f, NULL));
            }
        }
    }
    boat_map_legacy_destroy();
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        float period = waterfront_traffic_period(i);
        WaterfrontVesselMotion start = waterfront_traffic_motion(i, 0);
        WaterfrontVesselMotion end = waterfront_traffic_motion(i, period);
        CHECK(near(start.position, end.position, .002f));
        CHECK(near(start.velocity, end.velocity, .002f));
        CHECK(near(waterfront_traffic_motion(i, -.001f).position,
            waterfront_traffic_motion(i, .001f).position, .02f));
        // Velocity and heading follow the actual curve rather than interpolated key rotations.
        for (int tick = 0; tick < (int)period * 4; ++tick) {
            float t = tick / 4.0f;
            WaterfrontVesselMotion m = waterfront_traffic_motion(i, t);
            Vector3 finite_difference
                = Vector3Scale(Vector3Subtract(waterfront_traffic_motion(i, t + .01f).position,
                                   waterfront_traffic_motion(i, t - .01f).position),
                    50);
            CHECK(near(m.velocity, finite_difference, .03f));
            Vector3 bow = Vector3Transform((Vector3) { 0, 0, -1 }, MatrixRotateY(m.heading));
            CHECK(Vector3DotProduct(bow, Vector3Normalize(m.velocity)) > .9999f);
        }
    }
    // The two river periods repeat together after 4,160 s; all four corridors stay separated.
    for (int tick = 0; tick <= 8320; ++tick) {
        float time = tick * .5f;
        for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i)
            for (int j = i + 1; j < WATERFRONT_TRAFFIC_VESSELS; ++j) {
                BoatModelSpec a = waterfront_traffic_spec(i), b = waterfront_traffic_spec(j);
                WaterfrontVesselMotion ma = waterfront_traffic_motion(i, time);
                WaterfrontVesselMotion mb = waterfront_traffic_motion(j, time);
                CHECK(!footprints_overlap((Vector2) { ma.position.x, ma.position.z }, ma.heading,
                    a.length + 1, a.beam + .5f, (Vector2) { mb.position.x, mb.position.z },
                    mb.heading, b.length + 1, b.beam + .5f));
            }
    }
    WaterfrontTrafficFrame flat, waves, sought;
    for (int t = 0; t <= 320; ++t) {
        waterfront_traffic_evaluate((BoatWater) { 0 }, (float)t, &flat);
        waterfront_traffic_evaluate(boat_water_gentle(), (float)t, &waves);
        waterfront_traffic_evaluate(boat_water_gentle(), 320 - (float)t, &sought);
        waterfront_traffic_evaluate(boat_water_gentle(), (float)t, &sought);
        CHECK(!memcmp(&sought, &waves, sizeof(waves)));
        CHECK(waves.wake_count == WATERFRONT_TRAFFIC_WAKES);
        for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
            CHECK(waves.vessels[i].id == 3000 + i);
            CHECK(fabsf(waves.vessels[i].pose.position.y) < .22f);
            CHECK(flat.vessels[i].pose.position.y == 0);
            CHECK(near(flat.vessels[i].motion.position, waves.vessels[i].motion.position, 1e-6f));
        }
        for (int j = 0; j < waves.wake_count; ++j) {
            const WaterfrontTrafficWake *wake = &waves.wakes[j];
            CHECK(isfinite(wake->pose.rotation.x) && isfinite(wake->pose.rotation.z));
            CHECK(wake->pose.scale.z > 0 && wake->pose.scale.z < 5);
            CHECK(boat_map_legacy_water(
                (Vector2) { -wake->pose.position.x, wake->pose.position.z }, .5f));
            CHECK(wake->color.a == 255);
        }
    }
    // A moving vessel catches a neutral boat; the player is separated and can power away.
    for (int i = 0; i < WATERFRONT_TRAFFIC_VESSELS; ++i) {
        WaterfrontVesselMotion ship = waterfront_traffic_motion(i, 60);
        BoatModelSpec spec = waterfront_traffic_spec(i);
        BoatSession boat;
        boat_session_init(&boat, boat_location(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
        boat_session_set_tick(&boat, 7200);
        boat.boat.heading = boat.presentation.camera_heading = ship.heading;
        Vector3 p = Vector3Add(
            ship.position, Vector3Scale(Vector3Normalize(ship.velocity), spec.length * .5f + 2.7f));
        boat.boat.position = (Vector2) { -p.x, p.z };
        Vector2 initial = boat.boat.position;
        for (int tick = 0; tick < 120 * 8; ++tick) {
            boat_session_step(&boat, (BoatInput) { 0 });
            CHECK(hulls_clear(&boat.boat, boat_session_time(&boat)));
            CHECK(Vector2Length(boat.boat.velocity) < 12 && fabsf(boat.boat.yaw_velocity) < 5);
        }
        CHECK(Vector2Distance(initial, boat.boat.position) > .5f);
        boat.boat.throttle = 1;
        for (int tick = 0; tick < 120 * 6; ++tick) {
            boat_session_step(&boat, (BoatInput) { 0 });
            CHECK(hulls_clear(&boat.boat, boat_session_time(&boat)));
        }
        // A deliberate beam-on crossing checks the full SAT hull and moving contact torque.
        ship = waterfront_traffic_motion(i, 40);
        Vector2 right = { cosf(ship.heading), sinf(ship.heading) };
        boat_session_init(&boat, boat_location(BOAT_LOCATION_RIVER), (BoatWater) { 0 });
        boat_session_set_tick(&boat, 4800);
        boat.boat.position = Vector2Add((Vector2) { -ship.position.x, ship.position.z },
            Vector2Scale(right, spec.beam * .5f + 3));
        boat.boat.heading = ship.heading - PI / 2;
        boat.presentation.camera_heading = boat.boat.heading;
        boat.boat.velocity
            = (Vector2) { sinf(boat.boat.heading) * 8, -cosf(boat.boat.heading) * 8 };
        for (int tick = 0; tick < 120 * 5; ++tick) {
            boat_session_step(&boat, (BoatInput) { 0 });
            CHECK(hulls_clear(&boat.boat, boat_session_time(&boat)));
            CHECK(Vector2Length(boat.boat.velocity) < 16 && fabsf(boat.boat.yaw_velocity) < 6);
        }
    }
    CHECK(waterfront_traffic_mesh(-1) == NULL);
    CHECK(waterfront_traffic_period(-1) == 0);
    boat_map_legacy_destroy();
    puts("harbor_traffic=ok routes=4 pairwise_schedule=4160s visual_wakes=64 moving_contacts=ok "
         "long_session=30d");
    return 0;
}
