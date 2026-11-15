#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
/* R0 fixed-source baseline. Diagnostic counters run outside timed query sections. */
#include "external/glad.h"
#include "plugins/boat_plugin_io.h"
#include "plugins/boat_plugin_load.h"
#include "raylib.h"
#include "raymath.h"
#include "render/boat_gpu.h"
#include "support/boat_test_scenery.h"
#include "support/boat_test_session.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "wake research: %s:%d: %s\n", __FILE__, __LINE__, #x);                 \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define WARMUP 1200 /* 20 seconds at 60 frames/s: longer than the full 16-second lifetime. */
#define FRAMES 3600
#define QUERIES                                                                                    \
    30 /* four six-probe groups plus six fixed observations; not actual hull queries.              \
        */

static double milliseconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER frequency, counter;
    REQUIRE(QueryPerformanceFrequency(&frequency) && QueryPerformanceCounter(&counter));
    return (double)counter.QuadPart * 1000 / (double)frequency.QuadPart;
#else
    struct timespec t;
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (double)t.tv_sec * 1000 + t.tv_nsec / 1e6;
#endif
}

typedef struct {
    Vector2 p;
    float speed, wet;
    bool discontinuity;
} Source;
static Source source(const char *path, unsigned tick, unsigned emitter, bool dispersed)
{
    float t = tick / 120.0f, phase = emitter * PI / 2;
    Source s = { .speed = 15, .wet = 1 };
    if (!strcmp(path, "straight") || !strcmp(path, "stop") || !strcmp(path, "dry")
        || !strcmp(path, "teleport") || !strcmp(path, "slow")) {
        if (!strcmp(path, "stop"))
            t = fminf(t, 25);
        if (!strcmp(path, "slow"))
            s.speed = 2;
        s.p = (Vector2) { s.speed * (t - (!strcmp(path, "dry") ? 10 : 25)), 0 };
        if (!strcmp(path, "stop") && tick >= 3000)
            s.speed = 0;
        if (!strcmp(path, "dry") && tick % 2400 >= 1200)
            s.wet = 0;
        if (!strcmp(path, "teleport") && tick >= 3000) {
            s.p.x += 1000;
            s.discontinuity = tick == 3000;
        }
    } else if (!strcmp(path, "eight")) {
        float a = t * .35f + phase;
        s.p = (Vector2) { 30 * sinf(a), 20 * sinf(2 * a) };
        s.speed = hypotf(10.5f * cosf(a), 14 * cosf(2 * a));
    } else {
        float radius = !strcmp(path, "wide") ? 50 : 12;
        float travel = t;
        if (!strcmp(path, "reverse")) {
            float cycle = fmodf(t, 24);
            travel = cycle <= 12 ? cycle : 24 - cycle;
        }
        float a = travel * 15 / radius + phase;
        s.p = (Vector2) { radius * cosf(a), radius * sinf(a) };
    }
    /* Straight emitters rotate through all four compass directions. */
    if (!strcmp(path, "straight") || !strcmp(path, "stop") || !strcmp(path, "dry")
        || !strcmp(path, "teleport") || !strcmp(path, "slow")) {
        float x = s.p.x;
        s.p.x = x * cosf(phase);
        s.p.y = x * sinf(phase);
    }
    if (dispersed)
        s.p.x += 300 * emitter;
    return s;
}

/* Count exact production broad-phase predicates separately, never in measured queries.
 * These are derived counts, not instrumentation inside the production evaluator. */
static unsigned expensive(const BoatWakeObjects *w, Vector2 p)
{
    unsigned count = 0;
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        const BoatWakeObject *o = &w->objects[i];
        if (!o->amplitude)
            continue;
        Vector2 d = Vector2Subtract(p, o->position);
        float u = Vector2DotProduct(d, o->direction);
        if (fabsf(u) >= o->length)
            continue;
        float v = d.x * -o->direction.y + d.y * o->direction.x;
        float age = (float)(w->tick - o->born) / 120 - u * o->inverse_speed;
        float q = sqrtf(v * v + .25f) - .9f - 2.4f * age;
        count += q > -7 && q < 2;
    }
    return count;
}

