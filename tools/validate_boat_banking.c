#include "boat_banking_fixture.h"
#include "boat_map.h"
#include "boat_test_cases.h"
#include "boat_test_session.h"
#include "boat_view.h"
#include "raymath.h"
#include "waterfront_harbor.h"
#include "waterfront_traffic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Banking check failed at line %d: %s\n", __LINE__, #condition);        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static Vector2 forward(float heading) { return (Vector2) { sinf(heading), -cosf(heading) }; }
static Vector2 right(float heading) { return (Vector2) { cosf(heading), sinf(heading) }; }

static float energy(const Boat *boat)
{
    // Normalized horizontal mass=1, yaw inertia=2.2; excludes imposed tow work.
    return .5f * Vector2LengthSqr(boat->velocity) + 1.1f * boat->yaw_velocity * boat->yaw_velocity;
}

static bool open_water(const Boat *boat)
{
    // Conservative circumscribed hull radius (including shore clearance). Check before
    // stepping as well: a contact solver pulling a boat back cannot disguise a wall hit.
    return fabsf(boat->position.x) + 3 < BOAT_BASIN_HALF_X
        && fabsf(boat->position.y) + 3 < BOAT_BASIN_HALF_Z;
}

static bool same_state(const BoatSession *x, const BoatSession *y)
{
    const Boat *a = &x->boat, *b = &y->boat;
    // Explicit physical/control state, never padding or a struct-byte hash.
    // Sessions here run without presentation, which never feeds physics; camera follow and
    // presentation determinism are covered by boat_world_isolation's frame hashes.
#define SAME(member) (a->member == b->member)
    return SAME(position.x) && SAME(position.y) && SAME(velocity.x) && SAME(velocity.y)
        && x->tick == y->tick && SAME(heading) && SAME(yaw_velocity) && SAME(planing)
        && SAME(height) && SAME(vertical_velocity) && SAME(pitch) && SAME(pitch_velocity)
        && SAME(roll) && SAME(roll_velocity) && SAME(turn_roll_load) && SAME(throttle)
        && SAME(engine_response) && SAME(wheel) && SAME(engine_angle) && SAME(wave_spray);
#undef SAME
}

// The --report CSV, or NULL.
static FILE *report;

// All rows describe the pre-tick state. Acceleration is measured over the following tick,
// projected on its PRE-tick right vector. This is a force probe only in calm, contact-free
// water: no waves, impacts or post-tick heading projection may contaminate this estimate.
// Spatial-wave rows use NAN for unmeasured acceleration/residual, never a fabricated zero.
static void row(FILE *report, const char *name, int tick, const BoatSession *session,
    float acceleration, float radius, float residual)
{
    if (!report)
        return;
    const Boat *before = &session->boat;
    float u = Vector2DotProduct(Vector2Subtract(before->velocity,
                                    boat_water_current(session->world.water, before->position)),
        forward(before->heading));
    fprintf(report,
        "%s,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
        name, tick, tick * (double)BOAT_STEP, u, before->wheel, before->engine_angle,
        before->planing, before->yaw_velocity, before->roll, before->roll_velocity, before->height,
        before->pitch, energy(before), acceleration, radius, residual,
        Vector2DotProduct(before->velocity, right(before->heading)), before->turn_roll_load);
}

// Independent finite-difference force measurement: production never reads this diagnostic.
static int check_filter(const Boat *before, const Boat *after, float measured_load)
{
    float u = Vector2DotProduct(before->velocity, forward(before->heading));
    float blend = Clamp((u - 1) / 5, 0, 1);
    blend = blend * blend * (3 - 2 * blend);
    float gain = .12f + (0.28f - .12f) * after->planing * blend;
    float expected = before->turn_roll_load
        + (gain * measured_load - before->turn_roll_load) * (1 - expf(-BOAT_STEP / 0.06f));
    CHECK(fabsf(after->turn_roll_load - expected) < .00002f);
    return 0;
}

