#include "boat_clock.h"
#include "boat_map.h"
#include "boat_test_session.h"
#include "raymath.h"
#include "rlgl.h"
#include "waterfront_scene.h"

#include <math.h>
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

static int current_checks(void)
{
    BoatWater channel = boat_water_test(false, true, false);
    CHECK(Vector2Length(boat_water_current(channel, (Vector2) { 0 })) > .8f);
    CHECK(Vector2Length(boat_water_current(channel, (Vector2) { 33, 0 })) == 0);
    CHECK(Vector2Length(boat_water_current(channel, (Vector2) { 31.999f, 0 })) < 1e-6f);
    for (float x = -33; x < 33; x += .1f) {
        Vector2 a = boat_water_current(channel, (Vector2) { x, 0 });
        Vector2 b = boat_water_current(channel, (Vector2) { x + .001f, 0 });
        CHECK(Vector2Distance(a, b) < .0001f);
    }
    // Galilean equivalence in a uniform flow, including steering and the local stern station.
    BoatSession still, moving;
    boat_session_init(&still, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_session_init(&moving, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    Vector2 flow = { .7f, -.3f };
    moving.world.water.current = flow;
    moving.boat.velocity = flow;
    still.boat.throttle = moving.boat.throttle = .45f;
    for (int tick = 0; tick < 240; tick++) {
        boat_session_step(&still, (BoatInput) { .2f, 0, 0, 0 });
        boat_session_step(&moving, (BoatInput) { .2f, 0, 0, 0 });
        CHECK(Vector2Distance(still.boat.velocity, Vector2Subtract(moving.boat.velocity, flow))
            < .00003f);
        CHECK(fabsf(still.boat.heading - moving.boat.heading) < .00001f);
        CHECK(fabsf(still.boat.height - moving.boat.height) < .00001f);
        CHECK(fabsf(still.boat.planing - moving.boat.planing) < .00001f);
    }
    Vector2 offset = Vector2Scale(flow, 240 * BOAT_STEP);
    CHECK(Vector2Distance(moving.boat.position, Vector2Add(still.boat.position, offset)) < .0001f);
    BoatSession drift;
    boat_session_init(&drift, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    drift.world.water = channel;
    for (int tick = 0; tick < 1200; tick++)
        boat_session_step(&drift, (BoatInput) { 0 });
    CHECK(drift.boat.position.x > 3 && drift.boat.velocity.x > .4f && drift.boat.planing == 0);
    // Authored flow remains contained by the existing basin shore contacts.
    drift.boat.position = (Vector2) { 117, 0 };
    drift.world.water.current_width = 0;
    for (int tick = 0; tick < 3600; tick++) {
        boat_session_step(&drift, (BoatInput) { 0 });
        CHECK(drift.boat.position.x < BOAT_BASIN_HALF_X
            && fabsf(drift.boat.position.y) < BOAT_BASIN_HALF_Z);
        CHECK(isfinite(drift.boat.height));
    }
    return 0;
}

static int wake_checks(void)
{
    BoatWater wake = boat_water_test(false, false, true);
    float peak = 0;
    for (float t = 1; t < 25; t += .37f)
        for (float x = -28; x < 28; x += 3.3f)
            for (float z = -36; z < 36; z += 2.7f) {
                Vector2 p = { x, z };
                BoatWaterSample q = boat_water_query(wake, p, t);
                CHECK(fabsf(q.height) <= wake.wake_amplitude && q.normal.y > .5f);
                peak = fmaxf(peak, fabsf(q.height));
                float dt = .001f;
                float dy = (boat_water_query(wake, p, t + dt).height
                               - boat_water_query(wake, p, t - dt).height)
                    / (2 * dt);
                float dx = (boat_water_query(wake, (Vector2) { x + dt, z }, t).height
                               - boat_water_query(wake, (Vector2) { x - dt, z }, t).height)
                    / (2 * dt);
                float dz = (boat_water_query(wake, (Vector2) { x, z + dt }, t).height
                               - boat_water_query(wake, (Vector2) { x, z - dt }, t).height)
                    / (2 * dt);
                CHECK(fabsf(dy - q.velocity.y) < .002f);
                CHECK(fabsf(dx + q.normal.x / q.normal.y) < .002f);
                CHECK(fabsf(dz + q.normal.z / q.normal.y) < .002f);
            }
    CHECK(peak > .2f);
    BoatWater combined = boat_water_test(true, true, true);
    for (float x = -20; x < 20; x += 3) {
        Vector2 p = { x, 2 }, flow = boat_water_current(combined, p);
        BoatWaterSample q = boat_water_query(combined, p, 10);
        float dt = .001f;
        float derivative
            = (boat_water_query(combined, Vector2Add(p, Vector2Scale(flow, dt)), 10 + dt).height
                  - boat_water_query(combined, Vector2Subtract(p, Vector2Scale(flow, dt)), 10 - dt)
                      .height)
            / (2 * dt);
        CHECK(fabsf(derivative - q.velocity.y) < .002f);
    }
    Vector2 source;
    CHECK(boat_water_wake_source(wake, 6, &source) && source.x == 0 && source.y == -8);
    CHECK(!boat_water_wake_source(wake, 13, &source));
    for (float x = -40; x < 40; x += 4) {
        BoatWaterSample q = boat_water_query(wake, (Vector2) { x, 0 }, 25);
        CHECK(q.height == 0 && q.velocity.y == 0 && q.normal.y == 1);
    }
    BoatWater shifted = wake;
    shifted.wake_offset = (Vector2) { -140, -220 };
    for (float t = 4; t <= 16; t += 2) {
        Vector2 p = { -10, 4 };
        BoatWaterSample a = boat_water_query(wake, p, t);
        BoatWaterSample b = boat_water_query(shifted, Vector2Add(p, shifted.wake_offset), t);
        CHECK(fabsf(a.height - b.height) < .00001f);
        CHECK(Vector3Distance(a.normal, b.normal) < .00001f);
        CHECK(Vector3Distance(a.velocity, b.velocity) < .00001f);
    }
    for (int tick = 0; tick <= 120; ++tick) {
        CHECK(boat_water_wake_source(shifted, tick * .1f, &source));
        CHECK(test_river_water(source, 3));
    }
    // Repeated crossings at different speeds and angles must settle without stored energy.
    const float speeds[] = { 0, 3, 9 };
    float encounter = 0;
    for (int si = 0; si < 3; si++)
        for (int ai = 0; ai < 3; ai++) {
            BoatSession a, b;
            boat_session_init(&a, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
            a.boat.position = (Vector2) { 0, 1.6f };
            a.boat.heading = ai * PI / 4;
            a.presentation.camera_heading = a.boat.heading;
            boat_session_set_tick(&a, 1200);
            a.boat.velocity = (Vector2) { sinf(a.boat.heading) * speeds[si],
                -cosf(a.boat.heading) * speeds[si] };
            a.world.water = wake;
            b = a;
            for (int tick = 0; tick < 3600; tick++) {
                boat_session_step(&a, (BoatInput) { 0 });
                boat_session_step(&b, (BoatInput) { 0 });
                CHECK(a.boat.turn_roll_load == b.boat.turn_roll_load);
                CHECK(a.boat.height == b.boat.height && a.boat.position.x == b.boat.position.x
                    && a.boat.pitch == b.boat.pitch);
                CHECK(isfinite(a.boat.height) && fabsf(a.boat.height) < 1);
                CHECK(fabsf(a.boat.roll) < .65f && fabsf(a.boat.pitch) < .65f);
                CHECK(Vector2Length(a.boat.velocity) < 12);
                if (si == 0)
                    encounter = fmaxf(encounter, fabsf(a.boat.height));
            }
            CHECK(fabsf(a.boat.height) < .002f && fabsf(a.boat.pitch) < .002f
                && fabsf(a.boat.roll) < .002f);
            CHECK(fabsf(a.boat.vertical_velocity) < .002f && Vector2Length(a.boat.velocity) < .08f);
        }
    CHECK(encounter > .01f);
    return 0;
}

static int player_wake_checks(void)
{
    BoatWater water = { .player_wake_enabled = true };
    boat_water_emit_wake(&water, (Vector2) { -8, 0 }, (Vector2) { 0, 0 }, 2, .4f, 20);
    boat_water_emit_wake(&water, (Vector2) { 0, 0 }, (Vector2) { 8, 0 }, 2.4f, .4f, 20);
    boat_water_emit_wake(&water, (Vector2) { 8, 0 }, (Vector2) { 8, 8 }, 2.8f, .4f, 20);
    float peak = 0;
    // Derivatives must include the normalization when curved/intersecting patches overlap.
    for (float t = 3; t < 24; t += .71f)
        for (float x = -12; x < 12; x += 2.3f)
            for (float z = -16; z < 16; z += 2.7f) {
                Vector2 p = { x, z };
                BoatWaterSample q = boat_water_query(water, p, t);
                peak = fmaxf(peak, fabsf(q.height));
                CHECK(fabsf(q.height) <= .65f && q.normal.y > .4f);
                float dt = .001f;
                float dy = (boat_water_query(water, p, t + dt).height
                               - boat_water_query(water, p, t - dt).height)
                    / (2 * dt);
                float dx = (boat_water_query(water, (Vector2) { x + dt, z }, t).height
                               - boat_water_query(water, (Vector2) { x - dt, z }, t).height)
                    / (2 * dt);
                float dz = (boat_water_query(water, (Vector2) { x, z + dt }, t).height
                               - boat_water_query(water, (Vector2) { x, z - dt }, t).height)
                    / (2 * dt);
                CHECK(fabsf(dy - q.velocity.y) < .002f);
                CHECK(fabsf(dx + q.normal.x / q.normal.y) < .002f);
                CHECK(fabsf(dz + q.normal.z / q.normal.y) < .002f);
            }
    CHECK(peak > .25f);
    // Track the same positive crest in world space: it advances 1.5 m in half a second,
    // while the original point falls to a zero crossing. This catches a frozen phase.
    BoatWater travelling = { .player_wake_enabled = true };
    boat_water_emit_wake(&travelling, (Vector2) { -8, 0 }, (Vector2) { 0, 0 }, .2f, .4f, 20);
    float radius0 = .9f + 3 * 4 - 4.5f;
    float radius1 = radius0 + 3 * .5f;
    Vector2 crest0 = { -4, sqrtf(radius0 * radius0 - .25f) };
    Vector2 crest1 = { -4, sqrtf(radius1 * radius1 - .25f) };
    CHECK(boat_water_query(travelling, crest0, 4).height > .1f);
    CHECK(boat_water_query(travelling, crest1, 4.5f).height > .1f);
    CHECK(fabsf(boat_water_query(travelling, crest0, 4.5f).height) < .001f);
    CHECK(Vector2Distance(crest0, crest1) > 1.49f);
    // Densely overlapping history is bounded; copies own their history, reset clears it.
    BoatWater copy = water;
    for (int i = 0; i < 200; ++i)
        boat_water_emit_wake(&copy, (Vector2) { -8, 0 }, (Vector2) { 0, 0 }, 2, .4f, 20);
    CHECK(water.player_wake_count == 3 && copy.player_wake_count == BOAT_WAKE_SEGMENTS);
    for (float z = 0; z < 20; z += .1f)
        CHECK(fabsf(boat_water_query(copy, (Vector2) { -4, z }, 8).height) <= .65f);
    CHECK(boat_water_query(copy, (Vector2) { -4, 6 }, 25).height == 0);
    BoatSession drifting;
    boat_session_init(&drifting, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    drifting.world.water = (BoatWater) { .player_wake_enabled = true, .current = { 1, 0 } };
    drifting.boat.velocity = (Vector2) { 1, 0 };
    for (int tick = 0; tick < 480; ++tick)
        boat_session_step(&drifting, (BoatInput) { 0 });
    CHECK(drifting.world.water.player_wake_count == 0);
    // Drive a loop back through the emitted wake, then coast and settle after it passes.
    BoatSession a, b;
    boat_session_init(&a, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat_session_init(&b, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    a.world.water.player_wake_enabled = b.world.water.player_wake_enabled = true;
    float encounter = 0, max_height = 0;
    for (int tick = 0; tick < 6000; ++tick) {
        float throttle = tick < 1440 ? 1 : 0;
        float wheel = tick >= 420 && tick < 1440 ? 1 : 0;
        boat_session_step(&a,
            (BoatInput) { Clamp((wheel - a.boat.wheel) / BOAT_STEP, -1, 1),
                Clamp(
                    (throttle - a.boat.throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1),
                0, 0 });
        boat_session_step(&b,
            (BoatInput) { Clamp((wheel - b.boat.wheel) / BOAT_STEP, -1, 1),
                Clamp(
                    (throttle - b.boat.throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP), -1, 1),
                0, 0 });
        CHECK(a.boat.turn_roll_load == b.boat.turn_roll_load);
        CHECK(a.boat.height == b.boat.height && a.boat.position.x == b.boat.position.x
            && a.boat.roll == b.boat.roll);
        CHECK(fabsf(a.boat.roll) < .7f && fabsf(a.boat.pitch) < .7f && fabsf(a.boat.height) < 1.5f);
        CHECK(Vector2Length(a.boat.velocity) < 23);
        CHECK(fabsf(a.boat.position.x) < BOAT_BASIN_HALF_X
            && fabsf(a.boat.position.y) < BOAT_BASIN_HALF_Z);
        max_height = fmaxf(max_height, fabsf(a.boat.height));
        if (tick > 720 && tick < 1440)
            encounter = fmaxf(encounter,
                fabsf(boat_water_query(a.world.water, a.boat.position, boat_session_time(&a))
                        .height));
    }
    CHECK(encounter > .05f && max_height > .1f);
    CHECK(
        fabsf(a.boat.height) < .003f && fabsf(a.boat.pitch) < .003f && fabsf(a.boat.roll) < .003f);
    CHECK(Vector2Length(a.boat.velocity) < .01f);
    boat_session_init(&a, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    CHECK(a.world.water.player_wake_count == 0 && a.boat.physical_wake_time == 0);
    printf("player_wake=ok encounter=%g max_hull_height=%g\n", encounter, max_height);
    return 0;
}

// Upload the production float payloads derived from double TIME to each probe shader.
static void set_probe_time(Shader slope, Shader height, BoatWater water, double time)
{
    float waves[12], origins[BOAT_WAKE_SEGMENTS * 4], parameters[BOAT_WAKE_SEGMENTS * 4];
    boat_water_wave_uniforms(water, time, waves);
    float clock = boat_water_crossing_clock(water, time);
    int count = boat_water_wake_uniforms(water, time, origins, parameters);
    float amplitude = water.wake_amplitude;
    float offset[] = { -water.wake_offset.x, water.wake_offset.y };
    const Shader shaders[] = { slope, height };
    for (int si = 0; si < 2; ++si) {
        Shader shader = shaders[si];
        SetShaderValueV(
            shader, GetShaderLocation(shader, "waveParameters[0]"), waves, SHADER_UNIFORM_VEC4, 3);
        SetShaderValue(
            shader, GetShaderLocation(shader, "crossingWakeTime"), &clock, SHADER_UNIFORM_FLOAT);
        SetShaderValue(
            shader, GetShaderLocation(shader, "playerWakeCount"), &count, SHADER_UNIFORM_INT);
        if (count) {
            SetShaderValueV(shader, GetShaderLocation(shader, "playerWakeOrigins[0]"), origins,
                SHADER_UNIFORM_VEC4, count);
            SetShaderValueV(shader, GetShaderLocation(shader, "playerWakeParameters[0]"),
                parameters, SHADER_UNIFORM_VEC4, count);
        }
        SetShaderValue(
            shader, GetShaderLocation(shader, "wakeAmplitude"), &amplitude, SHADER_UNIFORM_FLOAT);
        SetShaderValue(
            shader, GetShaderLocation(shader, "wakeOffset"), offset, SHADER_UNIFORM_VEC2);
    }
}

// Unclamped RGBA32F readback: 8-bit targets cannot resolve millimetre phase errors.
static RenderTexture2D float_target(int size)
{
    RenderTexture2D target = { .id = rlLoadFramebuffer(),
        .texture = { .width = size,
            .height = size,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32 } };
    target.texture.id = rlLoadTexture(NULL, size, size, target.texture.format, 1);
    rlFramebufferAttach(
        target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    if (!target.texture.id || !rlFramebufferComplete(target.id)) {
        rlUnloadFramebuffer(target.id);
        rlUnloadTexture(target.texture.id);
        return (RenderTexture2D) { 0 };
    }
    return target;
}

static bool read_center(RenderTexture2D target, float *rgb)
{
    float *pixels = rlReadTexturePixels(
        target.texture.id, target.texture.width, target.texture.height, target.texture.format);
    if (!pixels)
        return false;
    int center = (target.texture.height / 2 * target.texture.width + target.texture.width / 2) * 4;
    memcpy(rgb, pixels + center, 3 * sizeof(float));
    MemFree(pixels);
    return true;
}

typedef struct {
    Shader slope, height;
    Material material;
    Mesh plane;
    float *rest;
    RenderTexture2D target;
    float error; // Largest observed CPU/GPU difference in height or slope.
    int checked;
} GpuProbe;

// Compare one world point: production fragment height/slope and actual vertex displacement.
static int probe_point(GpuProbe *probe, BoatWater water, Vector2 p, double time, float tolerance)
{
    BoatWaterSample q = boat_water_query(water, p, time);
    float world[] = { -p.x, p.y };
    SetShaderValue(
        probe->slope, GetShaderLocation(probe->slope, "probePosition"), world, SHADER_UNIFORM_VEC2);
    BeginTextureMode(probe->target);
    ClearBackground(BLANK);
    BeginShaderMode(probe->slope);
    DrawRectangle(0, 0, probe->target.texture.width, probe->target.texture.height, WHITE);
    EndShaderMode();
    EndTextureMode();
    float actual[3];
    CHECK(read_center(probe->target, actual));
    float values[] = { q.height, q.normal.x / q.normal.y, -q.normal.z / q.normal.y };
    for (int i = 0; i < 3; ++i) {
        float error = fabsf(actual[i] - values[i]);
        probe->error = fmaxf(probe->error, error);
        if (error > tolerance)
            fprintf(stderr, "fragment mismatch x=%g z=%g t=%.17g channel=%d cpu=%g gpu=%g\n", p.x,
                p.y, time, i, values[i], actual[i]);
        CHECK(error <= tolerance);
    }
    Camera3D camera = { .position = { world[0], 10, world[1] },
        .target = { world[0], 0, world[1] },
        .up = { 0, 0, -1 },
        .fovy = .01f,
        .projection = CAMERA_ORTHOGRAPHIC };
    for (int v = 0; v < probe->plane.vertexCount; ++v) {
        probe->plane.vertices[v * 3] = probe->rest[v * 3] + world[0];
        probe->plane.vertices[v * 3 + 2] = probe->rest[v * 3 + 2] + world[1];
    }
    UpdateMeshBuffer(probe->plane, 0, probe->plane.vertices,
        probe->plane.vertexCount * 3 * (int)sizeof(float), 0);
    BeginTextureMode(probe->target);
    ClearBackground(BLANK);
    BeginMode3D(camera);
    DrawMesh(probe->plane, probe->material, MatrixIdentity());
    EndMode3D();
    EndTextureMode();
    CHECK(read_center(probe->target, actual));
    float error = fabsf(actual[0] - q.height);
    probe->error = fmaxf(probe->error, error);
    if (error > tolerance)
        fprintf(stderr, "vertex mismatch x=%g z=%g t=%.17g cpu=%g gpu=%g\n", p.x, p.y, time,
            q.height, actual[0]);
    CHECK(error <= tolerance);
    ++probe->checked;
    return 0;
}

static int probe_grid(GpuProbe *probe, BoatWater water, double time, float tolerance)
{
    set_probe_time(probe->slope, probe->height, water, time);
    for (int x = -16; x <= 16; x += 8)
        for (int z = -24; z <= 16; z += 4)
            CHECK(
                probe_point(probe, water, (Vector2) { (float)x, (float)z }, time, tolerance) == 0);
    return 0;
}

// Compile the production fragment wave/wake functions into a readback probe, then test the
// actual production vertex displacement independently. No copied wave formula in tests.
static int gpu_checks(void)
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(32, 32, "Environment parity");
    CHECK(IsWindowReady());
    char *fragment = LoadFileText("assets/shaders/waterfront_water.fs");
    char *vertex = LoadFileText("assets/shaders/waterfront_water.vs");
    CHECK(fragment && vertex);
    char *main = strstr(fragment, "void main()");
    CHECK(main);
    size_t prefix = (size_t)(main - fragment);
    char *source = malloc(prefix + 512);
    CHECK(source);
    memcpy(source, fragment, prefix);
    strcpy(source + prefix,
        "uniform vec2 probePosition;\nvoid main() { finalColor = vec4("
        "ambientWaves(probePosition, 0.0) + crossingWake(probePosition, crossingWakeTime)"
        " + playerWake(probePosition), 1.0); }");
    GpuProbe probe = { .slope = LoadShaderFromMemory(NULL, source) };
    const char *height_fragment = "#version 330\nin vec3 fragWorldPosition;\n"
                                  "out vec4 finalColor; void main() { "
                                  "finalColor=vec4(fragWorldPosition.y,0,0,1); }";
    probe.height = LoadShaderFromMemory(vertex, height_fragment);
    CHECK(IsShaderValid(probe.slope) && IsShaderValid(probe.height));
    free(source);
    UnloadFileText(fragment);
    UnloadFileText(vertex);
    probe.target = float_target(8);
    CHECK(probe.target.id);
    probe.plane = GenMeshPlane(.01f, .01f, 1, 1);
    probe.rest = malloc((size_t)probe.plane.vertexCount * 3 * sizeof(float));
    CHECK(probe.rest);
    memcpy(probe.rest, probe.plane.vertices, (size_t)probe.plane.vertexCount * 3 * sizeof(float));
    probe.material = LoadMaterialDefault();
    probe.material.shader = probe.height;
    int enabled = 1, patch = 0;
    SetShaderValue(probe.height, GetShaderLocation(probe.height, "physicalWaves"), &enabled,
        SHADER_UNIFORM_INT);
    SetShaderValue(probe.height, GetShaderLocation(probe.height, "waterPatchPass"), &patch,
        SHADER_UNIFORM_INT);
    // Observed 2026-10-23 on Apple M1 Max: wakes <=3.5e-4 and ambient <=6.5e-5 at every epoch.
    // Float shader time would add about 1e-3 m of ambient phase error at 24 hours.
    const float tolerance = 5e-4f, ambient_tolerance = 1.5e-4f;
    // Short-session crossing and player wakes, exercising a full uniform array.
    BoatWater wake = boat_water_test(false, false, true);
    wake.player_wake_enabled = true;
    wake.wake_offset = (Vector2) { 3, -2 };
    const double epochs[] = { 0, 3600, 86400 };
    for (int e = 0; e < 3; ++e) {
        BoatWater water = e ? boat_water_gentle() : wake;
        water.player_wake_enabled = true;
        water.wake_amplitude = wake.wake_amplitude; // Gated off after 24 s at later epochs.
        water.wake_offset = wake.wake_offset;
        // The local packets occupy the last three slots after stale far-away history.
        for (int i = 0; i < BOAT_WAKE_SEGMENTS - 3; ++i)
            boat_water_emit_wake(
                &water, (Vector2) { 500, 500 }, (Vector2) { 508, 500 }, epochs[e] + 4, .4f, 20);
        boat_water_emit_wake(
            &water, (Vector2) { -20, -8 }, (Vector2) { -12, -8 }, epochs[e] + 4, .4f, 20);
        boat_water_emit_wake(
            &water, (Vector2) { -12, -8 }, (Vector2) { -4, -4 }, epochs[e] + 4.4f, .4f, 20);
        boat_water_emit_wake(
            &water, (Vector2) { -4, -4 }, (Vector2) { 4, -4 }, epochs[e] + 4.8f, .4f, 20);
        for (double t = 8; t <= 16; t += 4)
            CHECK(probe_grid(&probe, water, epochs[e] + t, tolerance) == 0);
    }
    // Ambient-only surface immediately around each component's individual phase wrap.
    BoatWater ambient = boat_water_gentle();
    const float speeds[] = { ambient.angular_speed, ambient.crossing[0].angular_speed,
        ambient.crossing[1].angular_speed };
    for (int i = 0; i < 3; ++i)
        for (int e = 1; e < 3; ++e) {
            double n = floor(speeds[i] * epochs[e] / BOAT_TWO_PI);
            double wrap = (2 * n + 1) * (BOAT_TWO_PI / 2) / speeds[i];
            for (int side = -1; side <= 1; side += 2)
                CHECK(probe_grid(&probe, ambient, wrap + side * 1e-4, ambient_tolerance) == 0);
        }
    // Material borrows the shader; release it separately exactly once.
    probe.material.shader = (Shader) { 0 };
    free(probe.rest);
    UnloadMaterial(probe.material);
    UnloadMesh(probe.plane);
    UnloadShader(probe.slope);
    UnloadShader(probe.height);
    rlUnloadFramebuffer(probe.target.id);
    rlUnloadTexture(probe.target.texture.id);
    CloseWindow();
    printf("boat_environment_gpu=ok samples=%d max_error=%.3g\n", probe.checked, probe.error);
    return 0;
}

// Independent double reference for the ambient components; it never uses packed phases.
static double ambient_reference(BoatWater water, Vector2 p, double time)
{
    const BoatWave waves[]
        = { { water.amplitude, water.wavelength, water.angular_speed, water.direction },
              water.crossing[0], water.crossing[1] };
    double height = 0;
    for (int i = 0; i < 3; ++i) {
        BoatWave w = waves[i];
        double k = BOAT_TWO_PI / w.wavelength;
        height += w.amplitude
            * sin(k * ((double)w.direction.x * p.x + (double)w.direction.y * p.y)
                - (double)w.angular_speed * time);
    }
    return height;
}

static int sample_matches(BoatWaterSample a, BoatWaterSample b, float tolerance)
{
    return fabsf(a.height - b.height) <= tolerance
        && Vector3Distance(a.normal, b.normal) <= tolerance
        && Vector3Distance(a.velocity, b.velocity) <= tolerance;
}

// Long-session contract: double time reaches every periodic phase and bounded wake age.
static int clock_checks(void)
{
    BoatWater ambient = boat_water_gentle();
    const double epochs[] = { 0, 10.25, 3600, 3600.37, 86400, 86400.71, 30 * 86400.0 };
    for (int e = 0; e < 7; ++e)
        for (float x = -30; x < 30; x += 7.3f)
            for (float z = -30; z < 30; z += 6.1f) {
                Vector2 p = { x, z };
                double t = epochs[e];
                BoatWaterSample q = boat_water_query(ambient, p, t);
                CHECK(fabs(q.height - ambient_reference(ambient, p, t)) < 1e-5);
                double dt = .001;
                float dy = (float)((boat_water_query(ambient, p, t + dt).height
                                       - boat_water_query(ambient, p, t - dt).height)
                    / (2 * dt));
                float ds = .001f;
                float dx = (boat_water_query(ambient, (Vector2) { x + ds, z }, t).height
                               - boat_water_query(ambient, (Vector2) { x - ds, z }, t).height)
                    / (2 * ds);
                float dz = (boat_water_query(ambient, (Vector2) { x, z + ds }, t).height
                               - boat_water_query(ambient, (Vector2) { x, z - ds }, t).height)
                    / (2 * ds);
                CHECK(fabsf(dy - q.velocity.y) < .002f);
                CHECK(fabsf(dx + q.normal.x / q.normal.y) < .002f);
                CHECK(fabsf(dz + q.normal.z / q.normal.y) < .002f);
            }
    // Control: the former float-time phase misses this reference by about a millimetre.
    float legacy_error = 0;
    for (float x = -30; x < 30; x += 7.3f) {
        Vector2 p = { x, 3 };
        float time = (float)epochs[5], height = 0;
        const BoatWave waves[]
            = { { ambient.amplitude, ambient.wavelength, ambient.angular_speed, ambient.direction },
                  ambient.crossing[0], ambient.crossing[1] };
        for (int i = 0; i < 3; ++i) {
            float k = 2 * PI / waves[i].wavelength;
            height += waves[i].amplitude
                * sinf(
                    k * Vector2DotProduct(waves[i].direction, p) - waves[i].angular_speed * time);
        }
        legacy_error
            = fmaxf(legacy_error, fabsf(height - (float)ambient_reference(ambient, p, epochs[5])));
    }
    CHECK(legacy_error > 2e-4f);
    // Each component's packed phase wraps from +pi to -pi independently; the surface must not.
    const float speeds[] = { ambient.angular_speed, ambient.crossing[0].angular_speed,
        ambient.crossing[1].angular_speed };
    int wraps = 0;
    for (int i = 0; i < 3; ++i)
        for (int e = 0; e < 4; ++e) {
            double epoch = (double[]) { 20, 3600, 86400, 30 * 86400.0 }[e];
            double n = floor(speeds[i] * epoch / BOAT_TWO_PI);
            double wrap = (2 * n + 1) * (BOAT_TWO_PI / 2) / speeds[i];
            CHECK(boat_phase(speeds[i], wrap - 1e-6) > 3.1f);
            CHECK(boat_phase(speeds[i], wrap + 1e-6) < -3.1f);
            for (float x = -12; x <= 12; x += 6) {
                Vector2 p = { x, 5 };
                BoatWaterSample before = boat_water_query(ambient, p, wrap - 1e-6);
                BoatWaterSample after = boat_water_query(ambient, p, wrap + 1e-6);
                CHECK(sample_matches(before, after, 2e-5f));
                for (double d = -1e-3; d <= 1e-3; d += 5e-4)
                    CHECK(fabs(boat_water_query(ambient, p, wrap + d).height
                              - ambient_reference(ambient, p, wrap + d))
                        < 1e-5);
                ++wraps;
            }
        }
    // A recent wake far into a session equals the same short-age wake, ambient disabled.
    for (int e = 2; e < 7; ++e) {
        double epoch = epochs[e];
        BoatWater early = { .player_wake_enabled = true }, late = early;
        const Vector2 path[] = { { -8, 0 }, { 0, 0 }, { 8, 0 }, { 8, 8 } };
        for (int i = 0; i < 3; ++i) {
            boat_water_emit_wake(&early, path[i], path[i + 1], 2 + .4f * i, .4f, 20);
            boat_water_emit_wake(&late, path[i], path[i + 1], epoch + 2 + .4f * i, .4f, 20);
        }
        for (float t = 3; t < 24; t += .71f) {
            for (float x = -12; x < 12; x += 2.3f)
                for (float z = -16; z < 16; z += 2.7f) {
                    BoatWaterSample a = boat_water_query(early, (Vector2) { x, z }, t);
                    BoatWaterSample b = boat_water_query(late, (Vector2) { x, z }, epoch + t);
                    CHECK(sample_matches(a, b, 2e-5f));
                }
            float oa[BOAT_WAKE_SEGMENTS * 4], pa[BOAT_WAKE_SEGMENTS * 4];
            float ob[BOAT_WAKE_SEGMENTS * 4], pb[BOAT_WAKE_SEGMENTS * 4];
            int count = boat_water_wake_uniforms(early, t, oa, pa);
            CHECK((count == 3 || t > 20)
                && boat_water_wake_uniforms(late, epoch + t, ob, pb) == count);
            for (int j = 0; j < count * 4; ++j)
                CHECK(oa[j] == ob[j] && fabsf(pa[j] - pb[j]) < 1e-5f);
        }
        // Expired history and the finite scripted crossing never wrap into recurring events.
        float o[BOAT_WAKE_SEGMENTS * 4], q[BOAT_WAKE_SEGMENTS * 4];
        for (double later = epoch + 30; later < epoch + 2 * 86400; later = later * 1.7 + 11) {
            CHECK(boat_water_wake_uniforms(late, later, o, q) == 0);
            for (float x = -12; x < 12; x += 2.3f)
                CHECK(boat_water_query(late, (Vector2) { x, 4 }, later).height == 0);
        }
    }
    BoatWater crossing = boat_water_test(false, false, true);
    Vector2 source;
    CHECK(boat_water_crossing_clock(crossing, 6) == 6);
    CHECK(boat_water_crossing_clock((BoatWater) { 0 }, 6) == -1);
    const double expired[] = { 24, 24.5, 36, 48, 3606, 86406, 30 * 86400.0 + 6 };
    for (int e = 0; e < 7; ++e) {
        CHECK(boat_water_crossing_clock(crossing, expired[e]) == -1);
        CHECK(!boat_water_wake_source(crossing, expired[e], &source));
        for (float x = -40; x < 40; x += 4)
            for (float z = -20; z < 4; z += 4)
                CHECK(boat_water_query(crossing, (Vector2) { x, z }, expired[e]).height == 0);
    }
    printf(
        "boat_clock_water=ok epochs=%d wraps=%d legacy_24h_error=%.3g\n", 7, wraps, legacy_error);
    return 0;
}

int main(int argc, char **argv)
{
    test_worlds_open();
    if (argc == 2 && strcmp(argv[1], "--gpu") == 0)
        return gpu_checks();
    CHECK(argc == 1);
    CHECK(current_checks() == 0);
    CHECK(wake_checks() == 0);
    CHECK(player_wake_checks() == 0);
    CHECK(clock_checks() == 0);
    puts("boat_environment=ok");
    test_worlds_close();
    return 0;
}
