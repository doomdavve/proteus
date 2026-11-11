// W1 diagnostic sampler: production water query, prescribed source paths, no hull dynamics.
#include "core/boat_water.h"
#include "raylib.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Vector2 path(const char *name, float t, Vector2 *heading)
{
    Vector2 p, v;
    if (!strcmp(name, "loop")) {
        float a = 8 * t / 12;
        p = (Vector2) { 12 * sinf(a), 12 * cosf(a) };
        v = (Vector2) { cosf(a), -sinf(a) };
    } else if (!strcmp(name, "s-turn")) {
        float a = PI * (t / 6 - 1);
        p = (Vector2) { 8 * t - 48, 12 * sinf(a) };
        v = (Vector2) { 8, 2 * PI * cosf(a) };
    } else {
        p = (Vector2) { 8 * t - 48, 0 };
        v = (Vector2) { 1, 0 };
    }
    *heading = Vector2Normalize(v);
    return p;
}

int main(int argc, char **argv)
{
    if (argc != 3
        || (strcmp(argv[1], "straight") && strcmp(argv[1], "loop") && strcmp(argv[1], "s-turn")
            && strcmp(argv[1], "crossing") && strcmp(argv[1], "packet"))) {
        fprintf(stderr, "Usage: wake_baseline straight|loop|s-turn|crossing|packet TIME\n");
        return 1;
    }
    char *end;
    float time = strtof(argv[2], &end);
    bool packet = !strcmp(argv[1], "packet");
    if (end == argv[2] || *end || !isfinite(time) || time < (packet ? 0 : 12)) {
        fprintf(stderr, "TIME must be finite and >= 12 (>= 0 for an isolated packet).\n");
        return 1;
    }
    BoatWater water = { .player_wake_enabled = true };
    // Reproduce the old emitter's common offset at both interval ends. 30 samples/source;
    // two trains fit the 64-slot history, so this experiment cannot confuse expiry with overwrite.
    int sources = !strcmp(argv[1], "crossing") ? 2 : 1;
    for (int source = 0; source < sources; ++source)
        for (int i = 1; i <= 30; ++i) {
            float t = i * .4f;
            Vector2 heading, ignored;
            Vector2 a = path(argv[1], t - .4f, &ignored);
            Vector2 b = path(argv[1], t, &heading);
            Vector2 offset = Vector2Scale(heading, -2);
            a = Vector2Add(a, offset);
            b = Vector2Add(b, offset);
            if (source) {
                a = (Vector2) { -a.y, a.x };
                b = (Vector2) { -b.y, b.x };
            }
            boat_water_emit_wake(&water, a, b, t, .4f, 8);
        }
    if (packet) {
        water = (BoatWater) { .player_wake_enabled = true };
        boat_water_emit_wake(&water, (Vector2) { -1.6f, 0 }, (Vector2) { 1.6f, 0 }, .2f, .4f, 8);
    }
    if (water.player_wake_count != (packet ? 1 : sources * 30)) {
        fprintf(
            stderr, "Baseline history capacity changed; this probe requires retained sources.\n");
        return 1;
    }
    // Batch points from stdin keep Python plotting independent of a duplicate old-wave recipe.
    float x, z;
    while (scanf("%f %f", &x, &z) == 2) {
        BoatWaterSample q = boat_water_query(water, (Vector2) { x, z }, time);
        printf("%.9g %.9g %.9g %.9g\n", q.height, -q.normal.x / q.normal.y,
            -q.normal.z / q.normal.y, q.velocity.y);
    }
    return ferror(stdin) || ferror(stdout) ? 1 : 0;
}