static int validate_core(void)
{
    BoatSession boat;
    physics_session(&boat, BOAT_LOCATION_BASIN);
    for (int tick = 0; tick < 240; ++tick) {
        boat_session_step(&boat, (BoatInput) { .steering_rate = 1 });
        CHECK(boat.boat.turn_roll_load == 0 && fabsf(boat.boat.roll) < .000001f
            && fabsf(boat.boat.roll_velocity) < .000001f);
    }
    // A no-force residual decays exactly; the filter is still active in neutral.
    boat.boat.turn_roll_load = 2;
    float previous = boat.boat.turn_roll_load;
    boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(fabsf(boat.boat.turn_roll_load - previous * expf(-BOAT_STEP / 0.06f)) < .000001f);
    CHECK(boat.boat.roll > 0 && boat.boat.roll_velocity > 0);
    physics_session(&boat, BOAT_LOCATION_BASIN);
    CHECK(boat.boat.turn_roll_load == 0 && boat.boat.roll == 0 && boat.boat.roll_velocity == 0);
    boat.boat.turn_roll_load = 3;
    physics_session(&boat, BOAT_LOCATION_RIVER);
    CHECK(boat.boat.turn_roll_load == 0);
    boat.boat.turn_roll_load = 3;
    physics_session(&boat, BOAT_LOCATION_BASIN);
    boat_session_start_course(&boat);
    CHECK(boat.boat.turn_roll_load == 0);
    // Saturated demand is a bounded torque, integrated through angular velocity.
    for (int sign = -1; sign <= 1; sign += 2) {
        physics_session(&boat, BOAT_LOCATION_BASIN);
        boat.boat.turn_roll_load = sign * 10000;
        boat_session_step(&boat, (BoatInput) { 0 });
        float moment = .45f * boat.boat.roll_velocity / BOAT_STEP;
        CHECK(fabsf(moment - sign * 27.04f * 12 * DEG2RAD) < .00001f);
        CHECK(fabsf(boat.boat.roll - boat.boat.roll_velocity * BOAT_STEP) < .000001f);
    }
    // Reverse keeps the low gain, even with stale planing state.
    physics_session(&boat, BOAT_LOCATION_BASIN);
    boat.boat.velocity = (Vector2) { .8f, 3 };
    boat.boat.yaw_velocity = .4f;
    boat.boat.planing = 1;
    BoatSession before = boat;
    boat_session_step(&boat, (BoatInput) { 0 });
    CHECK(check_filter(
              &before.boat, &boat.boat, (boat.boat.velocity.x - before.boat.velocity.x) / BOAT_STEP)
        == 0);
    CHECK(fabsf(boat.boat.turn_roll_load) < .1f);
    float reverse_peak = 0;
    for (int tick = 0; tick < 960; ++tick) {
        // External tow at fixed reverse water-relative motion; vertical state evolves.
        boat.boat.position = (Vector2) { 0 };
        boat.boat.velocity = Vector2Add(Vector2Scale(forward(boat.boat.heading), -3),
            Vector2Scale(right(boat.boat.heading), .8f));
        boat.boat.yaw_velocity = .4f;
        boat_session_step(&boat, (BoatInput) { 0 });
        reverse_peak = fmaxf(reverse_peak, fabsf(boat.boat.roll));
        CHECK(reverse_peak < 3 * DEG2RAD);
    }
    printf(
        "reverse_external_work u=-3 yaw=.4 slip=.8 peak_roll_deg=%.6f\n", reverse_peak * RAD2DEG);
    // Guard recovery does not clamp angle/rate or discard the disturbed state.
    for (int sign = -1; sign <= 1; sign += 2) {
        physics_session(&boat, BOAT_LOCATION_BASIN);
        boat.boat.roll = sign * 32 * DEG2RAD;
        boat_session_step(&boat, (BoatInput) { 0 });
        CHECK(sign * boat.boat.roll > 30 * DEG2RAD && sign * boat.boat.roll_velocity < 0);
        for (int tick = 0; tick < 360; ++tick)
            boat_session_step(&boat, (BoatInput) { 0 });
        CHECK(fabsf(boat.boat.roll) < .5f * DEG2RAD
            && fabsf(boat.boat.roll_velocity) < .5f * DEG2RAD);
    }
    puts("core=ok rest_lock,neutral_filter,reset,moment_limit,reverse_gain,guard_recovery");
    return 0;
}

static int validate_sides(void)
{
    const float headings[] = { 0, PI / 2, -PI / 2, PI - .001f, -PI + .001f };
    for (size_t i = 0; i < sizeof(headings) / sizeof(*headings); ++i) {
        for (int sign = -1; sign <= 1; sign += 2) {
            BoatSession boat;
            physics_session(&boat, BOAT_LOCATION_BASIN);
            boat.boat.heading = headings[i];
            boat.boat.roll = sign * .1f;
            WaterfrontPlayerState pose = { 0 };
            boat_player_pose(SESSION_VIEW(&boat), &boat.presentation.flag, &pose);
            Matrix rotation = MatrixRotateXYZ(pose.hull.rotation);
            // Simulation starboard is renderer local negative X at every heading.
            Vector3 starboard = Vector3Transform((Vector3) { -.65f, 0, 0 }, rotation);
            Vector3 port = Vector3Transform((Vector3) { .65f, 0, 0 }, rotation);
            Vector2 rendered_right = { -cosf(boat.boat.heading), sinf(boat.boat.heading) };
            CHECK(Vector2DotProduct((Vector2) { starboard.x, starboard.z }, rendered_right) > .64f);
            CHECK(sign * (port.y - starboard.y) > .12f);
            CHECK(fabsf(starboard.y + .65f * sinf(boat.boat.roll)) < .000001f);
        }
    }
    puts("side_height_signs=ok headings=0,+/-pi/2,near+/-pi positive_roll_lowers_starboard");
    return 0;
}

static int validate_restoring(void)
{
    BoatSession boat;
    physics_session(&boat, BOAT_LOCATION_BASIN);
    boat.boat.roll = .0001f;
    boat_session_step(&boat, (BoatInput) { 0 });
    float stiffness = -(.45f * boat.boat.roll_velocity / BOAT_STEP) / .0001f;
    CHECK(fabsf(stiffness - 64 * .65f * .65f) < .002f);
    physics_session(&boat, BOAT_LOCATION_BASIN);
    boat.boat.roll = 10 * DEG2RAD;
    int last_unsettled = 0;
    for (int tick = 0; tick < 360; ++tick) {
        BoatSession before = boat;
        boat_session_step(&boat, (BoatInput) { 0 });
        row(report, "free_decay", tick, &before, 0, 0, 0);
        CHECK(isfinite(boat.boat.roll) && isfinite(boat.boat.roll_velocity));
        if (fabsf(boat.boat.roll) >= .5f * DEG2RAD
            || fabsf(boat.boat.roll_velocity) >= .5f * DEG2RAD)
            last_unsettled = tick + 1;
    }
    CHECK(fabsf(boat.boat.roll) < .5f * DEG2RAD && fabsf(boat.boat.roll_velocity) < .5f * DEG2RAD);
    printf(
        "restoring_stiffness=%.6f recovery_10deg_seconds=%.6f threshold_deg=0.5 rate_deg_s=0.5\n",
        stiffness, (last_unsettled + 1) * BOAT_STEP);
    return 0;
}

