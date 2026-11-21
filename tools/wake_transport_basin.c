/* Private CPU-only R2b hull screen and basin trial. Never a production game option. */
#include "core/boat_clock.h"
#include "raylib.h"
#include "raymath.h"
#include "render/boat_gpu.h"
#include "session/boat_session.h"
#include "wake_transport.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "wake basin %s:%d: %s\n", __FILE__, __LINE__, #x);                     \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

typedef struct {
    uint64_t queries, mismatched_times, target_hits;
    double first_target_time;
} SampleStats;
typedef struct {
    const R2Capture *capture;
    Vector2 offset;
    bool spread;
    SampleStats *stats;
} Surface;
typedef struct {
    R2Capture capture;
    Surface surface;
    BoatSession session;
} Trial;
typedef struct {
    const char *name, *path;
    float radius;
    int turn;
    unsigned after_stop, target; // 0 outer crest, 1 gap gain, 2 inner, 3 trough, 4 crest max.
} Fixture;
typedef struct {
    float heave, roll, pitch, acceleration, launch, landing;
    float encounter_speed, encounter_heading, encounter_age;
    float surface_height, surface_gradient, surface_vertical_speed;
    unsigned landings, final_state, ticks;
    bool capsized, encountered, settled;
    unsigned target_hits;
    double first_target_time;
    uint64_t trace;
} Outcome;

static BoatWorldResources basin, open_water;
static BoatWorldDefinition open_definition;

static BoatWakeObjectSample surface_sample(const void *context, Vector2 point, double time)
{
    const Surface *s = context;
    if (s->stats) {
        ++s->stats->queries;
        s->stats->mismatched_times += fabs(time - s->capture->field.tick / 120.0) > 1e-9;
        if (Vector2LengthSqr(point) < 1e-8f) {
            if (!s->stats->target_hits)
                s->stats->first_target_time = time;
            ++s->stats->target_hits;
        }
    }
    point = Vector2Add(point, s->offset);
    return s->spread ? r2_transport_query(s->capture, point, time)
                     : r2_query(&s->capture->field, point, time);
}

static void bind_surface(Trial *t)
{
    t->surface.capture = &t->capture;
    t->session.world.water.object_sample = surface_sample;
    t->session.world.water.object_context = &t->surface;
}

static void resources(void)
{
    CHECK(boat_world_resources_init(&basin, boat_location(BOAT_LOCATION_BASIN)));
    open_definition = *boat_location(BOAT_LOCATION_BASIN);
    open_definition.name = "wake-hull-fixture";
    open_definition.half_extent = (Vector2) { 10000, 10000 };
    CHECK(boat_world_resources_init(&open_water, &open_definition));
}

static void seed(R2Capture *c, Fixture fixture)
{
    r2_capture_init(c);
    unsigned ticks
        = fixture.radius ? (unsigned)(PI / 2 * fixture.radius / 15 * 120) / 60 * 60 : 3000;
    if (!strcmp(fixture.path, "single"))
        ticks = 60;
    for (unsigned tick = 0; tick <= ticks; ++tick) {
        if (tick)
            CHECK(r2_capture_advance(c));
        R2Source s;
        if (fixture.radius) {
            float angle = tick / 120.0f * 15 / fixture.radius;
            s = (R2Source) {
                { fixture.radius * cosf(angle), fixture.turn * fixture.radius * sinf(angle) }, 15, 1
            };
        } else if (!strcmp(fixture.path, "single")) {
            s = (R2Source) { { -3.75f + tick / 8.0f, 0 }, 15, 1 };
        } else {
            s = r2_source(fixture.path, tick, 0, false);
            s.position.y *= fixture.turn;
        }
        CHECK(r2_capture_track(c, 0, 1, s));
    }
    unsigned after = fixture.after_stop * 120 - (fixture.target >= 5 ? 1 : 0);
    for (unsigned tick = 0; tick < after; ++tick)
        CHECK(r2_capture_advance(c));
    CHECK(r2_capture_valid(c));
}