static uint64_t digest(uint64_t h, const void *data, size_t size)
{
    const unsigned char *p = data;
    while (size--) {
        h ^= *p++;
        h *= UINT64_C(1099511628211);
    }
    return h;
}

int main(int argc, char **argv)
{
    const char *prefix = NULL, *path = "circle", *location = "basin", *clip = NULL,
               *scene_path = NULL;
    unsigned emitters = 4, frames = FRAMES;
    bool dispersed = false, off = false, diagnostics = false, graphics = false, aggregate = false;
    bool high = false, visible = false, overhead = false, traffic = false, restart = false;
    unsigned clip_start = WARMUP;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--output") && i + 1 < argc)
            prefix = argv[++i];
        else if (!strcmp(argv[i], "--path") && i + 1 < argc)
            path = argv[++i];
        else if (!strcmp(argv[i], "--scene") && i + 1 < argc)
            scene_path = argv[++i];
        else if (!strcmp(argv[i], "--location") && i + 1 < argc)
            location = argv[++i];
        else if (!strcmp(argv[i], "--emitters") && i + 1 < argc) {
            const char *n = argv[++i];
            REQUIRE(!strcmp(n, "1") || !strcmp(n, "4"));
            emitters = (unsigned)atoi(n);
        } else if (!strcmp(argv[i], "--clip-start") && i + 1 < argc) {
            char *end;
            unsigned long seconds = strtoul(argv[++i], &end, 10);
            REQUIRE(*argv[i] && !*end && seconds <= 120);
            clip_start = (unsigned)seconds * 60;
        } else if (!strcmp(argv[i], "--traffic"))
            traffic = true;
        else if (!strcmp(argv[i], "--restart"))
            restart = true;
        else if (!strcmp(argv[i], "--clip") && i + 1 < argc)
            clip = argv[++i];
        else if (!strcmp(argv[i], "--dispersed"))
            dispersed = true;
        else if (!strcmp(argv[i], "--off"))
            off = true;
        else if (!strcmp(argv[i], "--aggregate"))
            aggregate = true;
        else if (!strcmp(argv[i], "--diagnostics"))
            diagnostics = true;
        else if (!strcmp(argv[i], "--gpu"))
            graphics = true;
        else if (!strcmp(argv[i], "--high"))
            high = true;
        else if (!strcmp(argv[i], "--visible"))
            visible = graphics = true;
        else if (!strcmp(argv[i], "--overhead"))
            overhead = true;
        else {
            fprintf(stderr,
                "Usage: wake_research --output PREFIX [--path "
                "straight|slow|wide|circle|eight|reverse|stop|dry|teleport] [--emitters 1|4] "
                "[--dispersed] "
                "[--off] [--diagnostics] [--aggregate] [--gpu] [--high] [--location basin|outer] "
                "[--visible] "
                "[--overhead] [--traffic] [--scene VOYAGE_PLUGIN] [--restart] [--clip DIR "
                "--clip-start SECONDS]\n");
            return 2;
        }
    }
    REQUIRE(prefix && strlen(prefix) < 3900);
    REQUIRE(!strcmp(location, "basin") || !strcmp(location, "outer"));
    REQUIRE(!strcmp(path, "straight") || !strcmp(path, "slow") || !strcmp(path, "wide")
        || !strcmp(path, "circle") || !strcmp(path, "eight") || !strcmp(path, "stop")
        || !strcmp(path, "dry") || !strcmp(path, "teleport") || !strcmp(path, "reverse"));
    REQUIRE(!clip || graphics);
    unsigned warmup = clip ? clip_start : WARMUP;
    if (clip)
        frames = 600; /* ten seconds, after saturation; export outside timed sections. */
    SetTraceLogLevel(LOG_WARNING);
    test_scenery_load();
    BoatSession session;
    boat_session_init(&session,
        test_world(!strcmp(location, "outer") ? BOAT_LOCATION_OUTER : BOAT_LOCATION_BASIN),
        (BoatWater) { 0 });
    static BoatPluginVoyage authored;
    if (scene_path) {
        static BoatPluginFiles files;
        BoatPluginDiagnostic d;
        REQUIRE(boat_plugin_read(scene_path, NULL, &files, &d) == BOAT_PLUGIN_OK);
        BoatPluginBundle bundle = boat_plugin_bundle(&files);
        REQUIRE(boat_plugin_load_voyage(&bundle, NULL, &authored, &d) == BOAT_PLUGIN_OK);
        REQUIRE(authored.has_scene);
        session.world.scene = &authored.scene;
    }
    BoatWakeObjects w;
    REQUIRE(boat_wake_objects_init(&w, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
    session.world.water.objects = off ? NULL : &w;
    Vector2 centre = scene_path ? (Vector2) { 0, 185 } : session.boat.position;
    BoatWakeObjects traffic_wakes;
    if (traffic) {
        REQUIRE(waterfront_traffic_wakes_init(&traffic_wakes, 0));
        if (!off)
            session.world.water.traffic_objects = &traffic_wakes;
    }
    BoatGpu gpu = { 0 };
    GLuint timer = 0;
    bool timed_gpu = false;
    if (graphics) {
        SetConfigFlags(visible ? 0 : FLAG_WINDOW_HIDDEN);
        InitWindow(960, 600, "Wake research R0: fixed sources");
        REQUIRE(IsWindowReady() && boat_gpu_init(&gpu, false));
        if (high)
            REQUIRE(boat_gpu_resize(&gpu, 1280, 800));
        if (high)
            REQUIRE(boat_gpu_set_hdr(&gpu, true));
        if (visible)
            SetTargetFPS(60);
        if (glGenQueries && glGetQueryObjectui64v && glGetQueryiv && glBeginQuery && glEndQuery
            && glDeleteQueries) {
            GLint bits = 0;
            glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &bits);
            if (bits) {
                glGenQueries(1, &timer);
                timed_gpu = true;
            }
        }
    }
    char name[4096];
    snprintf(name, sizeof(name), "%s.csv", prefix);
    FILE *out = fopen(name, "w");
    REQUIRE(out);
    fputs("frame,tick,source_ms,query_ms,diagnostic_ms,pack_ms,submit_ms,wait_ms,total_ms,gpu_ms,"
          "live,peak,births,queries,slots_examined,active_examined,expensive_derived,packed_bytes,"
          "height,gradient_x,gradient_z,vertical_velocity,fixed_height,fixed_vertical_velocity,"
          "traffic_live,stamp_pieces,stamp_outside,stamp_texels,stamp_bytes,history_bytes,record_"
          "upload_bytes,max_record_length,field_hash\n",
        out);
    if (clip) {
        Image banner = GenImageColor(1280, 40, BLACK);
        ImageDrawText(&banner,
            TextFormat("BASELINE / %s / %s / FIXED SOURCE", path,
                overhead ? "STATIONARY OVERVIEW" : "CHASE"),
            8, 10, 16, WHITE);
        ImageDrawText(&banner,
            TextFormat("DIAGNOSTICS / %s / %s / FIXED SOURCE", path,
                overhead ? "STATIONARY OVERVIEW" : "CHASE"),
            648, 10, 16, WHITE);
        snprintf(name, sizeof(name), "%s-banner.png", prefix);
        REQUIRE(ExportImage(banner, name));
        UnloadImage(banner);
        banner = GenImageColor(1280, 40, BLACK);
        ImageDrawText(&banner, "BASELINE / HEIGHT RED + BLUE - / OBJECT FOAM", 8, 10, 16, WHITE);
        ImageDrawText(
            &banner, "DIAGNOSTICS / HEIGHT RED + BLUE - / OBJECT FOAM", 648, 10, 16, WHITE);
        snprintf(name, sizeof(name), "%s-field-banner.png", prefix);
        REQUIRE(ExportImage(banner, name));
        UnloadImage(banner);
    }
    float origins[BOAT_OBJECT_CAPACITY * 8], params[BOAT_OBJECT_CAPACITY * 8];
    uint64_t hash = UINT64_C(14695981039346656037);
    unsigned rows = 0, saturated = 0;
    double timer_floor = 1e9;
    for (int i = 0; i < 1000; ++i) {
        double a = milliseconds();
        double b = milliseconds();
        if (b > a)
            timer_floor = fmin(timer_floor, b - a);
    }
    for (unsigned frame = 0; frame < warmup + frames; ++frame) {
        if (visible && WindowShouldClose())
            break;
        double begin = milliseconds();
        uint64_t births = w.emitted;
        Source last = { 0 };
        for (unsigned sub = 0; sub < 2; ++sub) {
            boat_wake_objects_advance(&w);
            for (unsigned e = 0; e < emitters; ++e) {
                Source s = source(path, (unsigned)w.tick, e, dispersed);
                s.p = Vector2Add(s.p, centre);
                if (s.discontinuity && restart)
                    w.emitters[e].initialized = false;
                if (!off)
                    REQUIRE(boat_wake_objects_track(&w, e, s.p, s.speed, s.wet));
                if (e == 0)
                    last = s;
            }
            if (traffic && !off)
                REQUIRE(waterfront_traffic_wakes_step(&traffic_wakes, w.tick / 120.0));
        }
        double stepped = aggregate ? begin : milliseconds();
        Vector2 points[QUERIES];
        BoatWakeObjectSample sample[QUERIES];
        for (unsigned q = 0; q < 24; ++q) {
            unsigned age = 180 + 60 * (q % 3);
            unsigned historic = w.tick > age ? (unsigned)w.tick - age : 1;
            unsigned emitter = (q / 6) % emitters;
            Source s = source(path, historic, emitter, dispersed);
            Source previous = source(path, historic - 1, emitter, dispersed);
            Vector2 tangent = Vector2Normalize(Vector2Subtract(s.p, previous.p));
            Vector2 normal = { -tangent.y, tangent.x };
            float distance = .9f + 2.4f * (age / 120.0f - .25f);
            if (q % 6 >= 3)
                distance = -distance;
            points[q] = Vector2Add(centre, Vector2Add(s.p, Vector2Scale(normal, distance)));
        }
        for (unsigned q = 24; q < QUERIES; ++q)
            points[q] = Vector2Add(centre, (Vector2) { 0, 4.0f + (q - 24) * 4 });
        double query_begin = aggregate ? begin : milliseconds();
        for (unsigned q = 0; q < QUERIES; ++q) {
            sample[q] = boat_wake_objects_query(off ? NULL : &w, points[q]);
            if (traffic && !off) {
                BoatWakeObjectSample extra = boat_wake_objects_query(&traffic_wakes, points[q]);
                sample[q].height += extra.height;
                sample[q].gradient = Vector2Add(sample[q].gradient, extra.gradient);
                sample[q].vertical_velocity += extra.vertical_velocity;
                sample[q].crest_foam += extra.crest_foam;
            }
            REQUIRE(isfinite(sample[q].height) && isfinite(sample[q].gradient.x)
                && isfinite(sample[q].gradient.y) && isfinite(sample[q].vertical_velocity));
        }
        double queried = aggregate ? begin : milliseconds();
        unsigned costly = 0;
        if (diagnostics && !off)
            for (unsigned q = 0; q < QUERIES; ++q)
                costly += expensive(&w, points[q])
                    + (traffic ? expensive(&traffic_wakes, points[q]) : 0);
        double diagnosed = aggregate ? begin : milliseconds();
        int packed = boat_wake_objects_pack(&w, origins, params);
        if (traffic && !off)
            packed += boat_wake_objects_pack(
                &traffic_wakes, origins + packed * 4, params + packed * 4);
        double packed_at = aggregate ? begin : milliseconds();
        double submitted = packed_at, ended = aggregate ? milliseconds() : packed_at, gpu_ms = -1;
        if (graphics) {
            session.boat.position = overhead ? centre : last.p;
            Source previous = source(path, (unsigned)w.tick - 1, 0, dispersed);
            Vector2 delta = Vector2Subtract(last.p, Vector2Add(previous.p, centre));
            session.boat.heading = overhead ? 0 : atan2f(delta.x, -delta.y);
            session.boat.velocity = overhead ? (Vector2) { 0 } : Vector2Scale(delta, 120);
            session.presentation.camera_heading = session.boat.heading;
            boat_session_set_tick(&session, w.tick);
            BoatCamera camera = overhead
                ? (BoatCamera) { .distance = 40, .height = 60, .fixed_fov = true }
                : BOAT_CAMERA_DEFAULT;
            if (timed_gpu)
                glBeginQuery(GL_TIME_ELAPSED, timer);
            boat_gpu_draw(
                &gpu, SESSION_VIEW(&session), &session.presentation, &session.world, camera);
            REQUIRE(!gpu.failed);
            if (timed_gpu)
                glEndQuery(GL_TIME_ELAPSED);
            submitted = aggregate ? begin : milliseconds();
            glFinish();
            ended = milliseconds();
            if (timed_gpu) {
                GLuint64 ns;
                glGetQueryObjectui64v(timer, GL_QUERY_RESULT, &ns);
                gpu_ms = ns / 1e6;
            }
        }
        /* Checksums, output and captures are outside the measured complete workload. */
        hash = digest(hash, origins, (size_t)packed * 4 * sizeof(float));
        hash = digest(hash, params, (size_t)packed * 4 * sizeof(float));
        hash = digest(hash, sample, sizeof(sample));
        float max_length = 0;
        for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i)
            if (w.objects[i].amplitude)
                max_length = fmaxf(max_length, w.objects[i].length);
        if (frame >= warmup) {
            ++rows;
            saturated += w.live == emitters * BOAT_OBJECT_PER_BOAT;
            fprintf(out, "%u,%llu,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%u,%llu,%u,%u,%u,",
                frame - warmup, (unsigned long long)w.tick, stepped - begin, queried - query_begin,
                diagnosed - queried, packed_at - diagnosed, submitted - packed_at,
                ended - submitted, ended - begin, gpu_ms, w.live, w.peak_live,
                (unsigned long long)(w.emitted - births), QUERIES,
                off ? 0 : QUERIES * BOAT_OBJECT_CAPACITY * (traffic ? 2 : 1),
                off ? 0 : QUERIES * (w.live + (traffic ? traffic_wakes.live : 0)));
            if (diagnostics)
                fprintf(out, "%u", costly);
            fprintf(out,
                ",%zu,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%u,%u,%zu,%zu,%zu,%zu,%.9g,%016llx\n",
                (size_t)packed * 32, (double)sample[0].height, (double)sample[0].gradient.x,
                (double)sample[0].gradient.y, (double)sample[0].vertical_velocity,
                (double)sample[24].height, (double)sample[24].vertical_velocity,
                traffic && !off ? traffic_wakes.live : 0, gpu.scene.stats.wake_pieces,
                gpu.scene.stats.wake_outside, gpu.scene.stats.wake_texels,
                graphics ? waterfront_wake_stamp_bytes(&gpu.scene.wake_stamp) : 0,
                graphics ? waterfront_water_history_bytes(&gpu.scene.water_history) : 0,
                graphics ? (size_t)gpu.scene.object_wakes.width * 3 * 4 * sizeof(float) : 0,
                (double)max_length, (unsigned long long)hash);
            if (clip && frame % 6 == 0) {
                Image image = LoadImageFromTexture(gpu.target.texture);
                ImageFlipVertical(&image);
                snprintf(name, sizeof(name), "%s/%04u.png", clip, (frame - warmup) / 6);
                REQUIRE(ExportImage(image, name));
                UnloadImage(image);
                /* World-fixed physical height (left) and object crest foam (right).
                 * Not the cosmetic traffic ribbons or a second mechanics evaluator. */
                Image field = GenImageColor(160, 80, BLACK);
                for (int z = 0; z < 80; ++z)
                    for (int x = 0; x < 80; ++x) {
                        Vector2 p = Vector2Add(centre, (Vector2) { x * 1.5f - 60, z * 1.5f - 60 });
                        BoatWakeObjectSample q = boat_wake_objects_query(off ? NULL : &w, p);
                        unsigned char positive
                            = (unsigned char)(255 * Clamp(q.height / 2.4f, 0, 1));
                        unsigned char negative
                            = (unsigned char)(255 * Clamp(-q.height / 2.4f, 0, 1));
                        unsigned char foam = (unsigned char)(255 * Clamp(q.crest_foam, 0, 1));
                        ImageDrawPixel(&field, x, z, (Color) { positive, 30, negative, 255 });
                        ImageDrawPixel(&field, x + 80, z, (Color) { foam, foam, foam, 255 });
                    }
                snprintf(name, sizeof(name), "%s/field-%04u.png", clip, (frame - warmup) / 6);
                REQUIRE(ExportImage(field, name));
                UnloadImage(field);
            }
        }
        if (visible) {
            BeginDrawing();
            ClearBackground(BLACK);
            DrawTexturePro(gpu.target.texture,
                (Rectangle) {
                    0, 0, (float)gpu.target.texture.width, -(float)gpu.target.texture.height },
                (Rectangle) { 0, 0, 960, 600 }, (Vector2) { 0 }, 0, WHITE);
            DrawText(
                TextFormat("R0 FIXED SOURCE / %s / tick %llu", path, (unsigned long long)w.tick),
                12, 12, 20, WHITE);
            EndDrawing();
        }
    }
    REQUIRE(fclose(out) == 0);
    snprintf(name, sizeof(name), "%s.json", prefix);
    out = fopen(name, "w");
    REQUIRE(out);
    fprintf(out,
        "{\"schema\":1,\"path\":\"%s\",\"location\":\"%s\",\"emitters\":%u,\"dispersed\":%s,"
        "\"off\":%s,\"diagnostics\":%s,\"aggregate\":%s,\"graphics\":%s,\"gpu_timer\":%s,"
        "\"resolution\":\"%s\","
        "\"traffic\":%s,\"restart\":%s,\"overhead\":%s,\"warmup_ticks\":%u,\"measured_frames\":%u,"
        "\"saturated_"
        "frames\":%u,\"timer_min_ms\":%.9g,\"pool_bytes\":%zu,\"packed_capacity_bytes\":%zu,"
        "\"scene_digest\":\"%016llx\",\"hash\":\"%016llx\","
        "\"timing\":\"monotonic wall; synchronous GL completion; excludes "
        "CSV/hash/capture/presentation\",\"unavailable\":[\"isolated CPU render "
        "preparation\",\"advance versus tracking timing split\",\"network and authority "
        "cost\",\"GPU allocation accounting\",\"coupled hull "
        "outcomes (use wake_objects_review)\"]}\n",
        path, location, emitters, dispersed ? "true" : "false", off ? "true" : "false",
        diagnostics ? "true" : "false", aggregate ? "true" : "false", graphics ? "true" : "false",
        timed_gpu ? "true" : "false", high ? "1280x800 HDR" : "320x200", traffic ? "true" : "false",
        restart ? "true" : "false", overhead ? "true" : "false", warmup * 2, rows, saturated,
        timer_floor, sizeof(w) * (traffic ? 2 : 1), sizeof(origins) + sizeof(params),
        (unsigned long long)authored.scene_digest, (unsigned long long)hash);
    REQUIRE(fclose(out) == 0);
    if (graphics) {
        if (timer)
            glDeleteQueries(1, &timer);
        boat_gpu_destroy(&gpu);
        CloseWindow();
    }
    test_worlds_close();
    return rows == frames ? 0 : 1;
}
