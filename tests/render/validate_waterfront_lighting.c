#include "render/boat_view.h"
#include "render/renderer3d.h"
#include "render/waterfront_corner.h"
#include "render/waterfront_lighting.h"
#include "render/waterfront_scene.h"

#include "raylib.h"
#include "raymath.h"

#include <float.h>
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

// Largest expected movement of one field over one dense-sweep step. Values are classified by
// their declared groups: normalised light is tight, byte sky colours and distances are loose.
static float field_step_limit(size_t i)
{
    if (i == offsetof(WaterfrontLighting, phase) / sizeof(float))
        return 0;
    if (sun_field(i))
        return .02f;
    size_t sky_horizon = offsetof(WaterfrontLighting, sky_horizon) / sizeof(float);
    size_t sky_zenith = offsetof(WaterfrontLighting, sky_zenith) / sizeof(float);
    size_t sun_halo = offsetof(WaterfrontLighting, sun_halo) / sizeof(float);
    size_t cloud_underside = offsetof(WaterfrontLighting, cloud_underside) / sizeof(float);
    if ((i >= sky_horizon && i < sky_horizon + 3) || (i >= sky_zenith && i < sky_zenith + 3)
        || (i >= sun_halo && i < sun_halo + 9) || (i >= cloud_underside && i < cloud_underside + 6))
        return 3.0f;
    if (i == offsetof(WaterfrontLighting, fog_start) / sizeof(float)
        || i == offsetof(WaterfrontLighting, fog_end) / sizeof(float))
        return 3.0f;
    return .08f;
}

static int check_presets(void)
{
    // Phase zero retains the established daytime scene light, fog and water.
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

// Independent CPU ray mapping over the full reference panorama, following the documented
// U/V contract. It does not call the production draw path.
static Color reference_sky_sample(
    Image reference, View3D camera, int width, int height, int x, int y)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(camera.up, forward));
    Vector3 up = Vector3CrossProduct(forward, right);
    float scale = 2 * tanf(camera.vertical_fov * .5f) / height;
    Vector3 row = Vector3Add(forward, Vector3Scale(up, (height * .5f - y - .5f) * scale));
    Vector3 ray
        = Vector3Normalize(Vector3Add(row, Vector3Scale(right, (x + .5f - width * .5f) * scale)));
    float u = atan2f(ray.x, ray.z) / (2 * PI) + .5f;
    int sx = (int)(u * reference.width) % reference.width;
    int sy = (int)Clamp((1 - ray.y) * .5f * reference.height, 0, reference.height - 1);
    return ((const Color *)reference.data)[sy * reference.width + sx];
}