static int validate_free(void)
{
    const char *names[]
        = { "straight", "left", "right", "countersteer", "coast", "reverse", "stop" };
    for (int scenario = 0; scenario < 7; ++scenario) {
        BoatSession boat;
        banking_fixture_reset(&boat, scenario);
        boat.presentation_enabled = false;
        BoatSession repeat = boat;
        float peak_roll = 0, peak_rate = 0, peak_load = 0, sum_roll = 0;
        int ticks = banking_fixture_ticks(scenario);
        for (int tick = 0; tick < ticks; ++tick) {
            float wheel = banking_fixture_input(&boat.boat, scenario, tick);
            banking_fixture_input(&repeat.boat, scenario, tick);
            CHECK(open_water(&boat.boat));
            BoatSession before = boat;
            boat_session_step(&boat, (BoatInput) { .steering_rate = wheel });
            boat_session_step(&repeat, (BoatInput) { .steering_rate = wheel });
            CHECK(same_state(&boat, &repeat));
            CHECK(open_water(&boat.boat));
            float load
                = Vector2DotProduct(Vector2Subtract(boat.boat.velocity, before.boat.velocity),
                      right(before.boat.heading))
                / BOAT_STEP;
            CHECK(check_filter(&before.boat, &boat.boat, load) == 0);
            if (scenario == 4)
                CHECK(energy(&boat.boat) <= energy(&before.boat) + .00001f);
            row(report, names[scenario], tick, &before, load, 0, 0);
            CHECK(isfinite(load) && isfinite(boat.boat.roll) && isfinite(energy(&boat.boat)));
            if (scenario == 0 || scenario == 5 || scenario == 6)
                CHECK(fabsf(boat.boat.roll) < .000001f);
            peak_roll = fmaxf(peak_roll, fabsf(boat.boat.roll));
            peak_rate = fmaxf(peak_rate, fabsf(boat.boat.roll_velocity));
            peak_load = fmaxf(peak_load, fabsf(load));
            sum_roll += boat.boat.roll;
        }
        if (scenario == 1 || scenario == 2)
            CHECK((scenario == 1 ? -1 : 1) * boat.boat.roll > .01f);
        if (scenario == 4)
            CHECK(peak_roll > DEG2RAD); // Neutral coasting still produces a bank.
        printf(
            "free=%s mean_roll_deg=%.6f peak_roll_deg=%.6f peak_rate_deg_s=%.6f peak_load=%.6f\n",
            names[scenario], sum_roll / ticks * RAD2DEG, peak_roll * RAD2DEG, peak_rate * RAD2DEG,
            peak_load);
    }
    return 0;
}

static void prescribe(Boat *boat, float speed, float slip, float yaw)
{
    // External tow work: only horizontal position/velocity/yaw and planing are imposed.
    // Recenter on spatially uniform calm water; heave, pitch and roll keep evolving.
    boat->position = (Vector2) { 0 };
    boat->velocity = Vector2Add(
        Vector2Scale(forward(boat->heading), speed), Vector2Scale(right(boat->heading), slip));
    boat->yaw_velocity = yaw;
    boat->planing = 1;
}

static float probe_planing(float speed, float slip, float yaw, float planing)
{
    BoatSession boat;
    physics_session(&boat, BOAT_LOCATION_BASIN);
    prescribe(&boat.boat, speed, slip, yaw);
    boat.boat.planing = planing;
    Vector2 velocity = boat.boat.velocity;
    boat_session_step(&boat, (BoatInput) { 0 });
    return (boat.boat.velocity.x - velocity.x) / BOAT_STEP;
}

static float probe(float speed, float slip, float yaw)
{
    return probe_planing(speed, slip, yaw, 1);
}

static int validate_tow(void)
{
    const float speeds[] = { 4, 8, 12, 8, 8, 8 };
    const float radii[] = { 30, 30, 30, 20, 40, 80 };
    float means[6][2] = { 0 };
    for (int trial = 0; trial < 6; ++trial) {
        for (int sign = -1; sign <= 1; sign += 2) {
            float speed = speeds[trial], radius = radii[trial];
            float yaw = sign * speed / radius, target = speed * yaw;
            float lo = -16, hi = 16;
            float low_load = probe(speed, lo, yaw), high_load = probe(speed, hi, yaw);
            if (!(low_load > target && high_load < target)) {
                fprintf(stderr,
                    "No tow slip root: u=%g radius=%g target=%g bracket=[%g,%g] loads=[%g,%g]\n",
                    speed, sign * radius, target, lo, hi, low_load, high_load);
                return 1;
            }
            for (int iteration = 0; iteration < 32; ++iteration) {
                float mid = (lo + hi) / 2;
                if (probe(speed, mid, yaw) > target)
                    lo = mid;
                else
                    hi = mid;
            }
            float slip = (lo + hi) / 2;
            CHECK(fabsf(probe(speed, slip, yaw) - target) < .0002f);
            BoatSession boat;
            physics_session(&boat, BOAT_LOCATION_BASIN);
            float peak_residual = 0, mean_roll = 0, peak_roll = 0;
            char name[64];
            snprintf(name, sizeof(name), "tow_u%.0f_r%.0f_sign%d", speed, radius, sign);
            for (int tick = 0; tick < 960; ++tick) {
                prescribe(&boat.boat, speed, slip, yaw);
                CHECK(open_water(&boat.boat));
                BoatSession before = boat;
                boat_session_step(&boat, (BoatInput) { 0 });
                CHECK(open_water(&boat.boat));
                float load
                    = Vector2DotProduct(Vector2Subtract(boat.boat.velocity, before.boat.velocity),
                          right(before.boat.heading))
                    / BOAT_STEP;
                CHECK(check_filter(&before.boat, &boat.boat, load) == 0);
                float residual = load - target;
                CHECK(fabsf(residual) < .0003f);
                peak_residual = fmaxf(peak_residual, fabsf(residual));
                peak_roll = fmaxf(peak_roll, fabsf(boat.boat.roll));
                if (tick >= 720)
                    mean_roll += boat.boat.roll / 240;
                row(report, name, tick, &before, load, sign * radius, residual);
            }
            means[trial][(sign + 1) / 2] = mean_roll;
            CHECK(sign * mean_roll > .001f);
            CHECK(peak_roll < 25 * DEG2RAD);
            printf("tow_external_work u=%.0f nominal_radius=%g slip=%.6f load=%.6f "
                   "peak_residual=%.8f mean_roll_deg=%.6f peak_roll_deg=%.6f\n",
                speed, sign * radius, slip, target, peak_residual, mean_roll * RAD2DEG,
                peak_roll * RAD2DEG);
        }
    }
    for (int trial = 0; trial < 6; ++trial)
        CHECK(fabsf(means[trial][0] + means[trial][1]) < .00001f);
    CHECK(means[0][1] < means[1][1] && means[1][1] < means[2][1]);
    CHECK(means[3][1] > means[4][1] && means[4][1] > means[5][1]);
    return 0;
}

