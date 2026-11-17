#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "wake_capture.h"
#include <float.h>
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

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "capture %s:%d: %s\n", __FILE__, __LINE__, #x);                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void tick(R2Capture *c, Vector2 p, float speed, float wet, uint64_t identity)
{
    CHECK(r2_capture_advance(c));
    CHECK(r2_capture_track(c, 0, identity, (R2Source) { p, speed, wet }));
    CHECK(r2_capture_valid(c));
}

static void span(R2Capture *c, Vector2 end)
{
    Vector2 start = c->field.emitters[0].start;
    for (unsigned i = 1; i <= BOAT_OBJECT_EMIT_TICKS; ++i) {
        float t = i / (float)BOAT_OBJECT_EMIT_TICKS;
        tick(c, (Vector2) { start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t }, 15,
            1, 1);
    }
}

static R2Capture initial(void)
{
    R2Capture c;
    r2_capture_init(&c);
    CHECK(r2_capture_track(&c, 0, 1, (R2Source) { { 0, 0 }, 15, 1 }));
    CHECK(r2_capture_valid(&c));
    return c;
}

static void continuous(void)
{
    const char *paths[] = { "straight", "circle", "wide", "eight", "reverse" };
    for (unsigned p = 0; p < 5; ++p) {
        R2Capture c, replay;
        R2Field baseline;
        R2CaptureSnapshot snapshot;
        r2_capture_init(&c);
        r2_init(&baseline, R2_BASELINE);
        for (unsigned t = 0; t <= 9600; ++t) {
            if (t) {
                CHECK(r2_capture_advance(&c));
                r2_advance(&baseline);
            }
            for (unsigned e = 0; e < 4; ++e) {
                R2Source s = r2_source(paths[p], t, e, false);
                CHECK(r2_capture_track(&c, e, e + 1, s));
                CHECK(r2_track(&baseline, e, s));
            }
            CHECK(!memcmp(&c.field, &baseline, sizeof(baseline)));
            CHECK(r2_capture_valid(&c));
            if (t == 3000) {
                CHECK(r2_capture_snapshot(&c, &snapshot));
                CHECK(r2_capture_restore(&replay, &snapshot));
            } else if (t > 3000) {
                CHECK(r2_capture_advance(&replay));
                for (unsigned e = 0; e < 4; ++e)
                    CHECK(r2_capture_track(&replay, e, e + 1, r2_source(paths[p], t, e, false)));
                CHECK(!memcmp(&c, &replay, sizeof(c)));
            }
        }
        CHECK(c.field.peak == BOAT_OBJECT_CAPACITY);
    }
}

static void baseline_restarts(void)
{
    R2Capture c;
    R2Field baseline;
    r2_capture_init(&c);
    r2_init(&baseline, R2_BASELINE);
    unsigned ineligible = 0;
    for (unsigned t = 0; t <= 3000; ++t) {
        if (t) {
            CHECK(r2_capture_advance(&c));
            r2_advance(&baseline);
        }
        // Include the exact dry-to-wet carry-in and slow-to-fast source transitions.
        R2Source s = r2_source("circle", t, 0, false);
        if (t >= 100 && t <= 105)
            s.wet = 0;
        if (t >= 400 && t <= 405)
            s.speed = 0;
        if (t == 600)
            continue;
        CHECK(r2_track(&baseline, 0, s));
        CHECK(r2_capture_track(&c, 0, 1, s));
        CHECK(!memcmp(&c.field, &baseline, sizeof(baseline)));
        CHECK(r2_capture_valid(&c));
        if (t == 165 || t == 465) {
            unsigned slot = (c.field.emitters[0].next + 31) % 32;
            CHECK(!c.descriptors[slot].eligible && !c.descriptors[slot].paired);
            CHECK(c.descriptors[slot].curvature == 0);
            ++ineligible;
        }
    }
    CHECK(ineligible == 2);
}