static int check_sky_draw(void)
{
    View3D cameras[] = {
        { .position = { 0, 3, 0 },
            .target = { 0, 3, 1 },
            .up = { 0, 1, 0 },
            .vertical_fov = 1.1f,
            .near_plane = .1f,
            .far_plane = 100 },
        { .position = { 12, 4, -6 },
            .target = { -3, 1, 20 },
            .up = { 0, 1, 0 },
            .vertical_fov = 1.4f,
            .near_plane = .1f,
            .far_plane = 100 },
        { .position = { -8, 2, 5 },
            .target = { 9, 6, -4 },
            .up = { 0, 1, 0 },
            .vertical_fov = .8f,
            .near_plane = .1f,
            .far_plane = 100 },
    };
    const int widths[] = { 320, 97 }, heights[] = { 200, 61 };
    Color drawn[320 * 200], alternate[320 * 200];
    struct {
        bool legacy;
        float value;
    } cases[] = { { true, WATERFRONT_DAYLIGHT_DAY }, { true, WATERFRONT_DAYLIGHT_SUNSET },
        { true, WATERFRONT_DAYLIGHT_DUSK }, { false, 4.5f }, { false, 6 }, { false, 12 },
        { false, 18 }, { false, 19.5f }, { false, 21.5f }, { false, 23.9999f }, { false, .0001f } };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        WaterfrontLighting lighting;
        if (cases[c].legacy)
            lighting = waterfront_lighting(cases[c].value);
        else
            CHECK(waterfront_lighting_hour(cases[c].value, &lighting));
        Image reference = waterfront_sky_image(&lighting);
        for (int size = 0; size < 2; ++size) {
            int width = widths[size], height = heights[size];
            for (int view = 0; view < (size == 0 ? 3 : 1); ++view) {
                View3D camera = cameras[view];
                WaterfrontSkyStats before = waterfront_sky_stats();
                waterfront_sky_draw(drawn, width, height, camera, &lighting);
                WaterfrontSkyStats after = waterfront_sky_stats();
                CHECK(after.draw_calls == before.draw_calls + 1);
                bool first_sky = after.draw_generations != before.draw_generations;
                CHECK(after.draw_generations == before.draw_generations + (size == 0 && view == 0));
                CHECK(!first_sky || after.draw_samples > before.draw_samples);
                CHECK(after.draw_samples >= before.draw_samples);
                CHECK(
                    after.draw_samples - before.draw_samples <= (unsigned long long)width * height);
                CHECK(after.reference_samples == before.reference_samples);
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                        CHECK(ColorIsEqual(drawn[y * width + x],
                            reference_sky_sample(reference, camera, width, height, x, y)));

                // A fixed light reuses the visible cache without new recipe work.
                before = waterfront_sky_stats();
                waterfront_sky_draw(alternate, width, height, camera, &lighting);
                after = waterfront_sky_stats();
                CHECK(after.draw_samples == before.draw_samples);
                CHECK(after.draw_reused - before.draw_reused == (unsigned long long)width * height);
                CHECK(memcmp(drawn, alternate, (size_t)width * height * sizeof(Color)) == 0);

                // A weather squall is applied after the cached sky, frame by frame.
                WaterfrontSquall squall = {
                    .centre = { -24, 70 }, .radius = 110, .base = 190, .top = 520, .grown = .8f
                };
                waterfront_sky_draw_squall(alternate, width, height, camera, squall, &lighting);
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x) {
                        Vector3 forward
                            = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
                        Vector3 right = Vector3Normalize(Vector3CrossProduct(camera.up, forward));
                        Vector3 up = Vector3CrossProduct(forward, right);
                        float scale = 2 * tanf(camera.vertical_fov * .5f) / height;
                        Vector3 row = Vector3Add(
                            forward, Vector3Scale(up, (height * .5f - y - .5f) * scale));
                        Vector3 ray = Vector3Normalize(
                            Vector3Add(row, Vector3Scale(right, (x + .5f - width * .5f) * scale)));
                        Color base = reference_sky_sample(reference, camera, width, height, x, y);
                        CHECK(ColorIsEqual(alternate[y * width + x],
                            waterfront_squall_sky(base, camera.position, ray, squall)));
                    }
            }
        }
    }

    // Direct/backward seeks and alternating lights are deterministic and invalidate exactly the
    // adopted sky, leaving no stale texel from the previous hour.
    WaterfrontLighting seek[4];
    CHECK(waterfront_lighting_hour(6.5f, &seek[0]));
    CHECK(waterfront_lighting_hour(21.5f, &seek[1]));
    CHECK(waterfront_lighting_hour(6.5f, &seek[2]));
    CHECK(waterfront_lighting_hour(18.5f, &seek[3]));
    View3D camera = cameras[0];
    Color first[320 * 200];
    WaterfrontSkyStats before = waterfront_sky_stats();
    waterfront_sky_draw(first, 320, 200, camera, &seek[0]);
    unsigned long long changed = waterfront_sky_stats().draw_generations;
    waterfront_sky_draw(alternate, 320, 200, camera, &seek[1]);
    CHECK(waterfront_sky_stats().draw_generations == changed + 1);
    waterfront_sky_draw(drawn, 320, 200, camera, &seek[2]);
    CHECK(memcmp(first, drawn, sizeof(first)) == 0);
    waterfront_sky_draw(drawn, 320, 200, camera, &seek[3]);
    // The final seek's full panorama is painted only now, after the draws, so it is an
    // independent storage/traversal check of the same shared texel recipe.
    Image final_reference = waterfront_sky_image(&seek[3]);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x)
            CHECK(ColorIsEqual(
                drawn[y * 320 + x], reference_sky_sample(final_reference, camera, 320, 200, x, y)));
    // Each changed sky evaluates only visible texels, never the full 1024x512 panorama.
    CHECK(waterfront_sky_stats().draw_samples - before.draw_samples <= 4ull * 320 * 200);
    CHECK(waterfront_sky_stats().draw_samples - before.draw_samples < 1024ull * 512);
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
    // The GPU's static mask is stable, independent of the light, and actually has cloud flags.
    Image mask = waterfront_sky_cloud_mask();
    size_t mask_bytes = (size_t)mask.width * (size_t)mask.height * sizeof(Color);
    Color *mask_pixels = malloc(mask_bytes);
    CHECK(mask_pixels);
    memcpy(mask_pixels, mask.data, mask_bytes);
    int underside = 0, rim = 0, tones[3] = { 0 };
    for (int i = 0; i < mask.width * mask.height; ++i) {
        underside += mask_pixels[i].r != 0;
        rim += mask_pixels[i].g != 0;
        if (mask_pixels[i].r) {
            CHECK(mask_pixels[i].b == 85 || mask_pixels[i].b == 170 || mask_pixels[i].b == 255);
            ++tones[mask_pixels[i].b / 85 - 1];
        } else {
            CHECK(mask_pixels[i].b == 0);
        }
    }
    CHECK(mask.width == 1024 && mask.height == 512 && underside > 1000 && rim > 1000);
    CHECK(tones[0] > 100 && tones[1] > 100 && tones[2] > 100);
    (void)waterfront_sky_image(&dusk);
    CHECK(waterfront_sky_cloud_mask().data == mask.data
        && memcmp(waterfront_sky_cloud_mask().data, mask_pixels, mask_bytes) == 0);
    free(mask_pixels);
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

