#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "raylib.h"
#include "raymath.h"
#include "wake_transport.h"
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
            fprintf(stderr, "R2 %s:%d: %s\n", __FILE__, __LINE__, #x);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

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

static FILE *output(const char *root, const char *name)
{
    char path[4096];
    CHECK(snprintf(path, sizeof(path), "%s/%s", root, name) < (int)sizeof(path));
    FILE *f = fopen(path, "w");
    CHECK(f);
    return f;
}

typedef struct {
    R2Capture ledger;
    R2Mode mode;
} Experiment;
static const char *mode_name(R2Mode mode) { return mode ? "spread" : "baseline"; }
static void init(Experiment *w, R2Mode mode)
{
    r2_capture_init(&w->ledger);
    w->mode = mode;
}
static void advance(Experiment *w) { CHECK(r2_capture_advance(&w->ledger)); }
static bool track(Experiment *w, unsigned e, R2Source s)
{
    return r2_capture_track(&w->ledger, e, e + 1, s);
}
static BoatWakeObjectSample evaluate(const Experiment *w, Vector2 p, double t)
{
    return w->mode ? r2_transport_query(&w->ledger, p, t) : r2_query(&w->ledger.field, p, t);
}
static Experiment seed(R2Mode mode, const char *path, unsigned ticks, unsigned count)
{
    Experiment w;
    init(&w, mode);
    for (unsigned t = 0; t <= ticks; ++t) {
        if (t)
            advance(&w);
        for (unsigned e = 0; e < count; ++e)
            CHECK(track(&w, e, r2_source(path, t, e, false)));
    }
    CHECK(r2_capture_valid(&w.ledger));
    return w;
}
static void straight(FILE *out, FILE *traces)
{
    fputs("mode,after_stop_seconds,samples,height_normalized_rms,peak_error_normalized,gradient_"
          "normalized_rms,velocity_normalized_rms,height_scale,gradient_scale,velocity_scale,"
          "interior_rms,interior_peak_error,interior_samples\n",
        out);
    fputs("mode,tick,height,gradient_x,gradient_z,vertical_velocity\n", traces);
    Experiment ref = seed(R2_BASELINE, "straight", 3000, 1);
    const unsigned ages[] = { 1, 4, 8, 12, 15 };
    for (R2Mode mode = 0; mode < 2; ++mode) {
        Experiment w = seed(mode, "straight", 3000, 1);
        for (unsigned ai = 0; ai < 5; ++ai) {
            double h2 = 0, dh2 = 0, g2 = 0, dg2 = 0, v2 = 0, dv2 = 0;
            float peak = 0, maximum_error = 0, interior_peak = 0, interior_error = 0;
            double interior_h2 = 0, interior_dh2 = 0;
            unsigned count = 0, interior_count = 0;
            double time = 25 + ages[ai];
            for (unsigned i = 0; i < 600; ++i) {
                float source_t = 8.5f + 17 * (i + .5f) / 600;
                float age = (float)time - source_t - .25f;
                float radius = .9f + 2.4f * age;
                for (unsigned j = 0; j < 90; ++j) {
                    Vector2 p = { 15 * (source_t - 25), radius - 7 + .1f * j };
                    BoatWakeObjectSample a = evaluate(&ref, p, time), b = evaluate(&w, p, time);
                    h2 += a.height * a.height;
                    dh2 += (b.height - a.height) * (b.height - a.height);
                    g2 += Vector2LengthSqr(a.gradient);
                    dg2 += Vector2DistanceSqr(a.gradient, b.gradient);
                    v2 += a.vertical_velocity * a.vertical_velocity;
                    dv2 += (b.vertical_velocity - a.vertical_velocity)
                        * (b.vertical_velocity - a.vertical_velocity);
                    peak = fmaxf(peak, fabsf(a.height));
                    maximum_error = fmaxf(maximum_error, fabsf(b.height - a.height));
                    if (source_t >= 10.5f && source_t <= 24.5f) {
                        interior_h2 += a.height * a.height;
                        interior_dh2 += (b.height - a.height) * (b.height - a.height);
                        interior_peak = fmaxf(interior_peak, fabsf(a.height));
                        interior_error = fmaxf(interior_error, fabsf(b.height - a.height));
                        ++interior_count;
                    }
                    ++count;
                }
            }
            CHECK(h2 > 0 && g2 > 0 && v2 > 0 && peak > 0);
            fprintf(out, "%s,%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u\n",
                mode_name(mode), ages[ai], count, sqrt(dh2 / h2), maximum_error / (double)peak,
                sqrt(dg2 / g2), sqrt(dv2 / v2), sqrt(h2 / count), sqrt(g2 / count),
                sqrt(v2 / count), sqrt(interior_dh2 / interior_h2),
                interior_error / (double)interior_peak, interior_count);
        }
        for (unsigned tick = 3000; tick <= 4920; ++tick) {
            BoatWakeObjectSample s = evaluate(&w, (Vector2) { -30, 10 }, tick / 120.0);
            fprintf(traces, "%s,%u,%.9g,%.9g,%.9g,%.9g\n", mode_name(mode), tick, (double)s.height,
                (double)s.gradient.x, (double)s.gradient.y, (double)s.vertical_velocity);
        }
    }
}

static BoatWakeObjectSample pair(const Experiment *w, Vector2 point, double time, float source_time)
{
    unsigned best[2] = { 0, 0 };
    float distances[2] = { 1e9f, 1e9f };
    float half = .25f;
    for (unsigned i = 0; i < R2_SLOTS; ++i)
        if (w->ledger.field.records[i].amplitude) {
            float distance = fabsf(w->ledger.field.records[i].born / 120.0f - half - source_time);
            if (distance < distances[0]) {
                distances[1] = distances[0];
                best[1] = best[0];
                distances[0] = distance;
                best[0] = i;
            } else if (distance < distances[1]) {
                distances[1] = distance;
                best[1] = i;
            }
        }
    BoatWakeObjectSample a = r2_transport_piece(&w->ledger.field.records[best[0]],
        w->mode ? w->ledger.descriptors[best[0]].curvature : 0, point, time);
    BoatWakeObjectSample b = distances[1] < 1e9f
        ? r2_transport_piece(&w->ledger.field.records[best[1]],
              w->mode ? w->ledger.descriptors[best[1]].curvature : 0, point, time)
        : (BoatWakeObjectSample) { 0 };
    a.height += b.height;
    return a;
}

static Experiment turn_seed(R2Mode mode, float radius, unsigned ticks, bool line)
{
    Experiment w;
    init(&w, mode);
    for (unsigned tick = 0; tick <= ticks; ++tick) {
        if (tick)
            advance(&w);
        float t = tick / 120.0f, a = 15 * t / radius;
        R2Source s = { .position
            = line ? (Vector2) { 15 * t, 0 } : (Vector2) { radius * cosf(a), radius * sinf(a) },
            .speed = 15,
            .wet = 1 };
        CHECK(track(&w, 0, s));
    }
    return w;
}

static void turns(FILE *out)
{
    fputs("mode,radius,after_stop_seconds,side,station_step,radial_step,corridor,eligible,source_"
          "length,expanded_length,uncovered10,uncovered25,uncovered50,longest_gap25,min_ratio,max_"
          "ratio,max_ridge_displacement,local_pair_min_ratio,local_pair_max_ratio,max_gradient,max_"
          "vertical_speed\n",
        out);
    const float radii[] = { 12, 50 }, grids[] = { .1f, .05f, .025f };
    const unsigned ages[] = { 1, 4, 8, 12, 15 };
    for (R2Mode mode = 0; mode < 2; ++mode)
        for (unsigned ri = 0; ri < 2; ++ri) {
            float radius = radii[ri];
            unsigned ticks = (unsigned)(PI / 2 * radius / 15 * 120) / 60 * 60;
            Experiment w = turn_seed(mode, radius, ticks, false),
                       line = turn_seed(mode, radius, ticks, true);
            for (unsigned gi = 0; gi < 3; ++gi)
                for (unsigned ai = 0; ai < 5; ++ai)
                    for (unsigned corridor_index = 0; corridor_index < 2; ++corridor_index)
                        for (int side = -1; side <= 1; side += 2) {
                            if (corridor_index && gi != 1)
                                continue;
                            float corridor = corridor_index ? 6 : 2;
                            double time = ticks / 120.0 + ages[ai];
                            float begin = .25f, end = ticks / 120.0f - .25f;
                            unsigned samples = (unsigned)ceilf(15 * (end - begin) / grids[gi]);
                            float dt = (end - begin) / samples, ds = dt * 15;
                            unsigned eligible = 0;
                            float uncovered[3] = { 0 }, expanded = 0, run = 0, longest = 0;
                            float minimum = 1e9f, maximum = 0, displacement = 0, pair_min = 1e9f,
                                  pair_max = 0, gradient_max = 0, velocity_max = 0;
                            for (unsigned i = 0; i < samples; ++i) {
                                float t = begin + (i + .5f) * dt, angle = 15 * t / radius;
                                // Each control's own phase convention is used only in this geometry
                                // ratio; straight/arrival screens separately compare against
                                // baseline.
                                float age = (float)time - t - (.25f);
                                float distance = .9f + 2.4f * age;
                                if (side < 0 && radius - distance <= fmaxf(2, corridor)) {
                                    run = 0;
                                    continue;
                                }
                                float a = 0, b = 0, pa = 0, pb = 0, offset_best = 0;
                                int radial = (int)ceilf(corridor / grids[gi]);
                                for (int j = -radial; j <= radial; ++j) {
                                    float offset = j * grids[gi];
                                    Vector2 p
                                        = { (radius + side * (distance + offset)) * cosf(angle),
                                              (radius + side * (distance + offset)) * sinf(angle) };
                                    Vector2 q = { 15 * t, side * (distance + offset) };
                                    BoatWakeObjectSample surface = evaluate(&w, p, time);
                                    float h = surface.height;
                                    gradient_max
                                        = fmaxf(gradient_max, Vector2Length(surface.gradient));
                                    velocity_max
                                        = fmaxf(velocity_max, fabsf(surface.vertical_velocity));
                                    if (h > a) {
                                        a = h;
                                        offset_best = offset;
                                    }
                                    b = fmaxf(b, r2_query(&line.ledger.field, q, time).height);
                                    // Local-pair attribution at the expected ridge, not a wide
                                    // search that could accidentally choose an independent
                                    // neighboring train.
                                    if (j == 0) {
                                        pa = pair(&w, p, time, t).height;
                                        pb = pair(&line, q, time, t).height;
                                    }
                                }
                                if (b < .05f) {
                                    run = 0;
                                    continue;
                                }
                                float ratio = a / b;
                                ++eligible;
                                expanded
                                    += dt * hypotf((radius + side * distance) * 15 / radius, 2.4f);
                                minimum = fminf(minimum, ratio);
                                maximum = fmaxf(maximum, ratio);
                                displacement = fmaxf(displacement, fabsf(offset_best));
                                uncovered[0] += ratio < .1f ? ds : 0;
                                uncovered[1] += ratio < .25f ? ds : 0;
                                uncovered[2] += ratio < .5f ? ds : 0;
                                run = ratio < .25f ? run + ds : 0;
                                longest = fmaxf(longest, run);
                                if (pb > .05f) {
                                    pair_min = fminf(pair_min, pa / pb);
                                    pair_max = fmaxf(pair_max, pa / pb);
                                }
                            }
                            fprintf(out,
                                "%s,%.9g,%u,%d,%.9g,%.9g,%.9g,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,",
                                mode_name(mode), (double)radius, ages[ai], side, (double)ds,
                                (double)grids[gi], (double)corridor, eligible,
                                (double)(eligible * ds), (double)expanded, (double)uncovered[0],
                                (double)uncovered[1], (double)uncovered[2], (double)longest);
                            if (eligible)
                                fprintf(out, "%.9g,%.9g,%.9g,", (double)minimum, (double)maximum,
                                    (double)displacement);
                            else
                                fputs(",,,", out);
                            if (pair_min < 1e9f)
                                fprintf(out, "%.9g,%.9g", (double)pair_min, (double)pair_max);
                            else
                                fputc(',', out);
                            fprintf(
                                out, ",%.9g,%.9g\n", (double)gradient_max, (double)velocity_max);
                        }
        }
}

static bool valid_path(const char *path)
{
    const char *paths[] = { "straight", "wide", "circle", "eight", "reverse", "crossing" };
    for (unsigned i = 0; i < 6; ++i)
        if (!strcmp(path, paths[i]))
            return true;
    return false;
}

static unsigned index_argument(const char *text, unsigned maximum)
{
    char *end;
    unsigned long n = strtoul(text, &end, 10);
    CHECK(*text && !*end && n < maximum);
    return (unsigned)n;
}

static void bench(
    const char *path, R2Mode mode, unsigned emitters, bool dispersed, const char *prefix)
{
    Experiment w;
    init(&w, mode);
    FILE *out = fopen(prefix, "w");
    CHECK(out);
    fputs("frame,source_ms,query_ms,total_ms,live,peak,births,slots,record_bytes,workspace_bytes,"
          "checksum\n",
        out);
    volatile float checksum = 0;
    for (unsigned frame = 0; frame < 4800; ++frame) {
        double begin = milliseconds();
        uint64_t births = w.ledger.field.births;
        for (unsigned sub = 0; sub < 2; ++sub) {
            advance(&w);
            for (unsigned e = 0; e < emitters; ++e)
                CHECK(track(&w, e, r2_source(path, (unsigned)w.ledger.field.tick, e, dispersed)));
        }
        double sourced = milliseconds();
        for (unsigned q = 0; q < 30; ++q) {
            unsigned e = (q / 6) % emitters;
            unsigned age = 180 + (q % 3) * 60;
            unsigned historical
                = w.ledger.field.tick > age ? (unsigned)w.ledger.field.tick - age : 1;
            R2Source s = r2_source(path, historical, e, dispersed);
            R2Source previous = r2_source(path, historical - 1, e, dispersed);
            Vector2 d = Vector2Normalize(Vector2Subtract(s.position, previous.position));
            float distance = (.9f + 2.4f * (age / 120.0f - .25f)) * (q % 6 < 3 ? 1 : -1);
            Vector2 p = q < 24
                ? Vector2Add(s.position, (Vector2) { -d.y * distance, d.x * distance })
                : (Vector2) { 0, 4 + (q - 24) * 4.0f };
            BoatWakeObjectSample sample = evaluate(&w, p, w.ledger.field.tick / 120.0);
            checksum
                += sample.height + sample.gradient.x + sample.gradient.y + sample.vertical_velocity;
        }
        double end = milliseconds();
        if (frame >= 1200) {
            unsigned slots = 128;
            CHECK(w.ledger.field.live == emitters * (32u));
            fprintf(out, "%u,%.9g,%.9g,%.9g,%u,%u,%llu,%u,%zu,%zu,%.9g\n", frame - 1200,
                sourced - begin, end - sourced, end - begin, w.ledger.field.live,
                w.ledger.field.peak, (unsigned long long)(w.ledger.field.births - births), slots,
                slots * sizeof(BoatWakeObject), sizeof(w), (double)checksum);
        }
    }
    CHECK(isfinite(checksum));
    CHECK(fclose(out) == 0);
}

static void clip(const char *path, R2Mode mode, const char *root, bool detail)
{
    unsigned emitters = !strcmp(path, "crossing") ? 4 : 1;
    Experiment w = seed(mode, path, 3000, emitters);
    Vector2 center = detail ? r2_source(path, 3000, 0, false).position : (Vector2) { 0 };
    if (!detail && !strcmp(path, "straight"))
        center.x = -60;
    if (!detail && !strcmp(path, "wide"))
        center = (Vector2) { 25, 25 };
    float range = detail ? 80 : 160;
    const unsigned size = 96;
    Color pixels[96 * 192];
    for (unsigned frame = 0; frame < 100; ++frame) {
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x) {
                Vector2 p = { center.x + ((x + .5f) / size - .5f) * range,
                    center.y + ((y + .5f) / size - .5f) * range };
                BoatWakeObjectSample s = evaluate(&w, p, 25 + frame / 10.0);
                float h = Clamp(s.height / 2.4f, -1, 1);
                Color color = h >= 0 ? (Color) { (unsigned char)(20 + h * 235), 20, 30, 255 }
                                     : (Color) { 20, 20, (unsigned char)(30 - h * 225), 255 };
                pixels[y * size * 2 + x] = color;
                unsigned char foam = (unsigned char)(255 * Clamp(s.crest_foam, 0, 1));
                pixels[y * size * 2 + x + size] = (Color) { foam, foam, foam, 255 };
            }
        char name[4096];
        CHECK(snprintf(name, sizeof(name), "%s/%04u.png", root, frame) < (int)sizeof(name));
        CHECK(ExportImage((Image) { .data = pixels,
                              .width = 192,
                              .height = 96,
                              .mipmaps = 1,
                              .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 },
            name));
    }
}

