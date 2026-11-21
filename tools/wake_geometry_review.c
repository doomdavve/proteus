#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "raylib.h"
#include "raymath.h"
#include "wake_geometry.h"
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

static R2Field seed(R2Mode mode, const char *path, unsigned ticks, unsigned emitters)
{
    R2Field w;
    r2_init(&w, mode);
    for (unsigned tick = 0; tick <= ticks; ++tick) {
        if (tick)
            r2_advance(&w);
        for (unsigned e = 0; e < emitters; ++e)
            CHECK(r2_track(&w, e, r2_source(path, tick, e, false)));
    }
    return w;
}

static float error(BoatWakeObjectSample a, BoatWakeObjectSample b)
{
    return fmaxf(fabsf(a.height - b.height),
        fmaxf(fabsf(a.crest_foam - b.crest_foam),
            fmaxf(Vector2Distance(a.gradient, b.gradient),
                fabsf(a.vertical_velocity - b.vertical_velocity))));
}

static float parity(void)
{
    const char *paths[] = { "straight", "circle", "wide", "eight", "reverse" };
    float worst = 0;
    for (unsigned p = 0; p < 5; ++p) {
        R2Field private;
        BoatWakeObjects production;
        r2_init(&private, R2_BASELINE);
        CHECK(boat_wake_objects_init(&production, 1920, BOAT_OBJECT_DEFAULT_HEIGHT));
        for (unsigned tick = 0; tick <= 4200; ++tick) {
            if (tick) {
                r2_advance(&private);
                boat_wake_objects_advance(&production);
            }
            for (unsigned e = 0; e < 4; ++e) {
                R2Source s = r2_source(paths[p], tick, e, false);
                // Exercise a dry gap and explicit tracker restart without changing production.
                if (tick >= 3000 && tick < 3040)
                    s.wet = 0;
                if (tick == 3600) {
                    private.emitters[e].initialized = false;
                    production.emitters[e].initialized = false;
                }
                CHECK(r2_track(&private, e, s));
                CHECK(boat_wake_objects_track(&production, e, s.position, s.speed, s.wet));
                for (unsigned i = 0; i < 32; ++i) {
                    const BoatWakeObject *a = &private.records[e * 64 + i],
                                         *b = &production.objects[e * 32 + i];
                    CHECK(a->born == b->born && a->amplitude == b->amplitude
                        && a->length == b->length);
                    CHECK(a->position.x == b->position.x && a->position.y == b->position.y);
                    CHECK(a->direction.x == b->direction.x && a->direction.y == b->direction.y);
                    CHECK(a->inverse_speed == b->inverse_speed);
                }
            }
            CHECK(private.live == production.live && private.births == production.emitted);
            if (tick % 30 == 0)
                for (unsigned q = 0; q < 12; ++q) {
                    Vector2 pos = { -40 + q * 7.13f, -30 + q * 5.41f };
                    worst = fmaxf(worst,
                        error(r2_query(&private, pos, tick / 120.0),
                            boat_wake_objects_query(&production, pos)));
                }
        }
    }
    CHECK(worst < .00002f);
    return worst;
}

