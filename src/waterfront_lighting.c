#include "waterfront_lighting.h"

#include "raymath.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// All three presets share the daytime sun's bearing; only its elevation falls.
static Vector3 sun_at(float elevation)
{
    Vector2 bearing = Vector2Normalize((Vector2) { .10f, -.87f });
    return (Vector3) { bearing.x * cosf(elevation), sinf(elevation), bearing.y * cosf(elevation) };
}

// The original daytime constants, unchanged.
static WaterfrontLighting day(void)
{
    return (WaterfrontLighting) {
        .phase = WATERFRONT_DAYLIGHT_DAY,
        .sun_direction = Vector3Normalize((Vector3) { .10f, .48f, -.87f }),
        .ambient = { .48f, .57f, .65f },
        .direct = { 1.0f, .92f, .74f },
        .direct_intensity = .65f,
        .cpu_ambient = .30f,
        .cpu_direct = .70f,
        .cpu_tint = { 1, 1, 1 },
        .sky_horizon = { 186, 229, 244 },
        .sky_zenith = { 66, 158, 226 },
        .sky_gradient = .85f,
        .sun_halo = { 255, 237, 165 },
        .sun_disc = { 255, 244, 184 },
        .halo_strength = .24f,
        .halo_radius = .16f,
        .disc_strength = 1,
        .cloud_underside = { 225, 242, 244 },
        .cloud_rim = { 205, 225, 238 },
        .fog = { 186.0f / 255.0f, 229.0f / 255.0f, 244.0f / 255.0f },
        .fog_start = 145.0f,
        .fog_end = 620.0f,
        .fog_opacity = .38f,
        .water = { 1, 1, 1 },
        .water_shadow = .55f,
        .glint = { 1.0f, .96f, .84f },
        .highlight = { 1, 1, 1 },
        .window_dark = { 1, 1, 1 },
    };
}

// Warm, low sun: long shadows, a glowing horizon under it and a glitter path toward it.
static WaterfrontLighting sunset(void)
{
    return (WaterfrontLighting) {
        .phase = WATERFRONT_DAYLIGHT_SUNSET,
        .sun_direction = sun_at(7 * DEG2RAD),
        .ambient = { .40f, .38f, .50f },
        .direct = { 1.0f, .62f, .34f },
        .direct_intensity = .95f,
        .cpu_ambient = .24f,
        .cpu_direct = .72f,
        .cpu_tint = { 1.0f, .78f, .62f },
        .sky_horizon = { 252, 176, 116 },
        .sky_zenith = { 60, 86, 156 },
        .sky_gradient = .70f,
        .sun_halo = { 255, 190, 110 },
        .sun_disc = { 255, 234, 172 },
        .horizon_glow = { 255, 132, 72 },
        .halo_strength = .55f,
        .halo_radius = .32f,
        .disc_strength = 1,
        .glow_strength = .70f,
        .cloud_underside = { 240, 152, 128 },
        .cloud_rim = { 192, 114, 120 },
        .fog = { 228.0f / 255.0f, 164.0f / 255.0f, 128.0f / 255.0f },
        .fog_start = 130.0f,
        .fog_end = 600.0f,
        .fog_opacity = .42f,
        .water = { .86f, .72f, .70f },
        .water_shadow = .70f,
        .glint = { 1.0f, .70f, .38f },
        .highlight = { 1.0f, .62f, .34f },
        .window_light = .25f,
        .window_lit = { 1.0f, .80f, .48f },
        .window_dark = { .92f, .82f, .80f },
    };
}

// Blue dusk after the sun has set: no direct light, a fading afterglow on the horizon, deep
// blue scenery and warm occupied windows.
static WaterfrontLighting dusk(void)
{
    return (WaterfrontLighting) {
        .phase = WATERFRONT_DAYLIGHT_DUSK,
        .sun_direction = sun_at(-5 * DEG2RAD),
        .ambient = { .20f, .30f, .48f },
        .direct = { .90f, .50f, .40f },
        .direct_intensity = 0,
        .cpu_ambient = .55f,
        .cpu_direct = .25f,
        .cpu_tint = { .36f, .52f, .80f },
        .sky_horizon = { 104, 118, 168 },
        .sky_zenith = { 22, 36, 84 },
        .sky_gradient = .80f,
        .sun_halo = { 220, 130, 120 },
        .sun_disc = { 255, 200, 150 },
        .horizon_glow = { 236, 130, 96 },
        .halo_strength = .30f,
        .halo_radius = .45f,
        .glow_strength = .50f,
        .cloud_underside = { 70, 78, 120 },
        .cloud_rim = { 58, 64, 104 },
        .fog = { 96.0f / 255.0f, 108.0f / 255.0f, 152.0f / 255.0f },
        .fog_start = 100.0f,
        .fog_end = 520.0f,
        .fog_opacity = .50f,
        .water = { .40f, .58f, .86f },
        .water_shadow = 1,
        .window_light = 1,
        .window_lit = { 1.0f, .80f, .45f },
        .window_dark = { .30f, .40f, .60f },
    };
}