// Independent potential of unilateral supports, gravity and the passive guard.
// Only valid for unforced, non-planing flat water; imposed tow work is excluded.
static float floating_energy(const Boat *boat)
{
    float value = .5f * boat->vertical_velocity * boat->vertical_velocity
        + boat->pitch_velocity * boat->pitch_velocity
        + .225f * boat->roll_velocity * boat->roll_velocity + 9.81f * boat->height;
    float draft = 9.81f / 64;
    for (int end = -1; end <= 1; ++end)
        for (int side = -1; side <= 1; side += 2) {
            float height = boat->height + side * .65f * sinf(boat->roll) * cosf(boat->pitch)
                + end * 1.4f * sinf(boat->pitch);
            float immersion = fmaxf(0, draft - height);
            value += (64.0f / 12) * (immersion * immersion - draft * draft);
        }
    float excess = fmaxf(0, fabsf(boat->roll) - 25 * DEG2RAD);
    return value + 90 * excess * excess;
}

static int validate_release(void)
{
    const float angles[] = { -32, -10, 10, 32 };
    for (int trial = 0; trial < 4; ++trial) {
        BoatSession boat;
        physics_session(&boat, BOAT_LOCATION_BASIN);
        boat.boat.roll = angles[trial] * DEG2RAD;
        float previous = floating_energy(&boat.boat), peak_gain = 0;
        int last_unsettled = 0;
        for (int tick = 0; tick < 1200; ++tick) {
            boat_session_step(&boat, (BoatInput) { 0 });
            float next = floating_energy(&boat.boat);
            peak_gain = fmaxf(peak_gain, next - previous);
            CHECK(next <= previous + .000002f);
            CHECK(boat.boat.turn_roll_load == 0);
            previous = next;
            if (fabsf(boat.boat.roll) >= .5f * DEG2RAD
                || fabsf(boat.boat.roll_velocity) >= .5f * DEG2RAD)
                last_unsettled = tick + 1;
        }
        CHECK(last_unsettled < 360 && fabsf(previous) < .000002f);
        printf("release initial_deg=%g settle_seconds=%.6f peak_energy_gain=%.9g\n", angles[trial],
            (last_unsettled + 1) * BOAT_STEP, peak_gain);
    }
    return 0;
}