static void gates(void)
{
    const float xs[] = { .001f, 0, -.001f };
    for (unsigned i = 0; i < 3; ++i) {
        R2Capture c = initial();
        span(&c, (Vector2) { 4, 0 });
        CHECK(!c.descriptors[0].paired && c.descriptors[0].curvature == 0);
        span(&c, (Vector2) { 4 + xs[i], 4 });
        CHECK(c.descriptors[1].paired);
        CHECK(i ? c.descriptors[1].curvature == 0 : c.descriptors[1].curvature > 0);
        // A domain rejection does not discard this otherwise valid predecessor.
        span(&c, (Vector2) { 4 + 2 * xs[i], 8 });
        CHECK(c.descriptors[2].paired && fabsf(c.descriptors[2].curvature) < 1e-6f);
    }
    R2Capture c = initial();
    span(&c, (Vector2) { 4, 0 });
    span(&c, (Vector2) { 4, -4 });
    span(&c, (Vector2) { 2, -6 });
    CHECK(c.descriptors[2].curvature < 0);
    R2Descriptor frozen = c.descriptors[2];
    span(&c, (Vector2) { 2, 2 });
    CHECK(!memcmp(&frozen, &c.descriptors[2], sizeof(frozen)));
    CHECK(r2_capture_restart(&c, 0));
    CHECK(!memcmp(&frozen, &c.descriptors[2], sizeof(frozen)));
    tick(&c, (Vector2) { 100, 100 }, 15, 1, 1);
    span(&c, (Vector2) { 104, 100 });
    CHECK(!c.descriptors[4].paired && c.descriptors[4].curvature == 0);

    c = initial();
    span(&c, (Vector2) { 4, 0 });
    float heading = 0;
    Vector2 end = { 4, 0 };
    for (unsigned i = 0; i < 10; ++i) {
        heading += 1.5707963267948966f + (i % 2 ? .001f : -.001f);
        end.x += 4 * cosf(heading);
        end.y += 4 * sinf(heading);
        span(&c, end);
        CHECK(c.descriptors[i + 1].paired);
        CHECK(i % 2 ? c.descriptors[i + 1].curvature == 0 : c.descriptors[i + 1].curvature > 0);
    }

    r2_capture_init(&c);
    for (unsigned t = 0; t <= 600; ++t) {
        if (t)
            CHECK(r2_capture_advance(&c));
        CHECK(r2_capture_track(&c, 0, 1, r2_source("circle", t, 0, false)));
    }
    for (unsigned i = 1; i < 10; ++i)
        CHECK(fabsf(c.descriptors[i].curvature - 1.0f / 12) < 1e-6f);
}

static void breaks(void)
{
    for (unsigned mode = 0; mode < 7; ++mode) {
        R2Capture c = initial();
        span(&c, (Vector2) { 4, 0 });
        span(&c, (Vector2) { 6, 2 });
        CHECK(c.descriptors[1].curvature > 0);
        R2Descriptor frozen = c.descriptors[1];
        if (mode == 0)
            tick(&c, (Vector2) { 6, 2 }, 15, 0, 1);
        else if (mode == 1)
            tick(&c, (Vector2) { 6, 2 }, 0, 1, 1);
        else if (mode == 2)
            CHECK(r2_capture_advance(&c)); // Missed track.
        else if (mode == 3)
            CHECK(r2_capture_restart(&c, 0));
        else if (mode == 4) {
            CHECK(r2_capture_advance(&c));
            CHECK(!r2_capture_track(&c, 0, 1, (R2Source) { { NAN, 0 }, 15, 1 }));
        } else if (mode == 5)
            span(&c, (Vector2) { 6, 2 }); // Omitted zero-length span.
        uint64_t id = mode == 6 ? 2 : 1;
        tick(&c, (Vector2) { 6, 2 }, 15, 1, id);
        unsigned slot = c.field.emitters[0].next;
        for (unsigned t = 1; t <= 60; ++t)
            tick(&c, (Vector2) { 6 + t / 15.0f, 2 }, 15, 1, id);
        CHECK(c.descriptors[slot].span.serial);
        CHECK(!c.descriptors[slot].paired && c.descriptors[slot].curvature == 0);
        CHECK(!memcmp(&frozen, &c.descriptors[1], sizeof(frozen)));
        R2Capture unchanged;
        memcpy(&unchanged, &c, sizeof(c));
        CHECK(!r2_capture_track(&c, 0, id + 1, (R2Source) { { 0, 0 }, 15, 1 }));
        CHECK(!memcmp(&unchanged, &c, sizeof(c)));
    }
}

static void lifecycle(void)
{
    R2Capture c = initial(), restored;
    span(&c, (Vector2) { 4, 0 });
    span(&c, (Vector2) { 6, 2 });
    R2Descriptor child = c.descriptors[1];
    while (c.field.tick < 1980)
        CHECK(r2_capture_advance(&c));
    CHECK(!c.descriptors[0].span.serial);
    CHECK(!memcmp(&child, &c.descriptors[1], sizeof(child)));
    R2CaptureSnapshot snapshot;
    CHECK(r2_capture_snapshot(&c, &snapshot));
    CHECK(r2_capture_restore(&restored, &snapshot));
    CHECK(!memcmp(&restored, &c, sizeof(c)));
    R2Capture bad;
    memcpy(&bad, &c, sizeof(c));
    bad.descriptors[1].curvature = NAN;
    CHECK(!r2_capture_valid(&bad));
    memcpy(&bad, &c, sizeof(c));
    memset(&bad.descriptors[1], 0, sizeof(R2Descriptor));
    CHECK(!r2_capture_valid(&bad));
    snapshot.capture.descriptors[1].curvature += .01f;
    CHECK(!r2_capture_restore(&restored, &snapshot));
    CHECK(!memcmp(&restored, &c, sizeof(c)));
    CHECK(r2_capture_snapshot(&c, &snapshot));
    snapshot.version = 2;
    CHECK(!r2_capture_restore(&restored, &snapshot));
    // Parent slot reuse with uninterrupted emission while a child is still alive.
    c = initial();
    for (unsigned t = 1; t <= 1980; ++t) {
        R2Source s = r2_source("circle", t, 0, false);
        tick(&c, s.position, s.speed, s.wet, 1);
        if (t == 120)
            child = c.descriptors[1];
    }
    CHECK(c.descriptors[0].span.serial == 33);
    CHECK(!memcmp(&child, &c.descriptors[1], sizeof(child)));
    CHECK(r2_capture_snapshot(&c, &snapshot));
    CHECK(r2_capture_restore(&restored, &snapshot));
}