static Vector3 mix3(Vector3 a, Vector3 b, float t) { return Vector3Lerp(a, b, t); }

static WaterfrontLighting mix(WaterfrontLighting a, WaterfrontLighting b, float t, float phase)
{
    float elevation = Lerp(asinf(a.sun_direction.y), asinf(b.sun_direction.y), t);
    return (WaterfrontLighting) {
        .phase = phase,
        .sun_direction = sun_at(elevation),
        .ambient = mix3(a.ambient, b.ambient, t),
        .direct = mix3(a.direct, b.direct, t),
        .direct_intensity = Lerp(a.direct_intensity, b.direct_intensity, t),
        .cpu_ambient = Lerp(a.cpu_ambient, b.cpu_ambient, t),
        .cpu_direct = Lerp(a.cpu_direct, b.cpu_direct, t),
        .cpu_tint = mix3(a.cpu_tint, b.cpu_tint, t),
        .sky_horizon = mix3(a.sky_horizon, b.sky_horizon, t),
        .sky_zenith = mix3(a.sky_zenith, b.sky_zenith, t),
        .sky_gradient = Lerp(a.sky_gradient, b.sky_gradient, t),
        .sun_halo = mix3(a.sun_halo, b.sun_halo, t),
        .sun_disc = mix3(a.sun_disc, b.sun_disc, t),
        .horizon_glow = mix3(a.horizon_glow, b.horizon_glow, t),
        .halo_strength = Lerp(a.halo_strength, b.halo_strength, t),
        .halo_radius = Lerp(a.halo_radius, b.halo_radius, t),
        .disc_strength = Lerp(a.disc_strength, b.disc_strength, t),
        .glow_strength = Lerp(a.glow_strength, b.glow_strength, t),
        .cloud_underside = mix3(a.cloud_underside, b.cloud_underside, t),
        .cloud_rim = mix3(a.cloud_rim, b.cloud_rim, t),
        .fog = mix3(a.fog, b.fog, t),
        .fog_start = Lerp(a.fog_start, b.fog_start, t),
        .fog_end = Lerp(a.fog_end, b.fog_end, t),
        .fog_opacity = Lerp(a.fog_opacity, b.fog_opacity, t),
        .water = mix3(a.water, b.water, t),
        .water_shadow = Lerp(a.water_shadow, b.water_shadow, t),
        .glint = mix3(a.glint, b.glint, t),
        .highlight = mix3(a.highlight, b.highlight, t),
        .window_light = Lerp(a.window_light, b.window_light, t),
        .window_lit = mix3(a.window_lit, b.window_lit, t),
        .window_dark = mix3(a.window_dark, b.window_dark, t),
    };
}

WaterfrontLighting waterfront_lighting(float phase)
{
    if (!(phase > WATERFRONT_DAYLIGHT_DAY))
        return day();
    if (phase >= WATERFRONT_DAYLIGHT_DUSK)
        return dusk();
    if (phase == WATERFRONT_DAYLIGHT_SUNSET)
        return sunset();
    if (phase < WATERFRONT_DAYLIGHT_SUNSET)
        return mix(day(), sunset(), phase / WATERFRONT_DAYLIGHT_SUNSET, phase);
    return mix(sunset(), dusk(),
        (phase - WATERFRONT_DAYLIGHT_SUNSET)
            / (WATERFRONT_DAYLIGHT_DUSK - WATERFRONT_DAYLIGHT_SUNSET),
        phase);
}

// Exact: raymath's Vector3Equals tolerates differences that would change the panorama.
static bool same3(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

bool waterfront_lighting_same_sky(const WaterfrontLighting *a, const WaterfrontLighting *b)
{
    return same3(a->sun_direction, b->sun_direction) && same3(a->sky_horizon, b->sky_horizon)
        && same3(a->sky_zenith, b->sky_zenith) && a->sky_gradient == b->sky_gradient
        && same3(a->sun_halo, b->sun_halo) && same3(a->sun_disc, b->sun_disc)
        && same3(a->horizon_glow, b->horizon_glow) && a->halo_strength == b->halo_strength
        && a->halo_radius == b->halo_radius && a->disc_strength == b->disc_strength
        && a->glow_strength == b->glow_strength && same3(a->cloud_underside, b->cloud_underside)
        && same3(a->cloud_rim, b->cloud_rim);
}

bool waterfront_daylight_parse(const char *text, float *phase)
{
    static const struct {
        const char *name;
        float phase;
    } names[] = { { "day", WATERFRONT_DAYLIGHT_DAY }, { "sunset", WATERFRONT_DAYLIGHT_SUNSET },
        { "dusk", WATERFRONT_DAYLIGHT_DUSK } };
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (strcmp(text, names[i].name) == 0) {
            *phase = names[i].phase;
            return true;
        }
    char *end;
    float value = strtof(text, &end);
    if (end == text || *end || !(value >= 0 && value <= 1))
        return false;
    *phase = value;
    return true;
}