static int check_hour_input(void)
{
    float hour = -123;
    CHECK(waterfront_hour_normalize(0, &hour) && hour == 0 && !signbit(hour));
    CHECK(waterfront_hour_normalize(-0.0f, &hour) && hour == 0 && !signbit(hour));
    CHECK(waterfront_hour_normalize(24, &hour) && hour == 0);
    CHECK(waterfront_hour_normalize(-1, &hour) && hour == 23);
    CHECK(waterfront_hour_normalize(25, &hour) && hour == 1);
    CHECK(waterfront_hour_normalize(-1e-6f, &hour) && hour >= 0 && hour < 24);
    CHECK(waterfront_hour_normalize(-FLT_TRUE_MIN, &hour) && hour >= 0 && hour < 24);
    CHECK(waterfront_hour_normalize(FLT_MAX, &hour) && hour >= 0 && hour < 24);
    CHECK(waterfront_hour_normalize(-FLT_MAX, &hour) && hour >= 0 && hour < 24);
    hour = -123;
    CHECK(!waterfront_hour_normalize(NAN, &hour) && hour == -123);
    CHECK(!waterfront_hour_normalize(INFINITY, &hour) && hour == -123);
    CHECK(!waterfront_hour_normalize(-INFINITY, &hour) && hour == -123);

    CHECK(waterfront_hour_parse("0", &hour) && hour == 0);
    CHECK(waterfront_hour_parse("-1", &hour) && hour == 23);
    CHECK(waterfront_hour_parse("24", &hour) && hour == 0);
    CHECK(waterfront_hour_parse("25.5", &hour) && hour == 1.5f);
    const char *invalid[] = { "", "noon", "1x", "nan", "inf", "-inf", "1e999" };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        hour = -123;
        CHECK(!waterfront_hour_parse(invalid[i], &hour) && hour == -123);
    }

    // Invalid evaluation is explicit and does not overwrite a caller's existing frame.
    WaterfrontLighting sentinel = waterfront_lighting(WATERFRONT_DAYLIGHT_DAY), out = sentinel;
    CHECK(!waterfront_lighting_hour(NAN, &out) && same(out, sentinel));
    CHECK(!waterfront_lighting_hour(INFINITY, &out) && same(out, sentinel));
    return 0;
}