static float discrepancy(BoatWakeObjectSample a, BoatWakeObjectSample b)
{
    return fmaxf(fabsf(a.height - b.height),
        fmaxf(Vector2Distance(a.gradient, b.gradient),
            fabsf(a.vertical_velocity - b.vertical_velocity)));
}

static void mixed_crossings(FILE *out)
{
    if (out)
        fputs("ownership,mode,time_seconds,max_height,min_height,max_gradient,max_vertical_speed,"
              "overlap_samples\n",
            out);
    for (unsigned owner = 0; owner < 2; ++owner) {
        R2Capture c;
        r2_capture_init(&c);
        unsigned slot = owner ? 7 : R2_SLOTS;
        unsigned descriptor = owner ? 7 : BOAT_OBJECT_PER_BOAT;
        c.field.records[0] = (BoatWakeObject) { .direction = { 1, 0 },
            .length = 7.5f,
            .inverse_speed = 1.0f / 15,
            .amplitude = 1.2f,
            .born = 120 };
        c.field.records[slot] = c.field.records[0];
        c.field.records[slot].direction = (Vector2) { 0, 1 };
        c.field.records[slot].amplitude = -1.2f;
        c.descriptors[0].curvature = .08f;
        c.descriptors[descriptor].curvature = -.05f;
        // Synthetic signed records test superposition, not a valid captured source snapshot.
        for (unsigned mode = 0; mode < 2; ++mode)
            for (unsigned age = 3; age <= 12; age += 3) {
                float peak = 0, trough = 0, gradient = 0, velocity = 0;
                unsigned overlaps = 0;
                for (unsigned y = 0; y <= 120; ++y)
                    for (unsigned x = 0; x <= 120; ++x) {
                        Vector2 p = { -30 + x * .5f, -30 + y * .5f };
                        float ka = mode ? .08f : 0, kb = mode ? -.05f : 0;
                        BoatWakeObjectSample a
                            = r2_transport_piece(&c.field.records[0], ka, p, age);
                        BoatWakeObjectSample b
                            = r2_transport_piece(&c.field.records[slot], kb, p, age);
                        overlaps += fabsf(a.height) > .01f && fabsf(b.height) > .01f;
                        BoatWakeObjectSample expected = { .height = a.height + b.height,
                            .gradient = Vector2Add(a.gradient, b.gradient),
                            .vertical_velocity = a.vertical_velocity + b.vertical_velocity };
                        BoatWakeObjectSample actual
                            = mode ? r2_transport_query(&c, p, age) : r2_query(&c.field, p, age);
                        CHECK(discrepancy(expected, actual) == 0);
                        peak = fmaxf(peak, actual.height);
                        trough = fminf(trough, actual.height);
                        gradient = fmaxf(gradient, Vector2Length(actual.gradient));
                        velocity = fmaxf(velocity, fabsf(actual.vertical_velocity));
                    }
                if (age == 3)
                    CHECK(overlaps > 0);
                if (out)
                    fprintf(out, "%s,%s,%u,%.9g,%.9g,%.9g,%.9g,%u\n",
                        owner ? "same_emitter_nonadjacent" : "independent",
                        mode ? "spread" : "baseline", age, (double)peak, (double)trough,
                        (double)gradient, (double)velocity, overlaps);
            }
    }
}