// World target is translated to (0,0) by the sampler, so hulls never approach fixture walls.
static Vector2 target(const R2Capture *c, Fixture fixture, Vector2 *normal)
{
    double time = (c->field.tick + (fixture.target >= 5 ? 1 : 0)) / 120.0;
    if (!strcmp(fixture.path, "single")) {
        const BoatWakeObject *o = &c->field.records[0];
        float radius = .9f + 2.4f * (float)(time - o->born / 120.0);
        *normal = (Vector2) { 0, 1 };
        return (Vector2) { 0, sqrtf(fmaxf(0, radius * radius - .25f)) };
    }
    if (fixture.radius && fixture.target != 3 && fixture.target != 4) {
        float best = -INFINITY;
        Vector2 chosen = { 0 };
        // Same primary outer corridor as R2b, with 0.05 m radial probes. Choose max gain for
        // the gap case; record-centred outer/inner controls keep a fixed historical station.
        float begin = .25f, end = (c->field.tick / 120.0f - fixture.after_stop) - .25f;
        unsigned stations = fixture.target == 1 ? 100 : 1;
        for (unsigned i = 0; i < stations; ++i) {
            float source = begin + (i + .5f) * (end - begin) / stations;
            float angle = source * 15 / fixture.radius;
            Vector2 radial = { cosf(angle), fixture.turn * sinf(angle) };
            float age = (float)time - source - .25f;
            float distance = .9f + 2.4f * age;
            float side = fixture.target == 2 ? -1 : 1;
            for (int j = -40; j <= 40; ++j) {
                Vector2 p = Vector2Scale(radial, fixture.radius + side * (distance + j * .05f));
                BoatWakeObjectSample a = r2_query(&c->field, p, time);
                BoatWakeObjectSample b = r2_transport_query(c, p, time);
                float score = fixture.target == 1 ? b.height - a.height : a.height;
                if (score > best) {
                    best = score;
                    chosen = p;
                    *normal = Vector2Scale(radial, side);
                }
            }
        }
        return chosen;
    }
    Vector2 low, high, chosen = { 0 };
    r2_transport_bounds(c, time, &low, &high);
    float best = -INFINITY;
    for (float y = low.y; y <= high.y + 1; y += 1)
        for (float x = low.x; x <= high.x + 1; x += 1) {
            Vector2 p = { x, y };
            BoatWakeObjectSample h = r2_transport_query(c, p, time);
            float score = fixture.target == 3 || fixture.target == 5 ? -h.height : h.height;
            if (score > best) {
                best = score;
                chosen = p;
            }
        }
    // Approach along the normal of the largest individual signed contribution at this target.
    float largest = -1;
    for (unsigned i = 0; i < 32; ++i) {
        const BoatWakeObject *o = &c->field.records[i];
        float k = c->descriptors[i].curvature;
        float height = fabsf(r2_transport_piece(o, k, chosen, time).height);
        if (height > largest) {
            largest = height;
            Vector2 n = { -o->direction.y, o->direction.x };
            if (Vector2DotProduct(Vector2Subtract(chosen, o->position), n) < 0)
                n = Vector2Negate(n);
            *normal = n;
        }
    }
    return chosen;
}

static void receiver(Trial *t, const R2Capture *source, Vector2 offset, Vector2 normal, float speed,
    float angle, bool spread, bool at_time)
{
    memset(t, 0, sizeof(*t));
    memcpy(&t->capture, source, sizeof(*source));
    boat_session_init(&t->session, &open_water, (BoatWater) { 0 });
    t->session.presentation_enabled = false;
    t->surface = (Surface) { .offset = offset, .spread = spread };
    bind_surface(t);
    boat_session_set_tick(&t->session, source->field.tick);
    Vector2 toward = Vector2Rotate(normal, angle * DEG2RAD);
    t->session.boat.position = Vector2Scale(toward, 12);
    t->session.boat.velocity = Vector2Scale(toward, -speed);
    t->session.boat.heading = atan2f(-toward.x, toward.y);
    t->session.boat.throttle = t->session.boat.engine_response = .7f;
    t->session.boat.planing = 1;
    if (at_time) {
        t->session.boat.position = (Vector2) { 0 };
        Vector3 support = boat_pose_attachment(&t->session.boat, (Vector3) { -.65f, 0, 1.4f });
        t->session.boat.position = (Vector2) { support.x, -support.z };
    }
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ p[i]) * UINT64_C(1099511628211);
    return hash;
}

