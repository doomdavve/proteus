/* R0 paired turn/straight crest-coverage metric. No alternative surface evaluator. */
#include "core/boat_wake_objects.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "turn probe failed: %s\n", #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define PI_F 3.14159265358979323846f

static BoatWakeObjects track(float radius, unsigned ticks, bool straight)
{
    BoatWakeObjects w;
    CHECK(boat_wake_objects_init(&w, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT));
    for (unsigned tick = 0; tick <= ticks; ++tick) {
        if (tick)
            boat_wake_objects_advance(&w);
        float t = tick / 120.0f, angle = 15 * t / radius;
        Vector2 p = straight ? (Vector2) { 15 * t, 0 }
                             : (Vector2) { radius * cosf(angle), radius * sinf(angle) };
        CHECK(boat_wake_objects_track(&w, 0, p, 15, 1));
    }
    return w;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: wake_turn_probes OUTPUT.csv\n");
        return 2;
    }
    FILE *out = fopen(argv[1], "w");
    CHECK(out);
    fputs("radius,after_stop_seconds,side,eligible_samples,reference_length,uncovered_length_at_"
          "25pct,min_crest_ratio,max_crest_ratio,grid_metres,normal_search_metres\n",
        out);
    const float radii[] = { 12, 50 };
    const unsigned ages[] = { 120, 480, 960, 1440, 1800 };
    for (unsigned ri = 0; ri < 2; ++ri) {
        float radius = radii[ri];
        /* Complete half-second source spans, ending before a quarter circle. */
        unsigned ticks = (unsigned)(PI_F / 2 * radius / 15 * 120) / 60 * 60;
        BoatWakeObjects turn = track(radius, ticks, false), straight = track(radius, ticks, true);
        for (unsigned ai = 0; ai < 5; ++ai)
            for (int side = -1; side <= 1; side += 2) {
                turn.tick = straight.tick = ticks + ages[ai];
                unsigned eligible = 0, uncovered = 0;
                float minimum = 1e9f, maximum = 0;
                const unsigned samples = 400;
                float begin = .25f, end = ticks / 120.0f - .25f;
                float spacing = 15 * (end - begin) / samples;
                for (unsigned i = 0; i < samples; ++i) {
                    float t = begin + (i + .5f) * (end - begin) / samples;
                    float angle = 15 * t / radius;
                    /* Production local-age convention places the effective emission .25 s
                     * after the source midpoint for a 60-tick span. Same reference for both. */
                    float age = turn.tick / 120.0f - (t + .25f);
                    float distance = .9f + 2.4f * age;
                    if (side < 0 && radius - distance <= 2)
                        continue; // Passed the centre/focus.
                    float a = 0, b = 0;
                    for (int j = -40; j <= 40; ++j) {
                        float offset = j * .05f;
                        Vector2 p = { (radius + side * (distance + offset)) * cosf(angle),
                            (radius + side * (distance + offset)) * sinf(angle) };
                        Vector2 q = { 15 * t, side * (distance + offset) };
                        a = fmaxf(a, boat_wake_objects_query(&turn, p).height);
                        b = fmaxf(b, boat_wake_objects_query(&straight, q).height);
                    }
                    if (b < .05f)
                        continue; // Exclude near-zero reference while fading/expired.
                    float ratio = a / b;
                    minimum = fminf(minimum, ratio);
                    maximum = fmaxf(maximum, ratio);
                    ++eligible;
                    uncovered += ratio < .25f;
                }
                fprintf(out, "%.9g,%u,%d,%u,%.9g,%.9g,", (double)radius, ages[ai] / 120, side,
                    eligible, (double)(eligible * spacing), (double)(uncovered * spacing));
                if (eligible)
                    fprintf(out, "%.9g,%.9g", (double)minimum, (double)maximum);
                else
                    fputc(',', out);
                fprintf(out, ",%.9g,2\n", (double)spacing);
            }
    }
    CHECK(fclose(out) == 0);
    puts("turn_probes=ok: source arc versus matched straight; signed whole fields; 0.05m radial "
         "search");
    return 0;
}