static void contracts(FILE *out)
{
    if (out)
        fputs(
            "step,max_gradient_error,max_velocity_error,identity_error,cancellation_error\n", out);
    const float steps[] = { .01f, .002f, .0005f };
    const float ks[] = { -.08f, -.02f, 0, .02f, .08f };
    const float us[] = { -7.501f, -7.5f, -7.499f, -3, 0, 3, 7.499f, 7.5f, 7.501f };
    const float vs[] = { -14, -5, -1, -.001f, 0, .001f, 1, 5, 14 };
    const double clocks[] = { .0005, .15, .3, .65, 1, 4, 10, 12, 15, 15.9995, 16 };
    for (unsigned si = 0; si < 3; ++si) {
        float spatial = 0, temporal = 0, identity = 0, cancel = 0, h = steps[si];
        for (unsigned frame = 0; frame < 2; ++frame)
            for (unsigned ki = 0; ki < 5; ++ki)
                for (unsigned ui = 0; ui < 9; ++ui)
                    for (unsigned vi = 0; vi < 9; ++vi)
                        for (unsigned ti = 0; ti < 11; ++ti) {
                            float angle = frame ? .37f : 0;
                            BoatWakeObject o
                                = { .position = frame ? (Vector2) { 113, -71 } : (Vector2) { 0 },
                                      .direction = { cosf(angle), sinf(angle) },
                                      .length = 7.5f,
                                      .inverse_speed = 1.0f / 15,
                                      .amplitude = 1.2f,
                                      .born = 120 };
                            Vector2 t = o.direction, n = { -t.y, t.x };
                            Vector2 p = Vector2Add(o.position,
                                Vector2Add(
                                    Vector2Scale(t,
                                        us[ui]
                                            * (ks[ki] * vs[vi] < 0 ? 1
                                                        + powf(-ks[ki] * vs[vi], 3)
                                                            / (.0625f
                                                                + ks[ki] * ks[ki] * vs[vi] * vs[vi])
                                                                   : 1)),
                                    Vector2Scale(n, vs[vi])));
                            double time = 1 + clocks[ti];
                            BoatWakeObjectSample a = r2_transport_piece(&o, ks[ki], p, time);
                            CHECK(isfinite(a.height) && isfinite(a.gradient.x)
                                && isfinite(a.gradient.y) && isfinite(a.vertical_velocity));
                            Vector2 local = Vector2Subtract(p, o.position);
                            if (ks[ki] * Vector2DotProduct(local, n) >= 0)
                                identity = fmaxf(
                                    identity, discrepancy(a, r2_piece(&o, R2_BASELINE, p, time)));
                            BoatWakeObject negative = o;
                            negative.amplitude = -o.amplitude;
                            BoatWakeObjectSample b = r2_transport_piece(&negative, ks[ki], p, time);
                            b.height = -b.height;
                            b.gradient = Vector2Negate(b.gradient);
                            b.vertical_velocity = -b.vertical_velocity;
                            cancel = fmaxf(cancel, discrepancy(a, b));
                            float dx
                                = (r2_transport_piece(&o, ks[ki], (Vector2) { p.x + h, p.y }, time)
                                          .height
                                      - r2_transport_piece(
                                          &o, ks[ki], (Vector2) { p.x - h, p.y }, time)
                                          .height)
                                / ((p.x + h) - (p.x - h));
                            float dy
                                = (r2_transport_piece(&o, ks[ki], (Vector2) { p.x, p.y + h }, time)
                                          .height
                                      - r2_transport_piece(
                                          &o, ks[ki], (Vector2) { p.x, p.y - h }, time)
                                          .height)
                                / ((p.y + h) - (p.y - h));
                            float dt = (r2_transport_piece(&o, ks[ki], p, time + h).height
                                           - r2_transport_piece(&o, ks[ki], p, time - h).height)
                                / (2 * h);
                            spatial = fmaxf(spatial, hypotf(dx - a.gradient.x, dy - a.gradient.y));
                            temporal = fmaxf(temporal, fabsf(dt - a.vertical_velocity));
                        }
        CHECK(identity == 0 && cancel == 0);
        fprintf(stderr, "derivatives step=%g spatial=%g temporal=%g\n", (double)h, (double)spatial,
            (double)temporal);
        if (si == 2)
            CHECK(spatial < .003f && temporal < .02f);
        if (out)
            fprintf(out, "%.9g,%.9g,%.9g,%.9g,%.9g\n", (double)h, (double)spatial, (double)temporal,
                (double)identity, (double)cancel);
    }
    Experiment line = seed(R2_SHORT, "straight", 3000, 1);
    for (unsigned i = 0; i < 128; ++i)
        CHECK(line.ledger.descriptors[i].curvature == 0);
    // Finite straight histories with unequal speed, length and filtered strength remain exact.
    Experiment unequal;
    init(&unequal, R2_SHORT);
    float x = 0;
    for (unsigned tick = 0; tick <= 1800; ++tick) {
        if (tick)
            advance(&unequal);
        float speed = tick < 600 ? 8 : tick < 1200 ? 20 : 12;
        x += speed / 120;
        CHECK(track(&unequal, 0, (R2Source) { { x, 0 }, speed, tick < 900 ? .4f : 1 }));
    }
    for (unsigned i = 0; i < 128; ++i)
        CHECK(unequal.ledger.descriptors[i].curvature == 0);
    for (unsigned i = 0; i < 700; ++i) {
        Vector2 p = { -5 + i * .4f, -30.0f + (i % 60) };
        CHECK(discrepancy(evaluate(&unequal, p, 19), r2_query(&unequal.ledger.field, p, 19)) == 0);
    }
    // Fields remain identical after snapshot restoration; later source motion does not rewrite old
    // pieces.
    Experiment w = seed(R2_SHORT, "eight", 3000, 1), restore = { .mode = R2_SHORT };
    R2CaptureSnapshot snapshot;
    CHECK(r2_capture_snapshot(&w.ledger, &snapshot));
    CHECK(r2_capture_restore(&restore.ledger, &snapshot));
    for (unsigned i = 0; i < 100; ++i) {
        Vector2 p = { -20 + i * .71f, -30 + i * .63f };
        CHECK(discrepancy(evaluate(&w, p, 29), evaluate(&restore, p, 29)) == 0);
    }
}