static void observe(Outcome *o, const Trial *t, const Boat *calm, float before, unsigned landed)
{
    const Boat *b = &t->session.boat;
    float dv = fabsf(b->vertical_velocity - before);
    float pose[] = { b->position.x, b->position.y, b->height, b->heading, b->roll, b->pitch,
        b->vertical_velocity, b->velocity.x, b->velocity.y };
    for (unsigned i = 0; i < sizeof(pose) / sizeof(*pose); ++i)
        CHECK(isfinite(pose[i]));
    o->trace = hash_bytes(o->trace, pose, sizeof(pose));
    o->trace = hash_bytes(o->trace, &b->motion_state, sizeof(b->motion_state));
    o->heave = fmaxf(o->heave, b->height - calm->height);
    o->roll = fmaxf(o->roll, fabsf(b->roll));
    o->pitch = fmaxf(o->pitch, fabsf(b->pitch));
    o->acceleration = fmaxf(o->acceleration, dv * 120);
    o->launch = fmaxf(o->launch, b->vertical_velocity);
    if (b->landings > landed)
        o->landing = fmaxf(o->landing, dv);
    o->landings = b->landings;
    o->capsized |= b->motion_state == BOAT_CAPSIZED;
    o->final_state = b->motion_state;
    BoatWakeStern stern = boat_wake_stern(b, &t->session.world, boat_session_time(&t->session));
    Vector2 p = { (float)stern.x, (float)stern.z };
    BoatWakeObjectSample h = surface_sample(&t->surface, p, boat_session_time(&t->session));
    o->surface_height = fmaxf(o->surface_height, fabsf(h.height));
    o->surface_gradient = fmaxf(o->surface_gradient, Vector2Length(h.gradient));
    o->surface_vertical_speed = fmaxf(o->surface_vertical_speed, fabsf(h.vertical_velocity));
    if (!o->encountered && stern.wet > .05
        && (fabsf(h.height) > .05f || Vector2Length(h.gradient) > .05f)) {
        o->encountered = true;
        o->encounter_speed = (float)hypot(stern.vx, stern.vz);
        o->encounter_heading = b->heading;
        Vector2 local = Vector2Add(p, t->surface.offset);
        float largest = -1;
        for (unsigned i = 0; i < 32; ++i) {
            const BoatWakeObject *piece = &t->capture.field.records[i];
            BoatWakeObjectSample q = r2_transport_piece(piece,
                t->surface.spread ? t->capture.descriptors[i].curvature : 0, local,
                boat_session_time(&t->session));
            if (fabsf(q.height) > largest && piece->amplitude) {
                largest = fabsf(q.height);
                o->encounter_age = (t->capture.field.tick - piece->born) / 120.0f;
            }
        }
    }
}

static bool stable(const Boat *b)
{
    return b->motion_state != BOAT_AIRBORNE && b->motion_state != BOAT_LANDING
        && fabsf(b->vertical_velocity) < .05f;
}