static int check_hour_looks(void)
{
    // All integer hours are usable; the legacy phase member never holds a clock hour.
    for (int h = 0; h < 24; ++h) {
        WaterfrontLighting l;
        CHECK(waterfront_lighting_hour((float)h, &l));
        CHECK(l.phase == WATERFRONT_DAYLIGHT_DAY);
        CHECK(isfinite(l.sun_direction.x) && isfinite(l.sun_direction.y)
            && isfinite(l.sun_direction.z));
        CHECK(fabsf(Vector3Length(l.sun_direction) - 1) < 1e-5f);
        float horizontal
            = sqrtf(l.sun_direction.x * l.sun_direction.x + l.sun_direction.z * l.sun_direction.z);
        CHECK(horizontal > .4f); // Below the noon zenith, so no shadow direction collapses.
        if (l.sun_direction.y <= 0) {
            CHECK(l.direct_intensity == 0 && l.cpu_direct == 0 && l.disc_strength == 0);
            CHECK(same3(l.glint, (Vector3) { 0 }) && same3(l.highlight, (Vector3) { 0 }));
            CHECK(l.water_shadow == 1);
        }
    }

    WaterfrontLighting midnight, day, night, dusk, sunrise, sunset;
    CHECK(waterfront_lighting_hour(24, &midnight));
    CHECK(waterfront_lighting_hour(0, &midnight) && midnight.window_light == 1);
    CHECK(midnight.direct_intensity == 0 && midnight.cpu_direct == 0);
    CHECK(waterfront_lighting_hour(12, &day) && day.direct_intensity > 0 && day.window_light == 0);
    CHECK(waterfront_lighting_hour(21.5f, &night));
    CHECK(waterfront_lighting_hour(19.5f, &dusk));
    CHECK(night.sky_zenith.x + night.sky_zenith.y + night.sky_zenith.z
        < dusk.sky_zenith.x + dusk.sky_zenith.y + dusk.sky_zenith.z);
    CHECK(!same3(night.cpu_tint, dusk.cpu_tint));

    // Sunrise and sunset are on opposite sides of the scene.
    CHECK(waterfront_lighting_hour(6, &sunrise));
    CHECK(waterfront_lighting_hour(18, &sunset));
    Vector3 rise
        = Vector3Normalize((Vector3) { sunrise.sun_direction.x, 0, sunrise.sun_direction.z });
    Vector3 set = Vector3Normalize((Vector3) { sunset.sun_direction.x, 0, sunset.sun_direction.z });
    CHECK(Vector3DotProduct(rise, set) < -.999f);

    // A five-degree fade: just above the horizon each direct effect is only a small share of its
    // anchor value, not already fully lit.
    WaterfrontLighting just_above, low_sun;
    CHECK(waterfront_lighting_hour(6.1f, &just_above));
    CHECK(just_above.sun_direction.y > 0);
    CHECK(just_above.direct_intensity > 0 && just_above.direct_intensity < .005f);
    CHECK(just_above.cpu_direct > 0 && just_above.cpu_direct < .01f);
    CHECK(just_above.disc_strength > 0 && just_above.disc_strength < .01f);
    CHECK(Vector3Length(just_above.glint) > 0 && Vector3Length(just_above.glint) < .02f);
    CHECK(waterfront_lighting_hour(6.3f, &low_sun));
    CHECK(low_sun.direct_intensity > .05f && low_sun.direct_intensity < .2f);
    CHECK(low_sun.cpu_direct > .03f && low_sun.cpu_direct < .15f);

    // Midnight wraps continuously and evaluation is independent of the previous hour.
    WaterfrontLighting fresh, forward, backward;
    CHECK(waterfront_lighting_hour(23.9999f, &fresh));
    CHECK(fresh.sun_direction.y < 0 && fresh.direct_intensity == 0);
    CHECK(waterfront_lighting_hour(1e-6f, &fresh));
    CHECK(fresh.sun_direction.y < 0 && fresh.direct_intensity == 0);
    CHECK(waterfront_lighting_hour(17.3f, &fresh));
    for (float h = 0; h < 17.3f; h += .37f)
        CHECK(waterfront_lighting_hour(h, &forward));
    CHECK(waterfront_lighting_hour(17.3f, &forward));
    for (float h = 23.9f; h > 17.3f; h -= .29f)
        CHECK(waterfront_lighting_hour(h, &backward));
    CHECK(waterfront_lighting_hour(17.3f, &backward));
    WaterfrontLighting reordered;
    const float order[] = { 12, 3, 23, 7, 17.3f };
    for (size_t i = 0; i < sizeof(order) / sizeof(*order); ++i)
        CHECK(waterfront_lighting_hour(order[i], &reordered));
    CHECK(same(fresh, forward) && same(fresh, backward) && same(fresh, reordered));

    // A dense sweep is continuous, unit and finite across every anchor and the midnight seam.
    WaterfrontLighting previous;
    CHECK(waterfront_lighting_hour(0, &previous));
    for (int n = 1; n <= 2048; ++n) {
        float hour = (float)n * 24.0f / 2048.0f;
        WaterfrontLighting l;
        CHECK(waterfront_lighting_hour(hour, &l));
        CHECK(fabsf(Vector3Length(l.sun_direction) - 1) < 1e-5f);
        float turn
            = acosf(Clamp(Vector3DotProduct(l.sun_direction, previous.sun_direction), -1, 1));
        CHECK(turn < .02f);
        for (size_t i = 0; i < FIELDS; ++i) {
            CHECK(isfinite(fields(&l)[i]));
            CHECK(fabsf(fields(&l)[i] - fields(&previous)[i]) <= field_step_limit(i));
        }
        previous = l;
    }
    WaterfrontLighting wrapped;
    CHECK(waterfront_lighting_hour(24, &wrapped));
    WaterfrontLighting zero;
    CHECK(waterfront_lighting_hour(0, &zero));
    CHECK(same(wrapped, zero));
    return 0;
}