static void concentration(FILE *out)
{
    fputs("path,mode,after_stop_seconds,grid_metres,samples,surviving_records,low_x,low_z,high_x,"
          "high_z,max_height,min_height,max_gradient,max_vertical_speed\n",
        out);
    const char *paths[]
        = { "quarter12", "quarter50", "circle", "wide", "eight", "reverse", "crossing" };
    const unsigned ages[] = { 1, 4, 8, 12, 15 };
    for (unsigned pi = 0; pi < 7; ++pi) {
        Experiment w;
        if (pi < 2) {
            float radius = pi ? 50 : 12;
            unsigned ticks = (unsigned)(PI / 2 * radius / 15 * 120) / 60 * 60;
            w = turn_seed(R2_SHORT, radius, ticks, false);
        } else
            w = seed(R2_SHORT, paths[pi], 3000, pi == 6 ? 4 : 1);
        for (unsigned ai = 0; ai < 5; ++ai) {
            double time = w.ledger.field.tick / 120.0 + ages[ai];
            Vector2 low, high;
            r2_transport_bounds(&w.ledger, time, &low, &high);
            CHECK(isfinite(low.x) && isfinite(high.x));
            unsigned live = 0;
            for (unsigned e = 0; e < 4; ++e)
                for (unsigned i = 0; i < 32; ++i) {
                    const BoatWakeObject *o = &w.ledger.field.records[e * 64 + i];
                    live += o->amplitude && time < (o->born + 1920) / 120.0;
                }
            // Full history uses 1 m; quarter-turn controls add the finer .5 m grid.
            for (unsigned level = 0; level < (pi < 2 ? 2u : 1u); ++level) {
                float step = level ? .5f : 1;
                unsigned nx = (unsigned)ceilf((high.x - low.x) / step),
                         ny = (unsigned)ceilf((high.y - low.y) / step);
                for (unsigned mode = 0; mode < 2; ++mode) {
                    w.mode = (R2Mode)mode;
                    float peak = 0, trough = 0, gradient = 0, velocity = 0;
                    for (unsigned y = 0; y <= ny; ++y)
                        for (unsigned x = 0; x <= nx; ++x) {
                            Vector2 p = { low.x + x * step, low.y + y * step };
                            BoatWakeObjectSample h = evaluate(&w, p, time);
                            CHECK(isfinite(h.height) && isfinite(h.gradient.x)
                                && isfinite(h.gradient.y) && isfinite(h.vertical_velocity));
                            peak = fmaxf(peak, h.height);
                            trough = fminf(trough, h.height);
                            gradient = fmaxf(gradient, Vector2Length(h.gradient));
                            velocity = fmaxf(velocity, fabsf(h.vertical_velocity));
                        }
                    fprintf(out, "%s,%s,%u,%.9g,%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                        paths[pi], mode_name(w.mode), ages[ai], (double)step, (nx + 1) * (ny + 1),
                        live, (double)low.x, (double)low.y, (double)high.x, (double)high.y,
                        (double)peak, (double)trough, (double)gradient, (double)velocity);
                }
            }
        }
    }
}