static void run_pair(const R2Capture *source, Vector2 offset, Vector2 normal, float speed,
    float angle, bool at_time, Outcome out[2])
{
    Trial twins[2];
    receiver(&twins[0], source, offset, normal, speed, angle, false, at_time);
    receiver(&twins[1], source, offset, normal, speed, angle, true, at_time);
    SampleStats stats[2] = { 0 };
    for (unsigned mode = 0; mode < 2; ++mode)
        twins[mode].surface.stats = &stats[mode];
    BoatSession calm = twins[0].session;
    calm.world.water = (BoatWater) { 0 };
    BoatWakeObjects flat;
    CHECK(boat_wake_objects_init(&flat, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
    flat.tick = calm.tick;
    calm.world.water.objects = &flat; // Keep the same arcade handling on the calm reference.
    memset(out, 0, sizeof(Outcome) * 2);
    out[0].trace = out[1].trace = UINT64_C(14695981039346656037);
    unsigned settled = 0;
    for (unsigned tick = 0; tick < 2880; ++tick) {
        boat_wake_objects_advance(&flat);
        CHECK(boat_session_step(&calm, (BoatInput) { 0 }));
        for (unsigned mode = 0; mode < 2; ++mode) {
            Trial *t = &twins[mode];
            float before = t->session.boat.vertical_velocity;
            unsigned landed = t->session.boat.landings;
            CHECK(r2_capture_advance(&t->capture));
            CHECK(boat_session_step(&t->session, (BoatInput) { 0 }));
            CHECK(t->session.tick == t->capture.field.tick);
            if (at_time && tick == 0)
                CHECK(stats[mode].target_hits > 0);
            observe(&out[mode], t, &calm.boat, before, landed);
            out[mode].ticks = tick + 1;
        }
        settled = !twins[0].capture.field.live && !twins[1].capture.field.live
                && stable(&twins[0].session.boat) && stable(&twins[1].session.boat)
            ? settled + 1
            : 0;
        if (tick >= 959 && settled >= 120)
            break;
    }
    out[0].settled = out[1].settled = settled >= 120;
    for (unsigned mode = 0; mode < 2; ++mode) {
        out[mode].target_hits = (unsigned)stats[mode].target_hits;
        out[mode].first_target_time = stats[mode].first_target_time;
        CHECK(stats[mode].mismatched_times == 0);
        boat_session_destroy(&twins[mode].session);
    }
    boat_session_destroy(&calm);
}

static void screen(const char *path)
{
    FILE *out = fopen(path, "w");
    CHECK(out);
    fputs(
        "fixture,turn,after_stop_seconds,target,initial_speed,initial_angle,mode,target_x,target_z,"
        "heave,roll,pitch,acceleration,launch,landing_delta_v_proxy,landings,capsized,final_state,"
        "ticks,settled,encountered,encounter_speed,encounter_heading,dominant_record_age,"
        "peak_stern_height,peak_stern_gradient,peak_stern_vertical_speed,target_support_queries,"
        "first_target_time,target_initial_height,trace\n",
        out);
    const Fixture fixtures[] = {
        { "straight", "single", 0, 1, 1, 0 },
        { "straight", "single", 0, 1, 6, 0 },
        { "straight", "single", 0, 1, 12, 0 },
        { "outer12", "quarter", 12, 1, 1, 0 },
        { "outer12", "quarter", 12, -1, 1, 0 },
        { "gap12", "quarter", 12, 1, 4, 1 },
        { "gap12", "quarter", 12, -1, 4, 1 },
        { "gap12", "quarter", 12, 1, 8, 1 },
        { "gap12", "quarter", 12, -1, 8, 1 },
        { "old12", "quarter", 12, 1, 12, 0 },
        { "old12", "quarter", 12, -1, 12, 0 },
        { "inner12", "quarter", 12, 1, 1, 2 },
        { "inner12", "quarter", 12, -1, 1, 2 },
        { "outer50", "quarter", 50, 1, 1, 0 },
        { "outer50", "quarter", 50, -1, 1, 0 },
        { "gap50", "quarter", 50, 1, 4, 1 },
        { "gap50", "quarter", 50, -1, 4, 1 },
        { "gap50", "quarter", 50, 1, 8, 1 },
        { "gap50", "quarter", 50, -1, 8, 1 },
        { "old50", "quarter", 50, 1, 12, 0 },
        { "old50", "quarter", 50, -1, 12, 0 },
        { "inner50", "quarter", 50, 1, 4, 2 },
        { "inner50", "quarter", 50, -1, 4, 2 },
        { "eight-trough", "eight", 0, 1, 4, 3 },
        { "eight-crest", "eight", 0, 1, 4, 4 },
        { "eight-trough-at-time", "eight", 0, 1, 4, 5 },
        { "eight-crest-at-time", "eight", 0, 1, 4, 6 },
        { "reversal", "reverse", 0, 1, 4, 4 },
    };
    const float speeds[] = { 8, 16, 24 }, angles[] = { 0, 30, 60 };
    for (unsigned f = 0; f < sizeof(fixtures) / sizeof(*fixtures); ++f) {
        R2Capture source;
        seed(&source, fixtures[f]);
        Vector2 normal = { 0, 1 }, offset = target(&source, fixtures[f], &normal);
        for (unsigned si = 0; si < 3; ++si)
            for (unsigned ai = 0; ai < 3; ++ai) {
                Outcome result[2];
                run_pair(&source, offset, normal, speeds[si], angles[ai], fixtures[f].target >= 5,
                    result);
                for (unsigned mode = 0; mode < 2; ++mode) {
                    Outcome *o = &result[mode];
                    fprintf(out,
                        "%s,%d,%u,%u,%.9g,%.9g,%s,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
                        "%.9g,%.9g,%u,%d,%u,%u,%d,%d,",
                        fixtures[f].name, fixtures[f].turn, fixtures[f].after_stop,
                        fixtures[f].target, (double)speeds[si], (double)angles[ai],
                        mode ? "spread" : "baseline", (double)offset.x, (double)offset.y,
                        (double)o->heave, (double)o->roll, (double)o->pitch,
                        (double)o->acceleration, (double)o->launch, (double)o->landing, o->landings,
                        o->capsized, o->final_state, o->ticks, o->settled, o->encountered);
                    if (o->encountered)
                        fprintf(out, "%.9g,%.9g,%.9g,", (double)o->encounter_speed,
                            (double)o->encounter_heading, (double)o->encounter_age);
                    else
                        fputs(",,,", out);
                    double target_time
                        = (source.field.tick + (fixtures[f].target >= 5 ? 1 : 0)) / 120.0;
                    BoatWakeObjectSample initial = mode
                        ? r2_transport_query(&source, offset, target_time)
                        : r2_query(&source.field, offset, target_time);
                    fprintf(out, "%.9g,%.9g,%.9g,%u,%.9g,%.9g,%016llx\n", (double)o->surface_height,
                        (double)o->surface_gradient, (double)o->surface_vertical_speed,
                        o->target_hits, o->first_target_time, (double)initial.height,
                        (unsigned long long)o->trace);
                }
                if (f < 3)
                    CHECK(result[0].trace == result[1].trace);
            }
    }
    CHECK(fclose(out) == 0);
}

static BoatWakeObjectSample production_sample(const void *context, Vector2 point, double time)
{
    (void)time;
    return boat_wake_objects_query(context, point);
}

static void checks(void)
{
    BoatWakeObjects objects;
    CHECK(boat_wake_objects_init(&objects, 1920, 1.2f));
    CHECK(boat_wake_objects_track(&objects, 0, (Vector2) { 0 }, 15, 1));
    for (unsigned tick = 1; tick <= 240; ++tick) {
        boat_wake_objects_advance(&objects);
        CHECK(boat_wake_objects_track(&objects, 0, (Vector2) { tick / 8.0f, 0 }, 15, 1));
    }
    BoatWater normal = boat_water_gentle();
    normal.objects = &objects;
    normal.current = (Vector2) { .7f, -.2f };
    BoatWater adapted = normal;
    adapted.object_sample = production_sample;
    adapted.object_context = &objects;
    adapted.objects = NULL;
    for (unsigned i = 0; i < 100; ++i) {
        Vector2 p = { i * .3f, -10 + i * .17f };
        BoatWaterSample a = boat_water_query(normal, p, 2);
        BoatWaterSample b = boat_water_query(adapted, p, 2);
        CHECK(!memcmp(&a, &b, sizeof(a)));
    }
    BoatSession direct, callback;
    boat_session_init(&direct, &open_water, normal);
    boat_session_set_tick(&direct, objects.tick);
    direct.boat.position = (Vector2) { 12, 3 };
    direct.boat.height = 8;
    direct.boat.roll = 1;
    direct.boat.pitch = .4f;
    direct.boat.motion_state = BOAT_AIRBORNE;
    direct.boat.dry_time = .1f;
    callback = direct;
    callback.world.water = adapted;
    for (unsigned tick = 0; tick < 480; ++tick) {
        boat_wake_objects_advance(&objects);
        CHECK(boat_session_step(&direct, (BoatInput) { 0 }));
        CHECK(boat_session_step(&callback, (BoatInput) { 0 }));
        CHECK(!memcmp(&direct.boat, &callback.boat, sizeof(Boat)));
    }
    boat_session_destroy(&direct);
    boat_session_destroy(&callback);
    Trial t;
    memset(&t, 0, sizeof(t));
    seed(&t.capture, (Fixture) { "control", "single", 0, 1, 1, 0 });
    boat_session_init(&t.session, &basin, (BoatWater) { 0 });
    boat_session_set_tick(&t.session, t.capture.field.tick);
    SampleStats stats = { 0 };
    t.surface.stats = &stats;
    t.surface.spread = true;
    bind_surface(&t);
    CHECK(boat_water_displaced(t.session.world.water));
    CHECK(r2_capture_advance(&t.capture));
    CHECK(boat_session_step(&t.session, (BoatInput) { 0 }));
    CHECK(stats.queries > 0 && stats.mismatched_times == 0);
    stats = (SampleStats) { 0 };
    Color pixels[320 * 200] = { 0 };
    Renderer3D renderer = { 0 };
    BoatCpuView cpu = { 0 };
    BoatViewState view = boat_session_view(&t.session);
    CHECK(boat_draw(&renderer, &cpu, pixels, 320, 200, &view, &t.session.presentation,
        &t.session.world, BOAT_CAMERA_DEFAULT));
    CHECK(stats.queries > 0 && stats.mismatched_times == 0);
    BoatGpu gpu = { 0 };
    boat_gpu_draw(&gpu, &view, &t.session.presentation, &t.session.world, BOAT_CAMERA_DEFAULT);
    CHECK(gpu.failed); // Rejected before any graphics-context call.
    renderer3d_destroy(&renderer);
    boat_cpu_view_destroy(&cpu);
    boat_session_destroy(&t.session);
    Fixture fixture = { "gap12", "quarter", 12, 1, 4, 1 };
    R2Capture source;
    seed(&source, fixture);
    Vector2 n, offset = target(&source, fixture, &n);
    Outcome a[2], b[2];
    run_pair(&source, offset, n, 16, 30, false, a);
    run_pair(&source, offset, n, 16, 30, false, b);
    CHECK(a[0].trace == b[0].trace && a[1].trace == b[1].trace);
    puts("wake_transport_basin_contract=ok");
}

static void reset_trial(Trial *t, bool spread)
{
    memset(t, 0, sizeof(*t));
    r2_capture_init(&t->capture);
    boat_session_init(&t->session, &basin, (BoatWater) { 0 });
    t->surface.spread = spread;
    bind_surface(t);
    BoatWakeStern stern = boat_wake_stern(&t->session.boat, &t->session.world, 0);
    CHECK(r2_capture_track(
        &t->capture, 0, 1, (R2Source) { { (float)stern.x, (float)stern.z }, 0, (float)stern.wet }));
}

static void start_trial(Trial *t, bool spread, const char *encounter)
{
    if (!encounter) {
        reset_trial(t, spread);
        return;
    }
    Fixture fixture;
    if (!strcmp(encounter, "gap12"))
        fixture = (Fixture) { "gap12", "quarter", 12, 1, 4, 1 };
    else if (!strcmp(encounter, "gap50"))
        fixture = (Fixture) { "gap50", "quarter", 50, 1, 8, 1 };
    else if (!strcmp(encounter, "eight-trough"))
        fixture = (Fixture) { "eight-trough-at-time", "eight", 0, 1, 4, 5 };
    else {
        fputs("Encounter must be gap12, gap50 or eight-trough.\n", stderr);
        exit(2);
    }
    R2Capture source;
    seed(&source, fixture);
    Vector2 normal, offset = target(&source, fixture, &normal);
    receiver(
        t, &source, offset, normal, fixture.radius == 50 ? 24 : 16, 0, spread, fixture.target >= 5);
    t->session.presentation_enabled = true;
    t->session.presentation.camera_heading = t->session.boat.heading;
}

static BoatInput demo_input(uint64_t tick)
{
    return (BoatInput) { .throttle_rate = tick < 180 ? 1 : 0,
        .steering_rate = tick < 240 ? 0
            : tick % 1440 < 720     ? .7f
                                    : -.7f };
}

static void step_trial(Trial *t, BoatInput input, bool frozen)
{
    CHECK(r2_capture_advance(&t->capture));
    CHECK(boat_session_step(&t->session, input));
    CHECK(t->capture.field.tick == t->session.tick);
    if (frozen)
        return;
    BoatWakeStern stern
        = boat_wake_stern(&t->session.boat, &t->session.world, boat_session_time(&t->session));
    CHECK(r2_capture_track(&t->capture, 0, 1,
        (R2Source) { { (float)stern.x, (float)stern.z }, (float)hypot(stern.vx, stern.vz),
            (float)stern.wet }));
}

int main(int argc, char **argv)
{
    bool spread = true, demo = false, check = false;
    const char *screenshot = NULL, *screen_path = NULL, *capture = NULL, *encounter = NULL;
    unsigned ticks = 1200, frames = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--check"))
            check = true;
        else if (!strcmp(argv[i], "--demo"))
            demo = true;
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            const char *mode = argv[++i];
            CHECK(!strcmp(mode, "spread") || !strcmp(mode, "baseline"));
            spread = !strcmp(mode, "spread");
        } else if (!strcmp(argv[i], "--renderer") && i + 1 < argc) {
            if (strcmp(argv[++i], "cpu")) {
                fputs("Wake research basin requires --renderer cpu.\n", stderr);
                return 2;
            }
        } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
            screenshot = argv[++i];
        else if (!strcmp(argv[i], "--capture") && i + 1 < argc)
            capture = argv[++i];
        else if (!strcmp(argv[i], "--encounter") && i + 1 < argc)
            encounter = argv[++i];
        else if (!strcmp(argv[i], "--screen") && i + 1 < argc)
            screen_path = argv[++i];
        else if ((!strcmp(argv[i], "--ticks") || !strcmp(argv[i], "--frames")) && i + 1 < argc) {
            bool is_ticks = !strcmp(argv[i], "--ticks");
            char *end;
            unsigned long value = strtoul(argv[++i], &end, 10);
            CHECK(*argv[i] && !*end && value > 0 && value <= 72000);
            if (is_ticks)
                ticks = (unsigned)value;
            else
                frames = (unsigned)value;
        } else {
            fputs("Usage: wake_transport_basin [--mode baseline|spread] [--demo] [--frames N] "
                  "[--screenshot PNG|--capture DIR --ticks N] [--encounter "
                  "gap12|gap50|eight-trough] | "
                  "--screen CSV | --check\n",
                stderr);
            return 2;
        }
    }
    SetTraceLogLevel(LOG_WARNING);
    if (encounter && (!screenshot && !capture)) {
        fputs("Frozen encounters require --capture or --screenshot.\n", stderr);
        return 2;
    }
    if (encounter && ticks > 240) {
        fputs("Frozen encounter captures support at most 240 ticks.\n", stderr);
        return 2;
    }
    resources();
    if (check || screen_path) {
        if (check)
            checks();
        if (screen_path)
            screen(screen_path);
        boat_world_resources_destroy(&open_water);
        boat_world_resources_destroy(&basin);
        return 0;
    }
    Trial trial;
    start_trial(&trial, spread, encounter);
    Color pixels[320 * 200] = { 0 };
    Renderer3D renderer = { 0 };
    BoatCpuView cpu = { 0 };
    if (screenshot || capture) {
        // Frozen encounter clips stay near the basin's finite rendered water mesh.
        CHECK(!capture || !encounter || ticks <= 240);
        CHECK(!capture || DirectoryExists(capture));
        for (unsigned tick = 0; tick < ticks; ++tick) {
            step_trial(&trial, encounter ? (BoatInput) { 0 } : demo_input(trial.session.tick),
                encounter != NULL);
            if (capture && (tick + 1) % 12 == 0) {
                BoatViewState current = boat_session_view(&trial.session);
                CHECK(boat_draw(&renderer, &cpu, pixels, 320, 200, &current,
                    &trial.session.presentation, &trial.session.world, BOAT_CAMERA_DEFAULT));
                char path[4096];
                CHECK(snprintf(path, sizeof(path), "%s/%04u.png", capture, tick / 12)
                    < (int)sizeof(path));
                CHECK(ExportImage(
                    (Image) { pixels, 320, 200, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 }, path));
            }
        }
        BoatViewState view = boat_session_view(&trial.session);
        CHECK(boat_draw(&renderer, &cpu, pixels, 320, 200, &view, &trial.session.presentation,
            &trial.session.world, BOAT_CAMERA_DEFAULT));
        if (screenshot)
            CHECK(ExportImage(
                (Image) { pixels, 320, 200, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 }, screenshot));
    } else {
        SetConfigFlags(FLAG_WINDOW_RESIZABLE);
        InitWindow(960, 600, "Wake research - basin (CPU)");
        SetWindowMinSize(320, 200);
        SetTargetFPS(60);
        Texture2D texture = LoadTextureFromImage(
            (Image) { pixels, 320, 200, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 });
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
        double accumulator = 0;
        unsigned frame = 0;
        while (!WindowShouldClose() && (!frames || frame < frames)) {
            if (IsKeyPressed(KEY_B) || IsKeyPressed(KEY_R)) {
                if (IsKeyPressed(KEY_B))
                    spread = !spread;
                boat_session_destroy(&trial.session);
                start_trial(&trial, spread, encounter);
                boat_cpu_view_destroy(&cpu);
                accumulator = 0;
            }
            BoatInput input = { .steering_rate = (float)(IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))
                    - (float)(IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)),
                .throttle_rate = (float)(IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))
                    - (float)(IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) };
            accumulator += fmin(GetFrameTime(), .25);
            unsigned steps = demo ? 2 : (unsigned)(accumulator / BOAT_TICK_SECONDS);
            if (!demo)
                accumulator -= steps * BOAT_TICK_SECONDS;
            for (unsigned step = 0; step < steps; ++step)
                step_trial(&trial,
                    demo ? (encounter ? (BoatInput) { 0 } : demo_input(trial.session.tick)) : input,
                    encounter != NULL);
            BoatViewState view = boat_session_view(&trial.session);
            CHECK(boat_draw(&renderer, &cpu, pixels, 320, 200, &view, &trial.session.presentation,
                &trial.session.world, BOAT_CAMERA_DEFAULT));
            UpdateTexture(texture, pixels);
            int scale = GetScreenWidth() / 320;
            if (GetScreenHeight() / 200 < scale)
                scale = GetScreenHeight() / 200;
            if (scale < 1)
                scale = 1;
            int x = (GetScreenWidth() - 320 * scale) / 2;
            int y = (GetScreenHeight() - 200 * scale) / 2;
            BeginDrawing();
            ClearBackground(BLACK);
            DrawTexturePro(texture, (Rectangle) { 0, 0, 320, 200 },
                (Rectangle) { (float)x, (float)y, (float)(320 * scale), (float)(200 * scale) },
                (Vector2) { 0 }, 0, WHITE);
            DrawText(spread ? "RESEARCH: SPREAD" : "RESEARCH: BASELINE", x + 4 * scale,
                y + 4 * scale, 10 * scale, WHITE);
            DrawRectangle(x, y + 172 * scale, 320 * scale, 28 * scale, (Color) { 0, 0, 0, 210 });
            DrawText("W/S throttle  A/D steer  Esc quit", x + 4 * scale, y + 174 * scale,
                10 * scale, WHITE);
            DrawText(
                "B compare (resets)  R reset", x + 4 * scale, y + 186 * scale, 10 * scale, WHITE);
            EndDrawing();
            ++frame;
        }
        UnloadTexture(texture);
        CloseWindow();
    }
    printf("wake_basin mode=%s tick=%llu live=%u landings=%u\n", spread ? "spread" : "baseline",
        (unsigned long long)trial.session.tick, trial.capture.field.live,
        trial.session.boat.landings);
    boat_session_destroy(&trial.session);
    renderer3d_destroy(&renderer);
    boat_cpu_view_destroy(&cpu);
    boat_world_resources_destroy(&open_water);
    boat_world_resources_destroy(&basin);
    return 0;
}