// The glitter clock is a history-independent integral of the authored glint_speed recipe: anchored
// at noon, continuous across midnights and anchors, periodic in its daily increment, and exactly
// the derivative the instantaneous evaluator reports away from seams.
static int check_glitter_clock(void)
{
    const double cycle = 37.0, step = 1e-4;
    double seconds = -1;
    CHECK(waterfront_glitter_clock(12.0, cycle, &seconds) && seconds == 0);

    // The clock's derivative is the same authored speed the light reports, away from seams.
    const double hours[] = { -2.3, .7, 3.1, 5.2, 7.9, 10.6, 13.4, 16.7, 18.8, 20.9, 23.2, 26.1 };
    for (size_t i = 0; i < sizeof(hours) / sizeof(*hours); ++i) {
        double before, after;
        CHECK(waterfront_glitter_clock(hours[i] - step, cycle, &before));
        CHECK(waterfront_glitter_clock(hours[i] + step, cycle, &after));
        WaterfrontLighting lighting;
        CHECK(waterfront_lighting_hour((float)hours[i], &lighting));
        double speed = (after - before) / (2 * step) * 24.0 / cycle;
        CHECK(fabs(speed - lighting.glint_speed) < 1e-3);
    }

    // Independent midpoint integration agrees over more than three unwrapped days, including a
    // negative start, without calling the analytic primitive.
    const double start = -5.3, end = 76.7;
    const int steps = 60000;
    double width = (end - start) / steps, total = 0;
    for (int n = 0; n < steps; ++n) {
        WaterfrontLighting lighting;
        CHECK(waterfront_lighting_hour((float)(start + (n + .5) * width), &lighting));
        total += lighting.glint_speed * width;
    }
    double first, last;
    CHECK(waterfront_glitter_clock(start, cycle, &first));
    CHECK(waterfront_glitter_clock(end, cycle, &last));
    CHECK(fabs((last - first) * 24.0 / cycle - total) < 1e-2);

    // The integral has no jump at midnight or at any authored anchor.
    const double seams[] = { -24, 0, 4.5, 6, 9, 12, 15, 18, 19.5, 21.5, 24, 48 };
    const double gap = 1e-6;
    for (size_t i = 0; i < sizeof(seams) / sizeof(*seams); ++i) {
        double before, at, after;
        CHECK(waterfront_glitter_clock(seams[i] - gap, cycle, &before));
        CHECK(waterfront_glitter_clock(seams[i], cycle, &at));
        CHECK(waterfront_glitter_clock(seams[i] + gap, cycle, &after));
        CHECK(fabs(at - before) < 1e-4 && fabs(after - at) < 1e-4);
    }

    // Every additional day adds the same integral, forward or backward.
    double at_zero, at_day;
    CHECK(waterfront_glitter_clock(0, cycle, &at_zero));
    CHECK(waterfront_glitter_clock(24, cycle, &at_day));
    double day_increment = at_day - at_zero;
    CHECK(day_increment > 0);
    const double bases[] = { -13.7, 0, 7.3, 12, 23.9 };
    for (size_t i = 0; i < sizeof(bases) / sizeof(*bases); ++i) {
        double value;
        CHECK(waterfront_glitter_clock(bases[i], cycle, &value));
        for (int k = 1; k <= 3; ++k) {
            double later, earlier;
            CHECK(waterfront_glitter_clock(bases[i] + 24.0 * k, cycle, &later));
            CHECK(waterfront_glitter_clock(bases[i] - 24.0 * k, cycle, &earlier));
            CHECK(fabs((later - value) - k * day_increment) < 1e-8);
            CHECK(fabs((earlier - value) + k * day_increment) < 1e-8);
        }
    }

    // Direct, backward and reordered seeks are bit-identical: there is no accumulated history.
    double expected, direct, backward, reordered;
    CHECK(waterfront_glitter_clock(17.3, cycle, &expected));
    CHECK(waterfront_glitter_clock(17.3, cycle, &direct));
    const double order[] = { 23.9, 3.2, 40.5, -8.0, 0 };
    for (size_t i = 0; i < sizeof(order) / sizeof(*order); ++i)
        CHECK(waterfront_glitter_clock(order[i], cycle, &reordered));
    CHECK(waterfront_glitter_clock(17.3, cycle, &backward));
    CHECK(waterfront_glitter_clock(17.3, cycle, &reordered));
    CHECK(direct == expected && backward == expected && reordered == expected);

    // Invalid input, including an overflowing result, leaves the caller's value untouched.
    double sentinel = 123.25, out = sentinel;
    CHECK(!waterfront_glitter_clock(NAN, cycle, &out) && out == sentinel);
    CHECK(!waterfront_glitter_clock(INFINITY, cycle, &out) && out == sentinel);
    CHECK(!waterfront_glitter_clock(-INFINITY, cycle, &out) && out == sentinel);
    CHECK(!waterfront_glitter_clock(5, NAN, &out) && out == sentinel);
    CHECK(!waterfront_glitter_clock(5, INFINITY, &out) && out == sentinel);
    CHECK(!waterfront_glitter_clock(5, -INFINITY, &out) && out == sentinel);
    CHECK(!waterfront_glitter_clock(5, 0, &out) && out == sentinel);
    CHECK(!waterfront_glitter_clock(5, -1, &out) && out == sentinel);
    CHECK(!waterfront_glitter_clock(1e300, DBL_MAX, &out) && out == sentinel);
    return 0;
}