static void attribution(void)
{
    for (R2Mode mode = 0; mode < R2_MODES; ++mode) {
        R2Field w;
        r2_init(&w, mode);
        BoatWakeObject a = { .direction = { 1, 0 },
            .length = 7.5f,
            .amplitude = 1.2f,
            .inverse_speed = 1 / 15.0f,
            .born = 60 };
        BoatWakeObject b = a;
        b.direction = (Vector2) { 0, 1 };
        w.records[0] = a;
        w.records[7] = b; // Nonadjacent fronts of the same emitter, not an adjacency join.
        Vector2 p = { 4, 4 };
        BoatWakeObjectSample x = r2_piece(&a, mode, p, 2), y = r2_piece(&b, mode, p, 2);
        BoatWakeObjectSample sum = r2_query(&w, p, 2);
        CHECK(fabsf(sum.height - x.height - y.height) < 1e-5f);
        CHECK(fabsf(sum.vertical_velocity - x.vertical_velocity - y.vertical_velocity) < 1e-5f);
        CHECK(Vector2Distance(sum.gradient, Vector2Add(x.gradient, y.gradient)) < 1e-5f);
        w.records[7] = a;
        w.records[7].amplitude *= -1;
        sum = r2_query(&w, p, 2);
        CHECK(fabsf(sum.height) < 1e-5f && fabsf(sum.vertical_velocity) < 1e-5f);
        CHECK(Vector2Length(sum.gradient) < 1e-5f);

        w = seed(mode, "circle", 3000, 1);
        unsigned slots = r2_short(mode) ? 64 : 32;
        unsigned last = (w.emitters[0].next + slots - 1) % slots;
        BoatWakeObject frozen = w.records[last];
        Vector2 normal = { -frozen.direction.y, frozen.direction.x };
        Vector2 probe
            = Vector2Add(frozen.position, Vector2Scale(normal, sqrtf(3.3f * 3.3f - .25f)));
        BoatWakeObjectSample before = r2_piece(&frozen, mode, probe, 26);
        CHECK(before.height > .05f);
        w.emitters[0].initialized = false;
        for (unsigned t = 0; t < 120; ++t) {
            r2_advance(&w);
            R2Source source = r2_source("straight", (unsigned)w.tick, 0, true);
            if (t > 30 && t < 60)
                source.wet = 0;
            if (t == 60)
                w.emitters[0].initialized = false;
            CHECK(r2_track(&w, 0, source));
        }
        CHECK(error(before, r2_piece(&w.records[last], mode, probe, 26)) == 0);
        CHECK(
            w.records[last].position.x == frozen.position.x && w.records[last].born == frozen.born);
    }
}

static void envelope_seams(void)
{
    for (R2Mode mode = 0; mode < R2_MODES; ++mode) {
        BoatWakeObject o = { .direction = { .6f, .8f },
            .length = r2_short(mode) ? 3.75f : 7.5f,
            .amplitude = 1.2f,
            .inverse_speed = 1 / 15.0f,
            .born = 60 };
        float phase = mode == R2_MATCHED ? .125f : 0;
        float fade = 12 - o.length * o.inverse_speed - (mode == R2_MATCHED ? .25f : 0);
        const float clocks[] = { 0, .3f, .65f + phase, fade + phase, fade + 4 + phase, 16 };
        for (unsigned boundary = 0; boundary < 6; ++boundary)
            for (int offset = -1; offset <= 1; ++offset) {
                double clock = clocks[boundary] + offset * .0005;
                float radius = .9f + 2.4f * ((float)clock - phase);
                float v = sqrtf(fmaxf(radius * radius - .25f, 0));
                Vector2 normal = { -o.direction.y, o.direction.x };
                Vector2 p = Vector2Scale(normal, v), low = p, high = p;
                double time = .5 + clock;
                BoatWakeObjectSample a = r2_piece(&o, mode, p, time);
                CHECK(isfinite(a.height) && isfinite(a.gradient.x) && isfinite(a.gradient.y)
                    && isfinite(a.vertical_velocity));
                low.x -= .002f;
                high.x += .002f;
                float gx
                    = (r2_piece(&o, mode, high, time).height - r2_piece(&o, mode, low, time).height)
                    / .004f;
                low = high = p;
                low.y -= .002f;
                high.y += .002f;
                float gy
                    = (r2_piece(&o, mode, high, time).height - r2_piece(&o, mode, low, time).height)
                    / .004f;
                float vt = (r2_piece(&o, mode, p, time + .0005).height
                               - r2_piece(&o, mode, p, time - .0005).height)
                    / .001f;
                CHECK(fabsf(gx - a.gradient.x) < .003f && fabsf(gy - a.gradient.y) < .003f);
                CHECK(fabsf(vt - a.vertical_velocity) < .02f);
            }
    }
}