static int validate_blend_flow(void)
{
    float means[3] = { 0 };
    for (int trial = 0; trial < 3; ++trial) {
        BoatSession still, flow;
        physics_session(&still, BOAT_LOCATION_BASIN);
        flow = still;
        flow.world.water.current = (Vector2) { 1.7f, -.8f };
        float lo = -4, hi = 4, target = 64.0f / 30;
        CHECK(probe_planing(8, lo, 8.0f / 30, trial * .5f) > target);
        CHECK(probe_planing(8, hi, 8.0f / 30, trial * .5f) < target);
        for (int iteration = 0; iteration < 32; ++iteration) {
            float mid = (lo + hi) / 2;
            if (probe_planing(8, mid, 8.0f / 30, trial * .5f) > target)
                lo = mid;
            else
                hi = mid;
        }
        float slip = (lo + hi) / 2;
        for (int tick = 0; tick < 960; ++tick) {
            prescribe(&still.boat, 8, slip, 8.0f / 30);
            prescribe(&flow.boat, 8, slip, 8.0f / 30);
            still.boat.planing = flow.boat.planing = trial * .5f;
            flow.boat.velocity = Vector2Add(flow.boat.velocity, flow.world.water.current);
            // Flat, spatially uniform flow: recentering is legitimate external tow work.
            BoatSession before = still;
            boat_session_step(&still, (BoatInput) { 0 });
            boat_session_step(&flow, (BoatInput) { 0 });
            float load
                = Vector2DotProduct(Vector2Subtract(still.boat.velocity, before.boat.velocity),
                      right(before.boat.heading))
                / BOAT_STEP;
            // The tow preserves evolving heave/pitch. A temporarily dry stern now loses
            // skeg resistance; predict that missing force independently from local flow.
            BoatSession contact = before;
            contact.boat.planing
                = still.boat.planing; // Propeller check follows this tick's trim update.
            BoatSession reference;
            physics_session(&reference, BOAT_LOCATION_BASIN);
            reference.boat.planing = still.boat.planing;
            float initial_wet
                = boat_wake_stern(&reference.boat, &reference.world, boat_session_time(&reference))
                      .wet
                > 0;
            float current_wet
                = boat_wake_stern(&contact.boat, &contact.world, boat_session_time(&contact)).wet
                > 0;
            float stern_slip = slip - 1.6f * (8.0f / 30);
            float skeg_load = -.1f * hypotf(8, stern_slip) * stern_slip;
            float expected_load = target + skeg_load * (current_wet - initial_wet);
            CHECK(fabsf(load - expected_load) < .0003f);
            CHECK(check_filter(&before.boat, &still.boat, load) == 0);
            CHECK(fabsf(still.boat.roll - flow.boat.roll) < .00001f);
            CHECK(fabsf(still.boat.roll_velocity - flow.boat.roll_velocity) < .00001f);
            CHECK(fabsf(still.boat.turn_roll_load - flow.boat.turn_roll_load) < .00002f);
            if (tick >= 720)
                means[trial] += still.boat.roll / 240;
        }
        printf("blend_external_work planing=%g load=2.133333 mean_roll_deg=%.6f "
               "uniform_flow=matched\n",
            trial * .5f, means[trial] * RAD2DEG);
    }
    CHECK(means[0] > 0 && means[0] < means[1] && means[1] < means[2]);
    // Continuity of the blend: neighboring imposed planing values cannot jump the demand.
    BoatSession a, b;
    physics_session(&a, BOAT_LOCATION_BASIN);
    prescribe(&a.boat, 8, -.55f, 8.0f / 30);
    a.boat.planing = .5f;
    b = a;
    b.boat.planing += .0001f;
    boat_session_step(&a, (BoatInput) { 0 });
    boat_session_step(&b, (BoatInput) { 0 });
    CHECK(fabsf(a.boat.turn_roll_load - b.boat.turn_roll_load) < .0001f);
    const float speeds[] = { 0, 4, 12, 18 };
    for (int si = 0; si < 4; ++si) {
        physics_session(&a, BOAT_LOCATION_BASIN);
        for (int tick = 0; tick < 960; ++tick) {
            prescribe(&a.boat, speeds[si], 0, 0);
            boat_session_step(&a, (BoatInput) { 0 });
            CHECK(fabsf(a.boat.turn_roll_load) < .000001f && fabsf(a.boat.roll) < .000001f);
        }
    }
    // Docking: prescribed 1 unit/s with a substantial .4 rad/s yaw and .2 slip.
    physics_session(&a, BOAT_LOCATION_BASIN);
    float docking_peak = 0;
    for (int tick = 0; tick < 960; ++tick) {
        prescribe(&a.boat, 1, .2f, .4f);
        boat_session_step(&a, (BoatInput) { 0 });
        docking_peak = fmaxf(docking_peak, fabsf(a.boat.roll));
    }
    CHECK(docking_peak < 2 * DEG2RAD);
    printf("docking_external_work u=1 yaw=.4 slip=.2 peak_roll_deg=%.6f\n", docking_peak * RAD2DEG);
    return 0;
}

static int validate_transitions(void)
{
    BoatSession a, b;
    physics_session(&a, BOAT_LOCATION_BASIN);
    a.boat.position = (Vector2) { 0, 40 };
    a.boat.velocity.y = -12;
    a.boat.throttle = 1;
    b = a;
    bool crossed = false;
    int crossing_tick = -1;
    float peak = 0;
    for (int tick = 0; tick < 960; ++tick) {
        float wheel = tick < 480 ? 1 : -1;
        BoatSession before = a;
        CHECK(open_water(&a.boat) && open_water(&b.boat));
        boat_session_step(&a, (BoatInput) { .steering_rate = wheel });
        boat_session_step(&b, (BoatInput) { .steering_rate = -wheel });
        CHECK(open_water(&a.boat) && open_water(&b.boat));
        CHECK(fabsf(a.boat.roll + b.boat.roll) < .00002f);
        CHECK(fabsf(a.boat.roll_velocity + b.boat.roll_velocity) < .00005f);
        CHECK(fabsf(a.boat.height - b.boat.height) < .00001f
            && fabsf(a.boat.pitch - b.boat.pitch) < .00001f);
        CHECK(fabsf(Vector2Length(a.boat.velocity) - Vector2Length(b.boat.velocity)) < .00005f);
        CHECK(fabsf(a.boat.roll - before.boat.roll - a.boat.roll_velocity * BOAT_STEP) < .000001f);
        if (tick == 480)
            CHECK(a.boat.roll > DEG2RAD); // Existing bank survives opposite wheel command.
        if (tick >= 480 && !crossed && a.boat.roll < 0) {
            crossed = true;
            crossing_tick = tick;
        }
        peak = fmaxf(peak, fabsf(a.boat.roll));
        float load = Vector2DotProduct(Vector2Subtract(a.boat.velocity, before.boat.velocity),
                         right(before.boat.heading))
            / BOAT_STEP;
        row(report, "mirror_transition", tick, &before, load, 0, 0);
    }
    CHECK(crossed && crossing_tick > 481 && a.boat.roll < -DEG2RAD && a.boat.yaw_velocity < 0);
    printf("countersteer roll_zero_delay_seconds=%.6f peak_roll_deg=%.6f mirror=ok\n",
        (crossing_tick - 480 + 1) * BOAT_STEP, peak * RAD2DEG);
    // Release from a driven turn, with residual load preserved; this is not a passive
    // floating-energy test. Horizontal kinetic energy must still decrease in calm water.
    a.boat.throttle = 0;
    float release_peak = 0;
    for (int tick = 0; tick < 2400; ++tick) {
        BoatSession before = a;
        CHECK(open_water(&a.boat));
        boat_session_step(&a, (BoatInput) { 0 });
        CHECK(open_water(&a.boat));
        CHECK(energy(&a.boat) <= energy(&before.boat) + .00002f);
        float load = Vector2DotProduct(Vector2Subtract(a.boat.velocity, before.boat.velocity),
                         right(before.boat.heading))
            / BOAT_STEP;
        CHECK(check_filter(&before.boat, &a.boat, load) == 0);
        release_peak = fmaxf(release_peak, fabsf(a.boat.roll));
    }
    CHECK(fabsf(a.boat.roll) < .5f * DEG2RAD && fabsf(a.boat.roll_velocity) < .5f * DEG2RAD);
    CHECK(fabsf(a.boat.turn_roll_load) < .0001f);
    printf("driven_release peak_roll_deg=%.6f settled_after_seconds=20\n", release_peak * RAD2DEG);
    return 0;
}