// The shared view clock resolves a working-harbour lighting cycle without touching history: its
// normalized hour and analytic glitter clock are written only for valid props, and every seek is
// bit-identical.
static int check_view_clock(void)
{
    BoatViewState view = { 0 };
    BoatViewProps props = { 0 };
    float hour = -123.5f;
    double seconds = -456.25;
    CHECK(!boat_view_lighting_clock(NULL, &hour, &seconds));
    CHECK(!boat_view_lighting_clock(&view, &hour, &seconds));
    CHECK(hour == -123.5f && seconds == -456.25);
    view.props = &props;
    CHECK(!boat_view_lighting_clock(&view, &hour, &seconds));
    CHECK(hour == -123.5f && seconds == -456.25);

    // A valid cycle anchors the glitter clock at noon.
    props.lighting_cycle = true;
    props.lighting_hours = 12;
    props.lighting_cycle_seconds = 37;
    CHECK(boat_view_lighting_clock(&view, &hour, &seconds));
    CHECK(hour == 12 && seconds == 0);

    // Unwrapped hours wrap in double before the float cast; the clock keeps the integral.
    const double unwrapped[] = { 18.5, 36.25, 49.5, -1.5, -24.0 };
    const float expected_hours[] = { 18.5f, 12.25f, 1.5f, 22.5f, 0.0f };
    for (size_t i = 0; i < sizeof(unwrapped) / sizeof(*unwrapped); ++i) {
        props.lighting_hours = unwrapped[i];
        double expected;
        CHECK(waterfront_glitter_clock(unwrapped[i], props.lighting_cycle_seconds, &expected));
        hour = -1;
        seconds = -1;
        CHECK(boat_view_lighting_clock(&view, &hour, &seconds));
        CHECK(hour == expected_hours[i]);
        CHECK(seconds == expected);
    }

    // A double just below midnight casts to the float hour 24 and normalizes to positive zero.
    props.lighting_hours = nextafter(24.0, 0.0);
    CHECK(boat_view_lighting_clock(&view, &hour, &seconds));
    CHECK(hour == 0 && !signbit(hour));

    // Direct, backward and reordered seeks are bit-identical.
    props.lighting_hours = 17.3;
    double expected, direct, backward, reordered;
    CHECK(waterfront_glitter_clock(17.3, props.lighting_cycle_seconds, &expected));
    CHECK(boat_view_lighting_clock(&view, &hour, &direct));
    const double order[] = { 63.2, -8.0, 40.5, 0, 89.9 };
    for (size_t i = 0; i < sizeof(order) / sizeof(*order); ++i) {
        props.lighting_hours = order[i];
        CHECK(boat_view_lighting_clock(&view, &hour, &reordered));
    }
    props.lighting_hours = 17.3;
    CHECK(boat_view_lighting_clock(&view, &hour, &backward));
    CHECK(boat_view_lighting_clock(&view, &hour, &reordered));
    CHECK(direct == expected && backward == expected && reordered == expected);

    // Invalid hours, periods and overflowing clocks leave both outputs untouched.
    props.lighting_hours = 5;
    props.lighting_cycle_seconds = 37;
    const double bad_hours[] = { NAN, INFINITY, -INFINITY };
    for (size_t i = 0; i < sizeof(bad_hours) / sizeof(*bad_hours); ++i) {
        props.lighting_hours = bad_hours[i];
        hour = -123.5f;
        seconds = -456.25;
        CHECK(!boat_view_lighting_clock(&view, &hour, &seconds));
        CHECK(hour == -123.5f && seconds == -456.25);
    }
    props.lighting_hours = 5;
    const double bad_periods[] = { 0, -1, NAN, INFINITY, -INFINITY };
    for (size_t i = 0; i < sizeof(bad_periods) / sizeof(*bad_periods); ++i) {
        props.lighting_cycle_seconds = bad_periods[i];
        hour = -123.5f;
        seconds = -456.25;
        CHECK(!boat_view_lighting_clock(&view, &hour, &seconds));
        CHECK(hour == -123.5f && seconds == -456.25);
    }
    // A finite input can still overflow the analytic integral; it must not commit either output.
    props.lighting_hours = 1e300;
    props.lighting_cycle_seconds = DBL_MAX;
    hour = -123.5f;
    seconds = -456.25;
    CHECK(!boat_view_lighting_clock(&view, &hour, &seconds));
    CHECK(hour == -123.5f && seconds == -456.25);
    return 0;
}