static void derivatives(FILE *f, bool quick)
{
    if (f)
        fputs("mode,epsilon_metres,epsilon_seconds,spatial_error,time_error,signed_residual,"
              "samples\n",
            f);
    const float steps[] = { .01f, .002f, .0005f };
    for (R2Mode mode = 0; mode < R2_MODES; ++mode)
        for (unsigned level = 0; level < 3; ++level) {
            float dx = steps[level], worst_g = 0, worst_t = 0, worst_sum = 0;
            unsigned count = 0;
            BoatWakeObject o = { .direction = { 1, 0 },
                .length = 7.5f,
                .amplitude = 1.2f,
                .inverse_speed = 1 / 15.0f,
                .born = 60 };
            // Fixed seams and a dense sampling of both the capsule shoulder and angular band.
            unsigned grid = quick ? 24 : 96;
            for (unsigned age = 0; age < 6; ++age)
                for (unsigned i = 0; i <= grid; ++i)
                    for (unsigned j = 0; j <= grid; ++j) {
                        const double clocks[] = { .1, .5, 1, 6, 12, 15 };
                        double time = .5 + clocks[age];
                        o.length = r2_short(mode) ? 3.75f : 7.5f;
                        o.direction = age % 2 ? (Vector2) { .6f, .8f } : (Vector2) { 1, 0 };
                        Vector2 p = { -30 + 60.0f * i / grid, -40 + 80.0f * j / grid };
                        // Include exact shoulder and transition seams, with points on the crest.
                        if (j < 8) {
                            float a = .5f * o.length, delta = fminf(.1f * o.length, .25f);
                            const float us[] = { a - .001f, a, a + .001f, a + delta - .001f,
                                a + delta, a + delta + .001f, 12, 20 };
                            p.x = us[j];
                            p.y = .9f + 2.4f * (float)clocks[age] + (i / (float)grid - .5f) * 12;
                        }
                        if (j >= 8 && j < 16 && (mode == R2_OPEN || mode == R2_ANGLE)) {
                            const float gammas[] = { .4999f, .5f, .5001f, .6f, .7070068f, .7071068f,
                                .7072068f, .8f };
                            float gamma = gammas[j - 8], excess = 1 + i * .05f;
                            p.x = .5f * o.length + .125f + excess;
                            p.y = sqrtf(excess * excess * (1 / (gamma * gamma) - 1) - .25f);
                            time = .5 + (excess / gamma - .9) / 2.4
                                + (.5 * o.length + .125) * o.inverse_speed;
                        }
                        p = (Vector2) { p.x * o.direction.x - p.y * o.direction.y,
                            p.x * o.direction.y + p.y * o.direction.x };
                        BoatWakeObjectSample a = r2_piece(&o, mode, p, time);
                        Vector2 low = p, high = p;
                        low.x -= dx;
                        high.x += dx;
                        float gx = (r2_piece(&o, mode, high, time).height
                                       - r2_piece(&o, mode, low, time).height)
                            / (2 * dx);
                        low = high = p;
                        low.y -= dx;
                        high.y += dx;
                        float gy = (r2_piece(&o, mode, high, time).height
                                       - r2_piece(&o, mode, low, time).height)
                            / (2 * dx);
                        float vt = (r2_piece(&o, mode, p, time + dx).height
                                       - r2_piece(&o, mode, p, time - dx).height)
                            / (2 * dx);
                        worst_g = fmaxf(
                            worst_g, fmaxf(fabsf(gx - a.gradient.x), fabsf(gy - a.gradient.y)));
                        worst_t = fmaxf(worst_t, fabsf(vt - a.vertical_velocity));
                        BoatWakeObject negative = o;
                        negative.amplitude *= -1;
                        BoatWakeObjectSample b = r2_piece(&negative, mode, p, time);
                        worst_sum = fmaxf(worst_sum, fabsf(a.height + b.height));
                        worst_sum
                            = fmaxf(worst_sum, Vector2Length(Vector2Add(a.gradient, b.gradient)));
                        worst_sum
                            = fmaxf(worst_sum, fabsf(a.vertical_velocity + b.vertical_velocity));
                        CHECK(isfinite(a.height) && isfinite(a.gradient.x) && isfinite(a.gradient.y)
                            && isfinite(a.vertical_velocity) && isfinite(gx) && isfinite(gy)
                            && isfinite(vt));
                        ++count;
                    }
            if (f)
                fprintf(f, "%s,%.9g,%.9g,%.9g,%.9g,%.9g,%u\n", r2_name(mode), (double)dx,
                    (double)dx, (double)worst_g, (double)worst_t, (double)worst_sum, count);
            // Fine FD screens only; coarse-step errors expose truncation and seam convergence.
            if (level == 2)
                CHECK(worst_g < .003f && worst_t < .02f && worst_sum < 1e-5f);
        }
}