int main(int argc, char **argv)
{
    SetTraceLogLevel(LOG_WARNING);
    if (argc == 2 && !strcmp(argv[1], "--check")) {
        contracts(NULL);
        mixed_crossings(NULL);
        puts("wake_transport_contract=ok");
    } else if (argc == 3 && !strcmp(argv[1], "--screen")) {
        FILE *f = output(argv[2], "derivatives.csv");
        contracts(f);
        CHECK(fclose(f) == 0);
        f = output(argv[2], "crossings.csv");
        mixed_crossings(f);
        CHECK(fclose(f) == 0);
        f = output(argv[2], "straight.csv");
        FILE *t = output(argv[2], "arrivals.csv");
        straight(f, t);
        CHECK(fclose(f) == 0 && fclose(t) == 0);
        f = output(argv[2], "turns.csv");
        turns(f);
        CHECK(fclose(f) == 0);
        f = output(argv[2], "concentration.csv");
        concentration(f);
        CHECK(fclose(f) == 0);
    } else if (argc == 8 && !strcmp(argv[1], "--bench")) {
        CHECK(valid_path(argv[2]) && !strcmp(argv[6], "--output"));
        unsigned mode = index_argument(argv[3], 2), emitters = index_argument(argv[4], 5);
        CHECK(emitters == 1 || emitters == 4);
        CHECK(!strcmp(argv[5], "dense") || !strcmp(argv[5], "dispersed"));
        bench(argv[2], (R2Mode)mode, emitters, !strcmp(argv[5], "dispersed"), argv[7]);
    } else if ((argc == 6 || argc == 7) && !strcmp(argv[1], "--clip")) {
        CHECK(valid_path(argv[2]) && !strcmp(argv[4], "--output"));
        if (argc == 7)
            CHECK(!strcmp(argv[6], "--detail"));
        clip(argv[2], (R2Mode)index_argument(argv[3], 2), argv[5], argc == 7);
    } else {
        fputs("Usage: wake_transport --check | --screen DIR | --bench PATH 0|1 1|4 dense|dispersed "
              "--output CSV | --clip PATH 0|1 --output DIR [--detail]\n",
            stderr);
        return 2;
    }
    return 0;
}