// Wind follows explicit simulation time: changed time repaints visible samples, seeking
// restores bytes, and the periodic field remains continuous across its bounded clock wrap.
static int check_cloud_scroll(void)
{
    WaterfrontLighting light = waterfront_lighting(WATERFRONT_DAYLIGHT_DAY);
    View3D camera = { .position = { 0, 0, 0 },
        .target = { 1, 1, -1 },
        .up = { 0, 1, 0 },
        .vertical_fov = 1,
        .near_plane = .1f,
        .far_plane = 10 };
    enum { WIDTH = 97, HEIGHT = 61 };
    Color baseline[WIDTH * HEIGHT], drawn[WIDTH * HEIGHT];
    waterfront_sky_draw_at(baseline, WIDTH, HEIGHT, camera, (WaterfrontSquall) { 0 }, &light, 0);
    const double times[] = { 240, -60, 19200, 1e12, 0 };
    for (size_t i = 0; i < sizeof(times) / sizeof(*times); ++i) {
        double time = times[i];
        waterfront_sky_draw_at(
            drawn, WIDTH, HEIGHT, camera, (WaterfrontSquall) { 0 }, &light, time);
        Image reference = waterfront_sky_image_at(&light, time);
        for (int y = 0; y < HEIGHT; ++y)
            for (int x = 0; x < WIDTH; ++x)
                CHECK(ColorIsEqual(drawn[y * WIDTH + x],
                    reference_sky_sample(reference, camera, WIDTH, HEIGHT, x, y)));
        if (time == 240)
            CHECK(memcmp(baseline, drawn, sizeof(drawn)) != 0);
        if (time == 0 || time == 19200)
            CHECK(memcmp(baseline, drawn, sizeof(drawn)) == 0);
        WaterfrontSkyStats before = waterfront_sky_stats();
        waterfront_sky_draw_at(
            drawn, WIDTH, HEIGHT, camera, (WaterfrontSquall) { 0 }, &light, time);
        CHECK(waterfront_sky_stats().draw_samples == before.draw_samples);
    }
    Vector2 before = waterfront_cloud_offset(19199.999), after = waterfront_cloud_offset(19200.001);
    CHECK(fabsf(remainderf(before.x - after.x, 32)) < .0001f);
    CHECK(fabsf(remainderf(before.y - after.y, 32)) < .0001f);
    CHECK(waterfront_cloud_offset(INFINITY).x == 0);
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

int main(int argc, char **argv)
{
    // The exhaustive visible-sample sky comparison is registered as a separate slow CTest.
    if (argc == 2 && strcmp(argv[1], "--sky-check") == 0) {
        int failed = check_sky_draw() || check_cloud_scroll();
        if (!failed)
            printf("Waterfront sky drawing checks passed.\n");
        return failed;
    }
    if (argc != 1) {
        fprintf(stderr, "Usage: %s [--sky-check]\n", argv[0]);
        return 2;
    }
    int failed = check_presets() || check_continuity() || check_sky() || check_windows()
        || check_parse() || check_hour_input() || check_hour_looks() || check_glitter_clock()
        || check_view_clock() || check_renderer();
    if (!failed)
        printf("Waterfront lighting checks passed.\n");
    return failed;
}