static void straight(FILE *out, FILE *traces)
{
    fputs("mode,after_stop_seconds,samples,height_normalized_rms,peak_error_normalized,gradient_"
          "normalized_rms,velocity_normalized_rms,height_scale,gradient_scale,velocity_scale,"
          "interior_rms,interior_peak_error,interior_samples\n",
        out);
    fputs("mode,tick,height,gradient_x,gradient_z,vertical_velocity\n", traces);
    R2Field ref = seed(R2_BASELINE, "straight", 3000, 1);
    const unsigned ages[] = { 1, 4, 8, 12, 15 };
    for (R2Mode mode = 0; mode < R2_MODES; ++mode) {
        R2Field w = seed(mode, "straight", 3000, 1);
        for (unsigned ai = 0; ai < 5; ++ai) {
            double h2 = 0, dh2 = 0, g2 = 0, dg2 = 0, v2 = 0, dv2 = 0;
            float peak = 0, maximum_error = 0, interior_peak = 0, interior_error = 0;
            double interior_h2 = 0, interior_dh2 = 0;
            unsigned count = 0, interior_count = 0;
            double time = 25 + ages[ai];
            for (unsigned i = 0; i < 600; ++i) {
                float source_t = 10 + 15 * (i + .5f) / 600;
                float age = (float)time - source_t - .25f;
                float radius = .9f + 2.4f * age;
                for (unsigned j = 0; j < 90; ++j) {
                    Vector2 p = { 15 * (source_t - 25), radius - 7 + .1f * j };
                    BoatWakeObjectSample a = r2_query(&ref, p, time), b = r2_query(&w, p, time);
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
                r2_name(mode), ages[ai], count, sqrt(dh2 / h2), maximum_error / (double)peak,
                sqrt(dg2 / g2), sqrt(dv2 / v2), sqrt(h2 / count), sqrt(g2 / count),
                sqrt(v2 / count), sqrt(interior_dh2 / interior_h2),
                interior_error / (double)interior_peak, interior_count);
        }
        for (unsigned tick = 3000; tick <= 4920; ++tick) {
            BoatWakeObjectSample s = r2_query(&w, (Vector2) { -30, 10 }, tick / 120.0);
            fprintf(traces, "%s,%u,%.9g,%.9g,%.9g,%.9g\n", r2_name(mode), tick, (double)s.height,
                (double)s.gradient.x, (double)s.gradient.y, (double)s.vertical_velocity);
        }
    }
}

static BoatWakeObjectSample pair(const R2Field *w, Vector2 point, double time, float source_time)
{
    unsigned best[2] = { 0, 0 };
    float distances[2] = { 1e9f, 1e9f };
    float half = r2_short(w->mode) ? .125f : .25f;
    for (unsigned i = 0; i < R2_SLOTS; ++i)
        if (w->records[i].amplitude) {
            float distance = fabsf(w->records[i].born / 120.0f - half - source_time);
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
    BoatWakeObjectSample a = r2_piece(&w->records[best[0]], w->mode, point, time);
    BoatWakeObjectSample b = distances[1] < 1e9f
        ? r2_piece(&w->records[best[1]], w->mode, point, time)
        : (BoatWakeObjectSample) { 0 };
    a.height += b.height;
    return a;
}

static R2Field turn_seed(R2Mode mode, float radius, unsigned ticks, bool line)
{
    R2Field w;
    r2_init(&w, mode);
    for (unsigned tick = 0; tick <= ticks; ++tick) {
        if (tick)
            r2_advance(&w);
        float t = tick / 120.0f, a = 15 * t / radius;
        R2Source s = { .position
            = line ? (Vector2) { 15 * t, 0 } : (Vector2) { radius * cosf(a), radius * sinf(a) },
            .speed = 15,
            .wet = 1 };
        CHECK(r2_track(&w, 0, s));
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
    for (R2Mode mode = 0; mode < R2_MODES; ++mode)
        for (unsigned ri = 0; ri < 2; ++ri) {
            float radius = radii[ri];
            unsigned ticks = (unsigned)(PI / 2 * radius / 15 * 120) / 60 * 60;
            R2Field w = turn_seed(mode, radius, ticks, false),
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
                                float age = (float)time - t - (mode == R2_SHORT ? .125f : .25f);
                                float distance = .9f + 2.4f * age;
                                if (side < 0 && radius - distance <= 2) {
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
                                    BoatWakeObjectSample surface = r2_query(&w, p, time);
                                    float h = surface.height;
                                    gradient_max
                                        = fmaxf(gradient_max, Vector2Length(surface.gradient));
                                    velocity_max
                                        = fmaxf(velocity_max, fabsf(surface.vertical_velocity));
                                    if (h > a) {
                                        a = h;
                                        offset_best = offset;
                                    }
                                    b = fmaxf(b, r2_query(&line, q, time).height);
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
                                r2_name(mode), (double)radius, ages[ai], side, (double)ds,
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

static void concentration(FILE *out)
{
    fputs("mode,radius,after_stop_seconds,grid_metres,samples,max_height,min_height,max_gradient,"
          "max_vertical_speed\n",
        out);
    const float radii[] = { 12, 50 }, steps[] = { 1, .5f };
    const unsigned ages[] = { 1, 4, 8, 12, 15 };
    for (R2Mode mode = 0; mode < R2_MODES; ++mode)
        for (unsigned ri = 0; ri < 2; ++ri) {
            float radius = radii[ri], extent = radius + 42;
            unsigned ticks = (unsigned)(PI / 2 * radius / 15 * 120) / 60 * 60;
            R2Field w = turn_seed(mode, radius, ticks, false);
            for (unsigned level = 0; level < 2; ++level)
                for (unsigned ai = 0; ai < 5; ++ai) {
                    float high = 0, low = 0, gradient = 0, velocity = 0;
                    unsigned count = 0;
                    for (float x = -extent; x <= extent; x += steps[level])
                        for (float z = -extent; z <= extent; z += steps[level]) {
                            BoatWakeObjectSample a
                                = r2_query(&w, (Vector2) { x, z }, ticks / 120.0 + ages[ai]);
                            CHECK(isfinite(a.height) && isfinite(a.vertical_velocity));
                            high = fmaxf(high, a.height);
                            low = fminf(low, a.height);
                            gradient = fmaxf(gradient, Vector2Length(a.gradient));
                            velocity = fmaxf(velocity, fabsf(a.vertical_velocity));
                            ++count;
                        }
                    fprintf(out, "%s,%.9g,%u,%.9g,%u,%.9g,%.9g,%.9g,%.9g\n", r2_name(mode),
                        (double)radius, ages[ai], (double)steps[level], count, (double)high,
                        (double)low, (double)gradient, (double)velocity);
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
    R2Field w;
    r2_init(&w, mode);
    FILE *out = fopen(prefix, "w");
    CHECK(out);
    fputs("frame,source_ms,query_ms,total_ms,live,peak,births,slots,record_bytes,workspace_bytes,"
          "checksum\n",
        out);
    volatile float checksum = 0;
    for (unsigned frame = 0; frame < 4800; ++frame) {
        double begin = milliseconds();
        uint64_t births = w.births;
        for (unsigned sub = 0; sub < 2; ++sub) {
            r2_advance(&w);
            for (unsigned e = 0; e < emitters; ++e)
                CHECK(r2_track(&w, e, r2_source(path, (unsigned)w.tick, e, dispersed)));
        }
        double sourced = milliseconds();
        for (unsigned q = 0; q < 30; ++q) {
            unsigned e = (q / 6) % emitters;
            unsigned age = 180 + (q % 3) * 60;
            unsigned historical = w.tick > age ? (unsigned)w.tick - age : 1;
            R2Source s = r2_source(path, historical, e, dispersed);
            R2Source previous = r2_source(path, historical - 1, e, dispersed);
            Vector2 d = Vector2Normalize(Vector2Subtract(s.position, previous.position));
            float distance = (.9f + 2.4f * (age / 120.0f - .25f)) * (q % 6 < 3 ? 1 : -1);
            Vector2 p = q < 24
                ? Vector2Add(s.position, (Vector2) { -d.y * distance, d.x * distance })
                : (Vector2) { 0, 4 + (q - 24) * 4.0f };
            BoatWakeObjectSample sample = r2_query(&w, p, w.tick / 120.0);
            checksum
                += sample.height + sample.gradient.x + sample.gradient.y + sample.vertical_velocity;
        }
        double end = milliseconds();
        if (frame >= 1200) {
            unsigned slots = r2_short(mode) ? 256 : 128;
            CHECK(w.live == emitters * (r2_short(mode) ? 64u : 32u));
            fprintf(out, "%u,%.9g,%.9g,%.9g,%u,%u,%llu,%u,%zu,%zu,%.9g\n", frame - 1200,
                sourced - begin, end - sourced, end - begin, w.live, w.peak,
                (unsigned long long)(w.births - births), slots, slots * sizeof(BoatWakeObject),
                sizeof(w), (double)checksum);
        }
    }
    CHECK(isfinite(checksum));
    CHECK(fclose(out) == 0);
}

static void clip(const char *path, R2Mode mode, const char *root, bool detail)
{
    unsigned emitters = !strcmp(path, "crossing") ? 4 : 1;
    R2Field w = seed(mode, path, 3000, emitters);
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
                BoatWakeObjectSample s = r2_query(&w, p, 25 + frame / 10.0);
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

int main(int argc, char **argv)
{
    SetTraceLogLevel(LOG_WARNING);
    if (argc == 2 && !strcmp(argv[1], "--check")) {
        float match = parity();
        attribution();
        envelope_seams();
        derivatives(NULL, true);
        printf("wake_geometry_contract=ok parity_error=%.9g\n", (double)match);
    } else if (argc == 3 && !strcmp(argv[1], "--screen")) {
        float match = parity();
        attribution();
        envelope_seams();
        FILE *f = output(argv[2], "derivatives.csv");
        derivatives(f, false);
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
        f = output(argv[2], "parity.txt");
        fprintf(f, "max_production_error=%.9g\n", (double)match);
        CHECK(fclose(f) == 0);
    } else if (argc == 8 && !strcmp(argv[1], "--bench")) {
        unsigned mode = index_argument(argv[3], R2_MODES), emitters = index_argument(argv[4], 5);
        CHECK(mode < R2_MODES && (emitters == 1 || emitters == 4));
        CHECK(!strcmp(argv[5], "dense") || !strcmp(argv[5], "dispersed"));
        CHECK(!strcmp(argv[6], "--output"));
        CHECK(valid_path(argv[2]));
        bench(argv[2], (R2Mode)mode, emitters, !strcmp(argv[5], "dispersed"), argv[7]);
    } else if ((argc == 6 || argc == 7) && !strcmp(argv[1], "--clip")) {
        unsigned mode = index_argument(argv[3], R2_MODES);
        CHECK(mode < R2_MODES);
        CHECK(!strcmp(argv[4], "--output"));
        if (argc == 7)
            CHECK(!strcmp(argv[6], "--detail"));
        CHECK(valid_path(argv[2]));
        clip(argv[2], (R2Mode)mode, argv[5], argc == 7);
    } else {
        fputs("Usage: wake_geometry --check | --screen DIR | --bench PATH MODE 1|4 dense|dispersed "
              "--output CSV | --clip PATH MODE --output DIR [--detail]\n",
            stderr);
        return 2;
    }
    return 0;
}
