/* Complete opt-in workload: real boat/stern queries, field, audit, upload and GL completion. */
#include "boat_gpu.h"
#include "boat_test_session.h"
#include "external/glad.h"
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
static BoatWakeStern recorded[1600][2];

static int compare_time(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void crossing(void)
{
    BoatWakePlayer *world = boat_wake_player_create(1.0 / 120);
    CHECK(world);
    BoatSession emitter;
    boat_session_init(&emitter, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    emitter.world.wake_field = boat_wake_player_field(world);
    emitter.boat.position = (Vector2) { -95, 0 };
    emitter.boat.heading = 1.57079632679f;
    for (unsigned tick = 0; tick < 960; ++tick) {
        BoatWakeStern before
            = boat_wake_stern(&emitter.boat, &emitter.world, boat_session_time(&emitter));
        boat_session_step(&emitter, (BoatInput) { .throttle_rate = 1 });
        CHECK(boat_wake_player_step(world, before,
            boat_wake_stern(&emitter.boat, &emitter.world, boat_session_time(&emitter))));
    }
    BoatWakePlayerReport source = boat_wake_player_report(world);
    printf("actual full-throttle straight: speed %.6g m/s, x %.6g, wet %.6g; "
           "peak %.6g m, slope %.6g; positive %.6g / available %.6g J\n",
        hypot(emitter.boat.velocity.x, emitter.boat.velocity.y), emitter.boat.position.x,
        boat_wake_stern(&emitter.boat, &emitter.world, boat_session_time(&emitter)).wet,
        source.peak_height, source.peak_slope, source.energy.positive_work, source.resistance_work);
    /* Cross the freely propagating trail at right angles. The receiver emits
     * nothing: compare the real buoyancy response with an identical calm twin. */
    BoatSession receiver = emitter;
    receiver.boat.position = (Vector2) { 0, 30 };
    receiver.boat.heading = 0;
    receiver.boat.velocity = (Vector2) { 0, -16 };
    receiver.boat.yaw_velocity = 0;
    BoatSession calm = receiver;
    BoatWakePlayer *flat = boat_wake_player_create(1.0 / 120);
    CHECK(flat);
    /* Keep the candidate's buoyancy/damping path in the calm comparison too. */
    calm.world.wake_field = boat_wake_player_field(flat);
    double height = 0, velocity = 0, pitch = 0, surface = 0;
    unsigned dry_ticks = 0;
    for (unsigned tick = 0; tick < 480; ++tick) {
        boat_session_step(&receiver, (BoatInput) { 0 });
        boat_session_step(&calm, (BoatInput) { 0 });
        CHECK(boat_wake_player_step(world, (BoatWakeStern) { 0 }, (BoatWakeStern) { 0 }));
        height = fmax(height, fabs(receiver.boat.height - calm.boat.height));
        velocity
            = fmax(velocity, fabs(receiver.boat.vertical_velocity - calm.boat.vertical_velocity));
        pitch = fmax(pitch, fabs(receiver.boat.pitch - calm.boat.pitch));
        dry_ticks
            += boat_wake_stern(&receiver.boat, &receiver.world, boat_session_time(&receiver)).wet
            == 0;
        BoatWakeSample q;
        CHECK(boat_wake_query(
            receiver.world.wake_field, receiver.boat.position.x, receiver.boat.position.y, &q));
        surface = fmax(surface, fabs(q.height));
        CHECK(isfinite(receiver.boat.height) && fabs(receiver.boat.height) < 3);
    }
    printf("free-trail crossing: surface %.6g m; hull minus calm peak height %.6g m, "
           "vertical velocity %.6g m/s, pitch %.6g deg, dry stern %u ticks\n",
        surface, height, velocity, pitch * 57.2957795131, dry_ticks);
    for (unsigned tick = 0; tick < 720; ++tick) {
        float neutral = fmaxf(
            -1, fminf(1, -receiver.boat.throttle / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP)));
        boat_session_step(&receiver, (BoatInput) { .throttle_rate = neutral });
        CHECK(boat_wake_player_step(world, (BoatWakeStern) { 0 }, (BoatWakeStern) { 0 }));
        CHECK(isfinite(receiver.boat.height) && fabs(receiver.boat.height) < 3);
        CHECK(isfinite(receiver.boat.roll) && fabs(receiver.boat.roll) < 1.5);
    }
    BoatWakeSample settled;
    CHECK(boat_wake_query(
        receiver.world.wake_field, receiver.boat.position.x, receiver.boat.position.y, &settled));
    printf("neutral recovery after crossing: height relative to water %.6g m, "
           "vertical speed %.6g m/s, pitch %.6g deg\n",
        receiver.boat.height - settled.height, receiver.boat.vertical_velocity,
        receiver.boat.pitch * 57.2957795131);
    CHECK(source.energy.positive_work < source.resistance_work);
    CHECK(hypot(emitter.boat.velocity.x, emitter.boat.velocity.y) > 20);
    CHECK(source.peak_height > .65 && source.peak_height < 1.2);
    CHECK(height > .4 && velocity > 1.5 && dry_ticks > 0);
    CHECK(fabs(receiver.boat.height - settled.height) < .15
        && fabs(receiver.boat.vertical_velocity) < .3);
    boat_wake_player_destroy(flat);
    boat_wake_player_destroy(world);
}

static void step(BoatSession *session, BoatWakePlayer *world, unsigned tick)
{
    const Boat *boat = &session->boat;
    BoatWakeStern before = boat_wake_stern(boat, &session->world, boat_session_time(session));
    float target = tick < 1440 ? 1 : tick < 1800 ? 0 : -.4f;
    float wheel = tick >= 420 && tick < 1440 ? 1 : 0;
    boat_session_step(session,
        (BoatInput) { .steering_rate = fmaxf(-1, fminf(1, (wheel - boat->wheel) / BOAT_STEP)),
            .throttle_rate = fmaxf(-1,
                fminf(1, (target - boat->throttle) / (BOAT_THROTTLE_COMMAND_RATE * BOAT_STEP))) });
    CHECK(tick < 1600);
    recorded[tick][0] = before;
    recorded[tick][1] = boat_wake_stern(boat, &session->world, boat_session_time(session));
    CHECK(boat_wake_player_step(world, before, recorded[tick][1]));
    CHECK(isfinite(boat->height) && fabsf(boat->height) < 3);
    CHECK(isfinite(boat->roll) && fabsf(boat->roll) < 1.5);
}

static void shader_queries(const BoatSession *session, const BoatGpu *gpu)
{
    const Boat *boat = &session->boat;
    const BoatWake *field = session->world.wake_field;
    const char *paths[]
        = { "assets/shaders/waterfront_water.vs", "assets/shaders/waterfront_water.fs" };
    const char *vertex = "#version 330\nvoid main(){vec2 "
                         "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(2*p-1,0,1);}";
    GLuint tex, fb, vao;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, NULL);
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glViewport(0, 0, 1, 1);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    for (unsigned stage = 0; stage < 2; ++stage) {
        char *source = LoadFileText(paths[stage]);
        CHECK(source);
        char *start = strstr(source, "// Shared degree-eleven"),
             *end = strstr(source, "void main()");
        CHECK(start && end && end > start);
        char *code = malloc((size_t)(end - start) + 400);
        CHECK(code);
        sprintf(code,
            "#version 330\nout vec4 color; uniform vec2 probe;\n%.*s\nvoid "
            "main(){color=vec4(wakeFieldSample(probe),1);}\n",
            (int)(end - start), start);
        Shader shader = LoadShaderFromMemory(vertex, code);
        CHECK(shader.id && shader.id != rlGetShaderIdDefault());
        free(code);
        UnloadFileText(source);
        glUseProgram(shader.id);
        glUniform1i(GetShaderLocation(shader, "wakeFieldEnabled"), 1);
        glUniform1i(GetShaderLocation(shader, "wakeField"), 0);
        BoatWakeConfig c = boat_wake_config(field);
        glUniform3f(GetShaderLocation(shader, "wakeGrid"), (float)c.width, (float)c.height,
            (float)c.spacing);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gpu->wake_texture.id);
        double error = 0, norm = 0;
        for (int z = -20; z < 20; z += 3)
            for (int x = -20; x < 20; x += 3) {
                float px = boat->position.x + x + .173f, pz = boat->position.y + z + .391f;
                BoatWakeSample q;
                CHECK(boat_wake_query(field, px, pz, &q));
                glUniform2f(GetShaderLocation(shader, "probe"), -px, pz);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                float actual[4];
                glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, actual);
                double ref[] = { q.height, -q.gradient_x, q.gradient_z };
                for (unsigned j = 0; j < 3; ++j) {
                    error += pow(actual[j] - ref[j], 2);
                    norm += ref[j] * ref[j];
                }
            }
        printf(
            "production %s query L2 %.9g\n", stage ? "FS" : "VS", sqrt(error / fmax(norm, 1e-20)));
        CHECK(sqrt(error / fmax(norm, 1e-20)) < .0001);
        UnloadShader(shader);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glBindVertexArray(0);
    glDeleteVertexArrays(1, &vao);
    glDeleteFramebuffers(1, &fb);
    glDeleteTextures(1, &tex);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--crossing") == 0) {
        crossing();
        return 0;
    }
    bool aa = argc > 1 && strcmp(argv[1], "--gpu-aa") == 0;
    bool graphics = aa || (argc > 1 && strcmp(argv[1], "--gpu") == 0);
    SetTraceLogLevel(LOG_WARNING);
    BoatGpu gpu = { 0 };
    if (graphics) {
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(320, 200, "wake integrated review");
        CHECK(IsWindowReady() && boat_gpu_init(&gpu, aa));
        printf("GPU device: %s; AA %s\n", glGetString(GL_RENDERER), aa ? "1280x800" : "off");
    }
    BoatWakePlayer *world = boat_wake_player_create(1.0 / 120);
    CHECK(world);
    BoatSession boat;
    boat_session_init(&boat, test_world(BOAT_LOCATION_BASIN), (BoatWater) { 0 });
    boat.world.wake_field = boat_wake_player_field(world);
    BoatSession dry = boat;
    dry.boat.height = 10;
    CHECK(boat_wake_stern(&dry.boat, &dry.world, boat_session_time(&dry)).wet == 0);
    CHECK(boat_wake_stern(&boat.boat, &boat.world, boat_session_time(&boat)).wet > .9);
    /* Exercise real transformed stern history, then measure with a nonzero loop field. */
    unsigned tick = 0;
    for (; tick < 1080; ++tick)
        step(&boat, world, tick);
    if (graphics) {
        boat_gpu_draw(
            &gpu, SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT);
        glFinish();
    }
    double physics = 0, render = 0, peak = 0;
    double physics_times[120], total_times[120];
    BoatWakePlayerReport before = boat_wake_player_report(world);
    unsigned frames = graphics ? 120 : 60;
    for (unsigned frame = 0; frame < frames; ++frame) {
        double start = graphics ? GetTime() : (double)clock() / CLOCKS_PER_SEC;
        step(&boat, world, tick++);
        step(&boat, world, tick++);
        double stepped = graphics ? GetTime() : (double)clock() / CLOCKS_PER_SEC;
        physics += stepped - start;
        double rendered = stepped;
        if (graphics) {
            boat_gpu_draw(
                &gpu, SESSION_VIEW(&boat), &boat.presentation, &boat.world, BOAT_CAMERA_DEFAULT);
            glFinish();
            rendered = GetTime();
            render += rendered - stepped;
        }
        physics_times[frame] = 1000 * (stepped - start);
        total_times[frame] = 1000 * (rendered - start);
        BoatWakeSample q;
        CHECK(
            boat_wake_query(boat.world.wake_field, boat.boat.position.x, boat.boat.position.y, &q));
        peak = fmax(peak, fabs(q.height));
    }
    BoatWakePlayerReport r = boat_wake_player_report(world);
    printf("integrated %u frames: physics+source+field+queries+audit %.6f ms/frame; "
           "render+upload+sync %.6f; total %.6f; owned %zu bytes\n",
        frames, physics * 1000 / frames, render * 1000 / frames, (physics + render) * 1000 / frames,
        boat_wake_player_storage(world));
    qsort(physics_times, frames, sizeof(*physics_times), compare_time);
    qsort(total_times, frames, sizeof(*total_times), compare_time);
    unsigned p95 = (95 * frames + 99) / 100 - 1;
    printf("frame distribution: physics p50 %.6f p95 %.6f max %.6f; "
           "total p50 %.6f p95 %.6f max %.6f ms (%s)\n",
        .5 * (physics_times[frames / 2 - 1] + physics_times[frames / 2]), physics_times[p95],
        physics_times[frames - 1], .5 * (total_times[frames / 2 - 1] + total_times[frames / 2]),
        total_times[p95], total_times[frames - 1], graphics ? "wall time" : "process CPU time");
    printf(
        "field source %.6f update+endpoint %.6f audit %.6f ms/frame; retained %.6g J, signed %.6g "
        "(+%.6g / %.6g), absorbed %.6g, residual %.6g, existing drag %.6g; hull height peak %.6g\n",
        (r.source_seconds - before.source_seconds) * 1000 / frames,
        (r.update_seconds - before.update_seconds) * 1000 / frames,
        (r.query_audit_seconds - before.query_audit_seconds) * 1000 / frames, r.energy.retained,
        r.energy.injected, r.energy.positive_work, r.energy.negative_work, r.energy.absorbed,
        r.energy.numerical_residual, r.resistance_work, peak);
    CHECK(
        r.energy.injected > 0 && fabs(r.energy.numerical_residual) < .01 * r.energy.positive_work);
    CHECK(r.energy.positive_work < r.resistance_work);
    printf("nodal peak height %.9g m; slope %.9g\n", r.peak_height, r.peak_slope);
    CHECK(!gpu.failed && isfinite(r.peak_height) && isfinite(r.peak_slope));
    if (!graphics) {
        BoatWakePlayer *fine = boat_wake_player_create(1.0 / 240);
        CHECK(fine);
        for (unsigned i = 0; i < tick; ++i) {
            BoatWakeStern a = recorded[i][0], b = recorded[i][1];
            BoatWakeStern mid = { (a.x + b.x) * .5, (a.z + b.z) * .5, (a.vx + b.vx) * .5,
                (a.vz + b.vz) * .5, (a.wet + b.wet) * .5, (a.resistance + b.resistance) * .5,
                (a.drag_power + b.drag_power) * .5 };
            CHECK(boat_wake_player_step(fine, a, mid) && boat_wake_player_step(fine, mid, b));
        }
        double err = 0, norm = 0;
        for (int z = -40; z <= 20; z += 2)
            for (int x = -20; x <= 40; x += 2) {
                BoatWakeSample a, b;
                CHECK(boat_wake_query(boat.world.wake_field, x + .17, z + .39, &a));
                CHECK(boat_wake_query(boat_wake_player_field(fine), x + .17, z + .39, &b));
                err += pow(a.height - b.height, 2)
                    + pow(a.vertical_velocity - b.vertical_velocity, 2)
                    + pow(a.velocity_x - b.velocity_x, 2) + pow(a.velocity_z - b.velocity_z, 2);
                norm += b.height * b.height + b.vertical_velocity * b.vertical_velocity
                    + b.velocity_x * b.velocity_x + b.velocity_z * b.velocity_z;
            }
        double work = fabs(r.energy.injected - boat_wake_player_report(fine).energy.injected)
            / r.energy.positive_work;
        printf("actual fixed-tick stern resampling: combined query L2 %.9g, signed work %.9g\n",
            sqrt(err / norm), work);
        CHECK(sqrt(err / norm) < .01 && work < .01);
        boat_wake_player_destroy(fine);
    }
    if (graphics) {
        Image image = LoadImageFromTexture(gpu.target.texture);
        ImageFlipVertical(&image);
        CHECK(ExportImage(image, "build/wake-play-review.png"));
        UnloadImage(image);
        BoatSession flat = boat;
        flat.world.wake_field = NULL;
        /* Remove the candidate mesh for a conservative rest-water renderer baseline. */
        gpu.scene.near_water = (Mesh) { 0 };
        double start = GetTime();
        for (unsigned i = 0; i < 60; ++i) {
            boat_gpu_draw(
                &gpu, SESSION_VIEW(&flat), &flat.presentation, &flat.world, BOAT_CAMERA_DEFAULT);
            glFinish();
        }
        printf(
            "rest-water full renderer %.6f ms/frame (same pose; excludes wake mesh and upload)\n",
            1000 * (GetTime() - start) / 60);
        shader_queries(&boat, &gpu);
        CHECK(glGetError() == GL_NO_ERROR);
        boat_gpu_destroy(&gpu);
        CloseWindow();
    }
    double retained = r.energy.retained;
    boat_wake_player_reset(world);
    CHECK(boat_wake_player_report(world).energy.retired == retained);
    boat_wake_player_destroy(world);
    test_worlds_close();
    return 0;
}