static void invalid_inputs(void)
{
    R2Capture c = initial();
    span(&c, (Vector2) { 4, 0 });
    for (unsigned i = 0; i < 59; ++i)
        tick(&c, (Vector2) { 4, 0 }, 15, 1, 1);
    CHECK(r2_capture_advance(&c));
    uint64_t births = c.field.births;
    CHECK(!r2_capture_track(&c, 0, 1, (R2Source) { { FLT_MAX, FLT_MAX }, 15, 1 }));
    CHECK(c.field.births == births && !c.sources[0].has_previous);
    CHECK(r2_capture_valid(&c));
    tick(&c, (Vector2) { 0, 0 }, 15, 1, 1);
    span(&c, (Vector2) { 4, 0 });
    CHECK(!c.descriptors[1].paired);
    R2Capture bad;
    memcpy(&bad, &c, sizeof(c));
    bad.field.records[0].position.x = INFINITY;
    CHECK(!r2_capture_valid(&bad));
    memcpy(&bad, &c, sizeof(c));
    bad.descriptors[1].span.serial = bad.descriptors[0].span.serial;
    CHECK(!r2_capture_valid(&bad));
    memcpy(&bad, &c, sizeof(c));
    bad.sources[0].previous.serial = bad.descriptors[0].span.serial;
    CHECK(!r2_capture_valid(&bad));
    CHECK(r2_capture_advance(&c));
    CHECK(!r2_capture_track(&c, 0, 0, (R2Source) { { 4, 0 }, 15, 1 }));
    CHECK(!c.sources[0].has_previous);
    CHECK(r2_capture_track(&c, 0, 1, (R2Source) { { 4, 0 }, 15, 1 }));
    span(&c, (Vector2) { 8, 0 });
    CHECK(!c.descriptors[2].paired);
}

static double milliseconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    CHECK(QueryPerformanceFrequency(&f) && QueryPerformanceCounter(&c));
    return (double)c.QuadPart * 1000 / (double)f.QuadPart;
#else
    struct timespec t;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
#endif
}

static void benchmark(void)
{
    puts("repetition,emitters,mode,frame,tracking_ms,query_ms,enclosing_ms,live,births,height");
    for (unsigned count = 1; count <= 4; count += 3)
        for (unsigned repeat = 0; repeat < 3; ++repeat)
            for (unsigned mode = 0; mode < 2; ++mode) {
                R2Capture c;
                R2Field baseline;
                r2_capture_init(&c);
                r2_init(&baseline, R2_BASELINE);
                for (unsigned frame = 0; frame < 4800; ++frame) {
                    double begin = milliseconds();
                    for (unsigned sub = 0; sub < 2; ++sub) {
                        R2Field *field = mode ? &c.field : &baseline;
                        if (mode)
                            CHECK(r2_capture_advance(&c));
                        else
                            r2_advance(field);
                        for (unsigned e = 0; e < count; ++e) {
                            R2Source s = r2_source("circle", (unsigned)field->tick, e, false);
                            if (mode)
                                CHECK(r2_capture_track(&c, e, e + 1, s));
                            else
                                CHECK(r2_track(field, e, s));
                        }
                    }
                    double tracked = milliseconds();
                    const R2Field *field = mode ? &c.field : &baseline;
                    double height = 0;
                    for (unsigned q = 0; q < 30; ++q) {
                        Vector2 point = { -20 + q * 1.3f, -12 + q * .9f };
                        height += r2_query(field, point, field->tick / 120.0).height;
                    }
                    double end = milliseconds();
                    if (frame >= 1200) {
                        CHECK(field->live == count * BOAT_OBJECT_PER_BOAT);
                        printf("%u,%u,%s,%u,%.9f,%.9f,%.9f,%u,%llu,%.12g\n", repeat, count,
                            mode ? "capture" : "baseline", frame - 1200, tracked - begin,
                            end - tracked, end - begin, field->live,
                            (unsigned long long)field->births, height);
                    }
                }
                CHECK(!mode || r2_capture_valid(&c));
            }
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--bench")) {
        benchmark();
        return 0;
    }
    CHECK(argc == 1);
    continuous();
    baseline_restarts();
    gates();
    breaks();
    lifecycle();
    invalid_inputs();
    printf("R2b capture contract passed; field=%zu descriptor=%zu sidecars=%zu source=%zu "
           "sources=%zu capture=%zu snapshot=%zu bytes\n",
        sizeof(R2Field), sizeof(R2Descriptor), sizeof(((R2Capture *)0)->descriptors),
        sizeof(R2CaptureSource), sizeof(((R2Capture *)0)->sources), sizeof(R2Capture),
        sizeof(R2CaptureSnapshot));
    return 0;
}