static int validate_straightening(void)
{
    const float wheels[] = { .25f, .55f, 1 };
    for (int trial = 0; trial < 3; ++trial) {
        BoatSession a, b;
        banking_exit_reset(&a);
        a.presentation_enabled = false;
        b = a;
        int last_bank = -1, last_rate = -1, last_yaw = -1, last_load = -1;
        float steady = 0, peak = 0, roll_half = 0, roll_one = 0;
        char name[64];
        snprintf(name, sizeof(name), "straightening_w%.2f", wheels[trial]);
        for (int tick = 0; tick < BANKING_EXIT_TICKS; ++tick) {
            CHECK(open_water(&a.boat) && open_water(&b.boat));
            BoatSession before = a;
            banking_exit_step(&a, wheels[trial], tick);
            banking_exit_step(&b, -wheels[trial], tick);
            CHECK(open_water(&a.boat) && open_water(&b.boat));
            CHECK(fabsf(a.boat.roll + b.boat.roll) < .00002f
                && fabsf(a.boat.roll_velocity + b.boat.roll_velocity) < .00005f);
            CHECK(fabsf(a.boat.roll - before.boat.roll - a.boat.roll_velocity * BOAT_STEP)
                < .000001f);
            float load = Vector2DotProduct(Vector2Subtract(a.boat.velocity, before.boat.velocity),
                             right(before.boat.heading))
                / BOAT_STEP;
            CHECK(check_filter(&before.boat, &a.boat, load) == 0);
            row(report, name, tick, &before, load, 0, 0);
            peak = fmaxf(peak, fabsf(a.boat.roll));
            if (tick >= BANKING_EXIT_HOLD_TICKS - 120 && tick < BANKING_EXIT_HOLD_TICKS)
                steady += a.boat.roll / 120;
            if (tick >= BANKING_EXIT_HOLD_TICKS) {
                int release = tick - BANKING_EXIT_HOLD_TICKS;
                if (fabsf(a.boat.roll) >= 2 * DEG2RAD)
                    last_bank = release;
                if (fabsf(a.boat.roll_velocity) >= 2 * DEG2RAD)
                    last_rate = release;
                if (fabsf(a.boat.yaw_velocity) >= .05f)
                    last_yaw = release;
                if (fabsf(a.boat.turn_roll_load) >= .35f)
                    last_load = release;
                if (release == 59)
                    roll_half = a.boat.roll;
                if (release == 119)
                    roll_one = a.boat.roll;
            }
        }
        const float bank_limits[] = { 1.10f, 1.35f, 1.50f };
        CHECK((last_bank + 1) * BOAT_STEP <= bank_limits[trial]);
        CHECK((last_rate + 1) * BOAT_STEP <= bank_limits[trial] + .4f);
        CHECK(last_yaw < 359 && last_load < 359);
        CHECK(steady > (trial == 0 ? 5 : 10) * DEG2RAD);
        CHECK(steady < (trial == 0 ? 10 : 12) * DEG2RAD);
        CHECK(peak < 13 * DEG2RAD && roll_half > 5 * DEG2RAD);
        printf("straightening wheel=%.2f steady_deg=%.6f peak_deg=%.6f bank_2deg_seconds=%.6f "
               "rate_2deg_s_seconds=%.6f yaw_.05_seconds=%.6f load_.35_seconds=%.6f "
               "roll_0.5s_deg=%.6f roll_1s_deg=%.6f\n",
            wheels[trial], steady * RAD2DEG, peak * RAD2DEG, (last_bank + 1) * BOAT_STEP,
            (last_rate + 1) * BOAT_STEP, (last_yaw + 1) * BOAT_STEP, (last_load + 1) * BOAT_STEP,
            roll_half * RAD2DEG, roll_one * RAD2DEG);
    }
    return 0;
}

static bool finite_state(const Boat *boat)
{
    return isfinite(boat->height) && isfinite(boat->vertical_velocity) && isfinite(boat->pitch)
        && isfinite(boat->pitch_velocity) && isfinite(boat->roll) && isfinite(boat->roll_velocity)
        && isfinite(boat->turn_roll_load) && isfinite(boat->heading) && isfinite(boat->yaw_velocity)
        && isfinite(boat->position.x) && isfinite(boat->position.y) && isfinite(boat->velocity.x)
        && isfinite(boat->velocity.y);
}

