#include "renderer3d.h"
#include "waterfront_corner.h"
#include "waterfront_lighting.h"
#include "waterfront_scene.h"

#include "raymath.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Lighting check failed at line %d: %s\n", __LINE__, #condition);       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// Every member is a float, so the presets compare and difference as plain arrays.
enum { FIELDS = sizeof(WaterfrontLighting) / sizeof(float) };
_Static_assert(sizeof(WaterfrontLighting) % sizeof(float) == 0, "lighting holds only floats");

static const float *fields(const WaterfrontLighting *l) { return (const float *)l; }

static bool same(WaterfrontLighting a, WaterfrontLighting b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

static bool same3(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// The sun is checked by angle; its members come first after the phase.
static bool sun_field(size_t i)
{
    size_t first = offsetof(WaterfrontLighting, sun_direction) / sizeof(float);
    return i >= first && i < first + 3;
}

static int check_presets(void)
{
    // Phase zero is the original daytime look: the constants the renderers used before.
    WaterfrontLighting day = waterfront_lighting(WATERFRONT_DAYLIGHT_DAY);
    CHECK(same3(day.sun_direction, Vector3Normalize((Vector3) { .10f, .48f, -.87f })));
    CHECK(same3(day.ambient, (Vector3) { .48f, .57f, .65f }));
    CHECK(same3(day.direct, (Vector3) { 1.0f, .92f, .74f }) && day.direct_intensity == .65f);
    CHECK(day.cpu_ambient == .30f && day.cpu_direct == .70f);
    CHECK(same3(day.cpu_tint, (Vector3) { 1, 1, 1 }));
    CHECK(same3(day.fog, (Vector3) { 186.0f / 255.0f, 229.0f / 255.0f, 244.0f / 255.0f }));
    CHECK(day.fog_start == 145.0f && day.fog_end == 620.0f && day.fog_opacity == .38f);
    CHECK(same3(day.water, (Vector3) { 1, 1, 1 }) && day.water_shadow == .55f);
    CHECK(same3(day.glint, (Vector3) { 1.0f, .96f, .84f }));
    CHECK(same3(day.highlight, (Vector3) { 1, 1, 1 }));
    CHECK(day.window_light == 0 && day.glow_strength == 0);
    // A zeroed phase is day; out-of-range phases clamp; fixed phases return their preset.
    WaterfrontLighting zero = { 0 };
    CHECK(same(waterfront_lighting(zero.phase), day));
    CHECK(same(waterfront_lighting(-1), day));
    CHECK(same(waterfront_lighting(NAN), day));
    CHECK(same(waterfront_lighting(2), waterfront_lighting(WATERFRONT_DAYLIGHT_DUSK)));
    WaterfrontLighting sunset = waterfront_lighting(WATERFRONT_DAYLIGHT_SUNSET);
    WaterfrontLighting dusk = waterfront_lighting(WATERFRONT_DAYLIGHT_DUSK);
    CHECK(sunset.phase == WATERFRONT_DAYLIGHT_SUNSET && dusk.phase == WATERFRONT_DAYLIGHT_DUSK);
    // The sun sinks: low and warm at sunset, set at dusk with no direct light or glitter.
    CHECK(sunset.sun_direction.y > 0 && sunset.sun_direction.y < day.sun_direction.y);
    CHECK(dusk.sun_direction.y < 0 && dusk.direct_intensity == 0);
    CHECK(same3(dusk.glint, (Vector3) { 0 }) && dusk.water_shadow == 1);
    CHECK(sunset.direct.z < sunset.direct.x && dusk.window_light == 1);
    CHECK(dusk.sky_zenith.x + dusk.sky_zenith.y + dusk.sky_zenith.z
        < day.sky_zenith.x + day.sky_zenith.y + day.sky_zenith.z);
    // The CPU flat shading never exceeds full brightness.
    for (float p = 0; p <= 1; p += .125f) {
        WaterfrontLighting l = waterfront_lighting(p);
        float peak = (l.cpu_ambient + l.cpu_direct)
            * fmaxf(l.cpu_tint.x, fmaxf(l.cpu_tint.y, l.cpu_tint.z));
        CHECK(peak <= 1.0001f);
    }
    return 0;
}

// No sudden jump: every step of a fine sweep moves each value by at most its share of the
// difference between neighbouring presets, and the sun turns smoothly while staying unit.
static int check_continuity(void)
{
    const WaterfrontLighting presets[] = { waterfront_lighting(WATERFRONT_DAYLIGHT_DAY),
        waterfront_lighting(WATERFRONT_DAYLIGHT_SUNSET),
        waterfront_lighting(WATERFRONT_DAYLIGHT_DUSK) };
    const float step = 1.0f / 2048;
    float span[FIELDS] = { 0 }, elevation = 0;
    for (int k = 0; k < 2; ++k) {
        for (size_t i = 0; i < FIELDS; ++i)
            span[i] = fmaxf(span[i], fabsf(fields(&presets[k + 1])[i] - fields(&presets[k])[i]));
        elevation = fmaxf(elevation,
            fabsf(asinf(presets[k + 1].sun_direction.y) - asinf(presets[k].sun_direction.y)));
    }
    WaterfrontLighting previous = waterfront_lighting(0);
    for (int n = 1; n <= 2048; ++n) {
        WaterfrontLighting l = waterfront_lighting((float)n * step);
        CHECK(fabsf(Vector3Length(l.sun_direction) - 1) < 1e-5f);
        float turn
            = acosf(Clamp(Vector3DotProduct(l.sun_direction, previous.sun_direction), -1, 1));
        CHECK(turn <= 2 * step * elevation * 1.01f + 1e-3f);
        for (size_t i = 1; i < FIELDS; ++i)
            if (!sun_field(i))
                CHECK(fabsf(fields(&l)[i] - fields(&previous)[i])
                    <= 2 * step * span[i] * 1.01f + 1e-5f);
        previous = l;
    }
    return 0;
}

static int check_sky(void)
{
    WaterfrontLighting day = waterfront_lighting(WATERFRONT_DAYLIGHT_DAY);
    WaterfrontLighting dusk = waterfront_lighting(WATERFRONT_DAYLIGHT_DUSK);
    WaterfrontLighting hazy = day;
    hazy.fog_opacity = .9f;
    hazy.window_light = 1;
    CHECK(waterfront_lighting_same_sky(&day, &hazy));
    CHECK(!waterfront_lighting_same_sky(&day, &dusk));
    Image image = waterfront_sky_image(&day);
    size_t bytes = (size_t)image.width * (size_t)image.height * sizeof(Color);
    Color *painted = malloc(bytes);
    CHECK(painted);
    memcpy(painted, image.data, bytes);
    // A light outside the sky reuses the panorama; the dusk sky repaints it, darker overall.
    CHECK(
        waterfront_sky_image(&hazy).data == image.data && memcmp(image.data, painted, bytes) == 0);
    const Color *sky = waterfront_sky_image(&dusk).data;
    double day_sum = 0, dusk_sum = 0;
    for (int i = 0; i < image.width * image.height; ++i) {
        day_sum += painted[i].r + painted[i].g + painted[i].b;
        dusk_sum += sky[i].r + sky[i].g + sky[i].b;
    }
    bool darker = dusk_sum < day_sum * .6;
    // Returning to day restores the original panorama exactly.
    bool restored = memcmp(waterfront_sky_image(&day).data, painted, bytes) == 0;
    free(painted);
    CHECK(darker && restored);
    return 0;
}

// About three windows in eight are occupied, in every glass colour, and a window's state
// depends only on its seed, so it stays put as the camera moves.
static int check_windows(void)
{
    int occupied = 0, total = 0, by_glass[3] = { 0 }, glass_total[3] = { 0 };
    for (size_t triangle = 0; triangle < 400; ++triangle)
        for (int floor = 0; floor < 6; ++floor)
            for (int column = 0; column < 8; ++column) {
                unsigned int seed = waterfront_window_seed(triangle, column, floor);
                CHECK(waterfront_window_occupied(seed) == waterfront_window_occupied(seed));
                Color glass = waterfront_window_glass(seed);
                int kind = glass.r == 138 ? 0 : glass.r == 181 ? 1 : 2;
                bool lit = waterfront_window_occupied(seed);
                occupied += lit;
                by_glass[kind] += lit;
                ++glass_total[kind];
                ++total;
            }
    CHECK(fabs((double)occupied / total - .375) < .02);
    for (int kind = 0; kind < 3; ++kind)
        CHECK(fabs((double)by_glass[kind] / glass_total[kind] - .375) < .04);
    return 0;
}

static int check_parse(void)
{
    float phase = -1;
    CHECK(waterfront_daylight_parse("day", &phase) && phase == WATERFRONT_DAYLIGHT_DAY);
    CHECK(waterfront_daylight_parse("sunset", &phase) && phase == WATERFRONT_DAYLIGHT_SUNSET);
    CHECK(waterfront_daylight_parse("dusk", &phase) && phase == WATERFRONT_DAYLIGHT_DUSK);
    CHECK(waterfront_daylight_parse("0.25", &phase) && phase == .25f);
    const char *invalid[] = { "", "noon", "-0.1", "1.5", "0.5x", "nan" };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i)
        CHECK(!waterfront_daylight_parse(invalid[i], &phase));
    return 0;
}

// Begin restores the daytime flat shading whatever the previous frame set.
static int check_renderer(void)
{
    Renderer3D renderer = { 0 };
    Color pixels[16 * 16];
    View3D camera = { .position = { 0, 0, 0 },
        .target = { 0, 0, 1 },
        .up = { 0, 1, 0 },
        .vertical_fov = 1,
        .near_plane = .1f,
        .far_plane = 10 };
    bool begun = renderer3d_begin(&renderer, pixels, 16, 16, camera, (Vector3) { 0, 1, 0 });
    renderer.ambient = .5f;
    renderer.tint = (Vector3) { .2f, .3f, .4f };
    renderer.emissive_tint = (Vector3) { .5f, .5f, .5f };
    begun = begun && renderer3d_begin(&renderer, pixels, 16, 16, camera, (Vector3) { 0, 1, 0 });
    bool reset = renderer.ambient == .30f && renderer.diffuse == .70f
        && same3(renderer.tint, (Vector3) { 1, 1, 1 })
        && same3(renderer.emissive_tint, (Vector3) { 1, 1, 1 });
    renderer3d_destroy(&renderer);
    CHECK(begun && reset);
    return 0;
}

int main(void)
{
    int failed = check_presets() || check_continuity() || check_sky() || check_windows()
        || check_parse() || check_renderer();
    if (!failed)
        printf("Waterfront lighting checks passed.\n");
    return failed;
}