static int wave_turn(int trial)
{
    const char *names[] = { "beam", "head", "following", "scripted_wake", "own_wake", "calm_long" };
    BoatSession boat;
    physics_session(&boat, BOAT_LOCATION_BASIN);
    boat.boat.position = (Vector2) { 0, 0 };
    boat.boat.velocity.y = -8;
    boat.boat.throttle = .55f;
    if (trial < 3) {
        boat.world.water = boat_water_gentle();
        boat.boat.heading = atan2f(.6f, .8f) + (trial == 0 ? PI / 2 : trial == 1 ? 0 : PI);
        boat.boat.velocity = Vector2Scale(forward(boat.boat.heading), 8);
    } else if (trial == 3) {
        boat.world.water = boat_water_test(false, false, true);
        boat.boat.position.y = 1.6f;
    } else if (trial == 4)
        boat.world.water.player_wake_enabled = true;
    BoatSession repeat = boat;
    float peak = 0, peak_rate = 0, peak_load = 0, sum = 0, late_peak = 0, early_peak = 0;
    float peak_height = 0, peak_pitch = 0;
    int drive_ticks = trial == 1 ? 43200 : 14400;
    for (int tick = 0; tick < drive_ticks; ++tick) {
        // Free constant-wheel circles; no imposed positions in spatial wave fields.
        float wheel = .55f;
        static BoatSession before;
        if (report)
            before = boat;
        boat_session_step(&boat, (BoatInput) { .steering_rate = wheel });
        boat_session_step(&repeat, (BoatInput) { .steering_rate = wheel });
        CHECK(same_state(&boat, &repeat) && finite_state(&boat.boat));
        CHECK(open_water(&boat.boat));
        CHECK(fabsf(boat.boat.roll) < 30 * DEG2RAD);
        CHECK(fabsf(boat.boat.height) < 1 && fabsf(boat.boat.pitch) < .65f);
        peak_height = fmaxf(peak_height, fabsf(boat.boat.height));
        peak_pitch = fmaxf(peak_pitch, fabsf(boat.boat.pitch));
        peak = fmaxf(peak, fabsf(boat.boat.roll));
        peak_rate = fmaxf(peak_rate, fabsf(boat.boat.roll_velocity));
        peak_load = fmaxf(peak_load, fabsf(boat.boat.turn_roll_load));
        sum += boat.boat.roll;
        if (tick >= drive_ticks / 2)
            late_peak = fmaxf(late_peak, fabsf(boat.boat.roll));
        else
            early_peak = fmaxf(early_peak, fabsf(boat.boat.roll));
        row(report, names[trial], tick, &before, NAN, 0, NAN);
    }
    // Wave encounter phase changes as the free circle evolves. Sub-guard peaks
    // need not recur in the first half. Once the 25-degree guard is involved,
    // reject growth beyond its engagement envelope or the earlier peak (+1 degree).
    CHECK(late_peak <= fmaxf(early_peak, 25 * DEG2RAD) + DEG2RAD);
    printf("standard_turn=%s seconds=%d mean_roll_deg=%.6f peak_roll_deg=%.6f "
           "late_peak_deg=%.6f peak_rate_deg_s=%.6f peak_filtered_load=%.6f peak_height=%.6f "
           "peak_pitch_deg=%.6f\n",
        names[trial], drive_ticks / 120, sum / drive_ticks * RAD2DEG, peak * RAD2DEG,
        late_peak * RAD2DEG, peak_rate * RAD2DEG, peak_load, peak_height, peak_pitch * RAD2DEG);
    // Remove external forcing, preserve momentum/filter and recover without a reset.
    boat.world.water = (BoatWater) { 0 };
    boat.boat.throttle = 0;
    for (int tick = 0; tick < 14400; ++tick) {
        boat_session_step(&boat, (BoatInput) { 0 });
        CHECK(finite_state(&boat.boat) && fabsf(boat.boat.roll) < 30 * DEG2RAD);
    }
    CHECK(fabsf(boat.boat.roll) < .0001f && fabsf(boat.boat.roll_velocity) < .0001f);
    Vector2 rest = boat.boat.position;
    for (int tick = 0; tick < 14400; ++tick)
        boat_session_step(&boat, (BoatInput) { 0 });
    printf("rest_drift=%g speed=%g\n", Vector2Distance(rest, boat.boat.position),
        Vector2Length(boat.boat.velocity));
    CHECK(Vector2Distance(rest, boat.boat.position) < .001f && fabsf(boat.boat.roll) < .000001f);
    return 0;
}

static int wave_turn_beam(void) { return wave_turn(0); }
static int wave_turn_head(void) { return wave_turn(1); }
static int wave_turn_following(void) { return wave_turn(2); }
static int wave_turn_scripted_wake(void) { return wave_turn(3); }
static int wave_turn_own_wake(void) { return wave_turn(4); }
static int wave_turn_calm_long(void) { return wave_turn(5); }

static bool footprint_clear(const BoatSession *session)
{
    const Boat *boat = &session->boat;
    bool mapped = session->world.definition->mapped;
    const Vector2 samples[]
        = { { 0, 2 }, { -.75f, 2 }, { .75f, 2 }, { 0, -1.6f }, { -.75f, -1.6f }, { .75f, -1.6f } };
    for (size_t i = 0; i < sizeof(samples) / sizeof(*samples); ++i) {
        Vector2 p = Vector2Add(boat->position,
            Vector2Add(Vector2Scale(right(boat->heading), samples[i].x),
                Vector2Scale(forward(boat->heading), samples[i].y)));
        if (mapped) {
            if (!test_river_water(p, .35f) || waterfront_harbor_contact(p, .35f, NULL))
                return false;
        } else if (fabsf(p.x) > BOAT_BASIN_HALF_X - .35f || fabsf(p.y) > BOAT_BASIN_HALF_Z - .35f)
            return false;
    }
    WaterfrontTrafficContact contact;
    return !mapped
        || !waterfront_traffic_contact(
            boat->position, boat->heading, 2, 1.6f, .75f, boat_session_time(session), &contact);
}

static int validate_long_transitions(void)
{
    BoatSession boat;
    physics_session(&boat, BOAT_LOCATION_BASIN);
    boat.world.water = boat_water_gentle();
    BoatSession repeat = boat;
    float peak = 0, peak_rate = 0;
    for (int tick = 0; tick < 14400; ++tick) {
        float wheel = sinf(tick * .005f), throttle_change = sinf(tick * .001f);
        boat_session_step(
            &boat, (BoatInput) { .steering_rate = wheel, .throttle_rate = throttle_change });
        boat_session_step(
            &repeat, (BoatInput) { .steering_rate = wheel, .throttle_rate = throttle_change });
        CHECK(same_state(&boat, &repeat) && finite_state(&boat.boat));
        CHECK(footprint_clear(&boat));
        CHECK(fabsf(boat.boat.roll) < 30 * DEG2RAD);
        peak = fmaxf(peak, fabsf(boat.boat.roll));
        peak_rate = fmaxf(peak_rate, fabsf(boat.boat.roll_velocity));
    }
    printf("long_transitions seconds=120 peak_roll_deg=%.6f peak_rate_deg_s=%.6f repeat=exact\n",
        peak * RAD2DEG, peak_rate * RAD2DEG);
    return 0;
}

static int validate_contacts(void)
{
    for (int trial = 0; trial < 8; ++trial) {
        BoatSession boat;
        physics_session(&boat, trial >= 2 ? BOAT_LOCATION_RIVER : BOAT_LOCATION_BASIN);
        if (trial < 2) {
            boat.boat.position = (Vector2) { 116, trial == 0 ? 0 : 76 };
            boat.boat.velocity = (Vector2) { 8, trial == 0 ? 0 : 8 };
            boat.boat.heading = .2f;
        } else if (trial == 2 || trial == 3) {
            boat.boat.position = trial == 2 ? (Vector2) { -34, 35 } : (Vector2) { 0, 182 };
            boat.boat.heading = trial == 2 ? -PI / 2 : PI / 2;
            boat.boat.velocity = Vector2Scale(forward(boat.boat.heading), 8);
        } else {
            int vessel = trial - 4;
            WaterfrontVesselMotion ship = waterfront_traffic_motion(vessel, 40);
            BoatModelSpec spec = waterfront_traffic_spec(vessel);
            boat_session_set_tick(&boat, 4800);
            boat.boat.position = Vector2Add((Vector2) { -ship.position.x, ship.position.z },
                Vector2Scale(right(ship.heading), spec.beam * .5f + 3));
            boat.boat.heading = ship.heading - PI / 2;
            boat.boat.velocity = Vector2Scale(forward(boat.boat.heading), 8);
        }
        int impacts = 0;
        float peak = 0, max_load_error = 0;
        for (int tick = 0; tick < 1200; ++tick) {
            // Same PRE-contact hydrodynamics at a safe basin origin. Flat water has no
            // spatial forcing; no impact delta may enter the filter or physical roll.
            BoatSession probe_boat = boat;
            probe_boat.world.resources = test_world(BOAT_LOCATION_BASIN);
            probe_boat.world.definition = probe_boat.world.resources->definition;
            probe_boat.boat.position = (Vector2) { 0 };
            boat_session_step(&probe_boat, (BoatInput) { 0 });
            boat_session_step(&boat, (BoatInput) { 0 });
            float error = fabsf(boat.boat.turn_roll_load - probe_boat.boat.turn_roll_load);
            max_load_error = fmaxf(max_load_error, error);
            CHECK(error < .000001f);
            CHECK(fabsf(boat.boat.roll - probe_boat.boat.roll) < .000001f
                && fabsf(boat.boat.roll_velocity - probe_boat.boat.roll_velocity) < .000001f);
            if (Vector2Distance(boat.boat.velocity, probe_boat.boat.velocity) > .001f)
                ++impacts;
            CHECK(footprint_clear(&boat) && finite_state(&boat.boat));
            peak = fmaxf(peak, fabsf(boat.boat.roll));
            CHECK(peak < 30 * DEG2RAD);
        }
        CHECK(impacts > 0);
        CHECK(fabsf(boat.boat.roll) < .5f * DEG2RAD);
        printf("contact=%d impact_ticks=%d peak_roll_deg=%.6f max_filter_error=%.9g\n", trial,
            impacts, peak * RAD2DEG, max_load_error);
    }
    return 0;
}

static const TestCase cases[] = {
    { "core", validate_core, false },
    { "sides", validate_sides, false },
    { "restoring", validate_restoring, false },
    { "free", validate_free, false },
    { "tow", validate_tow, false },
    { "release", validate_release, false },
    { "blend_flow", validate_blend_flow, false },
    { "transitions", validate_transitions, false },
    { "straightening", validate_straightening, false },
    SLOW_CASE(wave_turn_beam),
    SLOW_CASE(wave_turn_head),
    SLOW_CASE(wave_turn_following),
    SLOW_CASE(wave_turn_scripted_wake),
    SLOW_CASE(wave_turn_own_wake),
    SLOW_CASE(wave_turn_calm_long),
    { "long_transitions", validate_long_transitions, true },
    { "contacts", validate_contacts, false },
};

int main(int argc, char **argv)
{
    char *straightening[] = { argv[0], "--case", "straightening" };
    if (argc == 2 && strcmp(argv[1], "--straightening") == 0) {
        argc = 3;
        argv = straightening;
    } else if (argc == 3 && strcmp(argv[1], "--report") == 0) {
        report = fopen(argv[2], "w");
        if (!report) {
            perror(argv[2]);
            return 1;
        }
        fprintf(report,
            "fixture,tick,time,u,wheel,engine_angle,planing,yaw_rate,roll,roll_rate,"
            "height,pitch,horizontal_energy,lateral_acceleration,nominal_radius,"
            "load_residual,side_slip,turn_roll_load\n");
        argc = 1;
    }
    int result = test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
    if (report) {
        bool failed = ferror(report) != 0;
        if (fclose(report) != 0)
            failed = true;
        if (failed) {
            fprintf(stderr, "Failed to write banking report\n");
            result = 1;
        }
    }
    if (!result && argc == 1)
        puts("boat_banking=ok (T5 dynamics/environment/straightening regression)");
    test_worlds_close();
    return result;
}
