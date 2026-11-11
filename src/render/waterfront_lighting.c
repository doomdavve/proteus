#include "render/waterfront_lighting.h"

#include "raylib.h"
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

// Day keeps the established scene light, with the deeper-blue sky selected for play review.
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
        .sky_horizon = { 160, 213, 242 },
        .sky_zenith = { 22, 91, 190 },
        .sky_gradient = .65f,
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
        .glint_sparkle = .55f,
        .glint_speed = .55f,
        .window_dark = { 1, 1, 1 },
        .unlit = { 1, 1, 1 },
    };
}

// A low sun's glitter is calm: a soft road of light with a few slow sparkles. Dusk keeps the
// same character, so blends from sunset change only its colour.
#define SUNSET_SHEEN .55f
#define SUNSET_SPARKLE .30f
#define SUNSET_SPEED .40f

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
        .sky_gradient = .45f,
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
        .water = { .72f, .68f, .86f },
        .water_shadow = .70f,
        .glint = { 1.0f, .70f, .38f },
        .highlight = { 1.0f, .62f, .34f },
        .glint_sheen = SUNSET_SHEEN,
        .glint_sparkle = SUNSET_SPARKLE,
        .glint_speed = SUNSET_SPEED,
        .window_light = .25f,
        .window_lit = { 1.0f, .80f, .48f },
        .window_dark = { .92f, .82f, .80f },
        .unlit = { .92f, .76f, .68f },
    };
}

// Blue dusk after the sun has set: no direct light, a fading afterglow on the horizon, deep
// blue scenery and warm occupied windows.
static WaterfrontLighting dusk(void)
{
    return (WaterfrontLighting) {
        .phase = WATERFRONT_DAYLIGHT_DUSK,
        .sun_direction = sun_at(-5 * DEG2RAD),
        .ambient = { .16f, .25f, .42f },
        .direct = { .90f, .50f, .40f },
        .direct_intensity = 0,
        .cpu_ambient = .55f,
        .cpu_direct = .25f,
        .cpu_tint = { .30f, .46f, .74f },
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
        .water = { .46f, .70f, .94f },
        .water_shadow = 1,
        .glint_sheen = SUNSET_SHEEN,
        .glint_sparkle = SUNSET_SPARKLE,
        .glint_speed = SUNSET_SPEED,
        .window_light = 1,
        .window_lit = { 1.0f, .80f, .45f },
        .window_dark = { .30f, .40f, .60f },
        .unlit = { .40f, .50f, .72f },
    };
}

// The full-day recipes reuse the approved palettes at day, sunset and dusk, with new sun
// bearings supplied by the hour path. Night and dawn are their own recipes so night is not dusk
// with the light removed.
static WaterfrontLighting night(void)
{
    return (WaterfrontLighting) {
        .phase = WATERFRONT_DAYLIGHT_DAY,
        .sun_direction = { 0, 0, 1 },
        .ambient = { .10f, .15f, .26f },
        .direct = { .55f, .68f, .95f },
        .direct_intensity = 0,
        .cpu_ambient = .36f,
        .cpu_direct = 0,
        .cpu_tint = { .34f, .44f, .62f },
        .sky_horizon = { 26, 36, 62 },
        .sky_zenith = { 4, 9, 24 },
        .sky_gradient = .90f,
        .sun_halo = { 90, 110, 150 },
        .sun_disc = { 120, 140, 180 },
        .halo_strength = 0,
        .halo_radius = .35f,
        .disc_strength = 0,
        .cloud_underside = { 20, 28, 46 },
        .cloud_rim = { 28, 36, 56 },
        .fog = { 18.0f / 255.0f, 26.0f / 255.0f, 44.0f / 255.0f },
        .fog_start = 90,
        .fog_end = 460,
        .fog_opacity = .50f,
        .water = { .36f, .50f, .72f },
        .water_shadow = 1,
        .glint = { 0 },
        .highlight = { 0 },
        .glint_speed = 0,
        .window_light = 1,
        .window_lit = { 1.0f, .82f, .52f },
        .window_dark = { .26f, .34f, .50f },
        .unlit = { .34f, .42f, .58f },
    };
}

static WaterfrontLighting dawn(void)
{
    return (WaterfrontLighting) {
        .phase = WATERFRONT_DAYLIGHT_DAY,
        .sun_direction = { 0, 0, 1 },
        .ambient = { .22f, .25f, .40f },
        .direct = { .90f, .60f, .50f },
        .direct_intensity = 0,
        .cpu_ambient = .42f,
        .cpu_direct = 0,
        .cpu_tint = { .42f, .50f, .70f },
        .sky_horizon = { 92, 88, 132 },
        .sky_zenith = { 18, 28, 64 },
        .sky_gradient = .70f,
        .sun_halo = { 220, 130, 120 },
        .sun_disc = { 255, 190, 150 },
        .horizon_glow = { 200, 110, 90 },
        .halo_strength = .25f,
        .halo_radius = .50f,
        .glow_strength = .35f,
        .cloud_underside = { 55, 58, 90 },
        .cloud_rim = { 48, 52, 84 },
        .fog = { 70.0f / 255.0f, 78.0f / 255.0f, 112.0f / 255.0f },
        .fog_start = 110,
        .fog_end = 500,
        .fog_opacity = .45f,
        .water = { .45f, .58f, .82f },
        .water_shadow = 1,
        .glint = { 1.0f, .60f, .40f },
        .highlight = { 1.0f, .60f, .45f },
        .glint_sheen = SUNSET_SHEEN,
        .glint_sparkle = SUNSET_SPARKLE,
        .glint_speed = SUNSET_SPEED,
        .window_light = .75f,
        .window_lit = { 1.0f, .80f, .48f },
        .window_dark = { .45f, .50f, .66f },
        .unlit = { .55f, .60f, .76f },
    };
}

static WaterfrontLighting sunrise(void)
{
    return (WaterfrontLighting) {
        .phase = WATERFRONT_DAYLIGHT_DAY,
        .sun_direction = { 0, 0, 1 },
        .ambient = { .35f, .34f, .44f },
        .direct = { 1.0f, .55f, .32f },
        .direct_intensity = .90f,
        .cpu_ambient = .34f,
        .cpu_direct = .60f,
        .cpu_tint = { 1.0f, .80f, .68f },
        .sky_horizon = { 250, 170, 130 },
        .sky_zenith = { 55, 90, 160 },
        .sky_gradient = .48f,
        .sun_halo = { 255, 180, 110 },
        .sun_disc = { 255, 234, 172 },
        .horizon_glow = { 255, 120, 80 },
        .halo_strength = .50f,
        .halo_radius = .35f,
        .disc_strength = 1,
        .glow_strength = .60f,
        .cloud_underside = { 235, 150, 135 },
        .cloud_rim = { 180, 110, 120 },
        .fog = { 210.0f / 255.0f, 150.0f / 255.0f, 130.0f / 255.0f },
        .fog_start = 125,
        .fog_end = 600,
        .fog_opacity = .40f,
        .water = { .70f, .68f, .88f },
        .water_shadow = .70f,
        .glint = { 1.0f, .65f, .35f },
        .highlight = { 1.0f, .60f, .32f },
        .glint_sheen = SUNSET_SHEEN,
        .glint_sparkle = SUNSET_SPARKLE,
        .glint_speed = SUNSET_SPEED,
        .window_light = .18f,
        .window_lit = { 1.0f, .80f, .48f },
        .window_dark = { .90f, .82f, .82f },
        .unlit = { .90f, .76f, .70f },
    };
}

static WaterfrontLighting morning(void)
{
    WaterfrontLighting l = day();
    l.ambient = (Vector3) { .46f, .55f, .63f };
    l.direct = (Vector3) { 1.0f, .91f, .75f };
    l.sky_horizon = (Vector3) { 200, 230, 242 };
    l.sky_zenith = (Vector3) { 64, 148, 216 };
    l.sky_gradient = .85f;
    l.fog = (Vector3) { 200.0f / 255.0f, 230.0f / 255.0f, 242.0f / 255.0f };
    return l;
}

static WaterfrontLighting afternoon(void)
{
    WaterfrontLighting l = day();
    l.ambient = (Vector3) { .50f, .53f, .58f };
    l.direct = (Vector3) { 1.0f, .86f, .62f };
    l.cpu_tint = (Vector3) { 1.0f, .97f, .92f };
    l.sky_horizon = (Vector3) { 208, 224, 228 };
    l.sky_zenith = (Vector3) { 70, 150, 210 };
    l.sky_gradient = .85f;
    l.fog = (Vector3) { 200.0f / 255.0f, 218.0f / 255.0f, 226.0f / 255.0f };
    l.water = (Vector3) { 1.0f, .98f, .94f };
    l.unlit = (Vector3) { .98f, .92f, .86f };
    return l;
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
        .glint_sheen = Lerp(a.glint_sheen, b.glint_sheen, t),
        .glint_sparkle = Lerp(a.glint_sparkle, b.glint_sparkle, t),
        .glint_speed = Lerp(a.glint_speed, b.glint_speed, t),
        .window_light = Lerp(a.window_light, b.window_light, t),
        .window_lit = mix3(a.window_lit, b.window_lit, t),
        .window_dark = mix3(a.window_dark, b.window_dark, t),
        .unlit = mix3(a.unlit, b.unlit, t),
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

// Artistic hours, not solar claims. Azimuth advances through the day so sunrise and sunset are
// opposite; each segment interpolates angle, never a direction vector through zero.
typedef struct {
    float hour, elevation, azimuth; // Degrees.
    WaterfrontLighting (*recipe)(void);
} HourAnchor;

static const HourAnchor hour_anchors[] = {
    { 0, -35, 0, night },
    { 4.5f, -12, 30, dawn },
    { 6, 0, 90, sunrise },
    { 9, 38, 135, morning },
    { 12, 58, 180, day },
    { 15, 38, 225, afternoon },
    { 18, 4, 270, sunset },
    { 19.5f, -6, 290, dusk },
    { 21.5f, -25, 320, night },
    { 24, -35, 360, night },
};

static Vector3 sun_path(float elevation, float azimuth)
{
    float e = elevation * DEG2RAD, a = azimuth * DEG2RAD, horizontal = cosf(e);
    return (Vector3) { horizontal * sinf(a), sinf(e), horizontal * cosf(a) };
}

static float smooth(float t) { return t * t * (3 - 2 * t); }

// Direct light, disc, glitter, highlight and cast water shadow share one horizon gate. At and
// below the horizon the first five are exactly zero and the water keeps its fully unshadowed
// brightness.
static void apply_horizon(WaterfrontLighting *lighting, float elevation_degrees)
{
    float visible = elevation_degrees <= 0
        ? 0
        : smooth(Clamp(elevation_degrees / 5.0f, 0, 1)); // Five-degree fade, in degrees.
    lighting->direct_intensity *= visible;
    lighting->cpu_direct *= visible;
    lighting->disc_strength *= visible;
    lighting->glint = Vector3Scale(lighting->glint, visible);
    lighting->highlight = Vector3Scale(lighting->highlight, visible);
    lighting->glint_sheen *= visible;
    lighting->glint_sparkle *= visible;
    lighting->water_shadow = Lerp(1, lighting->water_shadow, visible);
}

bool waterfront_hour_normalize(float hour, float *wrapped)
{
    if (!isfinite(hour))
        return false;
    double value = fmod((double)hour, 24.0);
    if (value < 0)
        value += 24.0;
    float result = (float)value;
    if (result >= 24.0f || result == 0)
        result = 0; // Also canonicalizes -0.
    *wrapped = result;
    return true;
}

bool waterfront_hour_parse(const char *text, float *hour)
{
    char *end;
    float value = strtof(text, &end);
    if (end == text || *end)
        return false;
    return waterfront_hour_normalize(value, hour);
}

bool waterfront_lighting_hour(float hour, WaterfrontLighting *lighting)
{
    float h;
    if (!waterfront_hour_normalize(hour, &h))
        return false;
    size_t i = 0;
    while (i + 2 < sizeof(hour_anchors) / sizeof(*hour_anchors) && h >= hour_anchors[i + 1].hour)
        ++i;
    const HourAnchor *a = &hour_anchors[i], *b = &hour_anchors[i + 1];
    float elevation = a->elevation, azimuth = a->azimuth;
    WaterfrontLighting result;
    if (h == a->hour) {
        result = a->recipe();
    } else {
        float t = smooth((h - a->hour) / (b->hour - a->hour));
        result = mix(a->recipe(), b->recipe(), t, WATERFRONT_DAYLIGHT_DAY);
        elevation = Lerp(a->elevation, b->elevation, t);
        azimuth = Lerp(a->azimuth, b->azimuth, t);
    }
    result.phase = WATERFRONT_DAYLIGHT_DAY;
    result.sun_direction = sun_path(elevation, azimuth);
    apply_horizon(&result, elevation);
    *lighting = result;
    return true;
}

// The authored glint_speed is linearly interpolated between consecutive anchors through the same
// smoothstep as the light. Its integral over an interval of D hours, T of the way in, is analytic:
// D * (a*T + (b - a)*(T^3 - T^4/2)). This reads each anchor's recipe, so the clock has no second
// copy of the artistic speed table and performs no per-frame sweep.
static double glint_interval_area(size_t i, double t)
{
    const HourAnchor *a = &hour_anchors[i], *b = &hour_anchors[i + 1];
    double d = (double)b->hour - (double)a->hour;
    double from = a->recipe().glint_speed, to = b->recipe().glint_speed;
    return d * (from * t + (to - from) * (t * t * t - .5 * t * t * t * t));
}

static double glint_day_area(void)
{
    double area = 0;
    size_t count = sizeof(hour_anchors) / sizeof(*hour_anchors);
    for (size_t i = 0; i + 1 < count; ++i)
        area += glint_interval_area(i, 1.0);
    return area;
}

// Integral of glint_speed from hour zero of the anchor table, with whole days handled analytically
// before walking the existing anchors once for the remainder.
static double glint_primitive(double hour)
{
    double days = floor(hour / 24.0);
    double remaining = hour - days * 24.0;
    double area = days * glint_day_area();
    size_t count = sizeof(hour_anchors) / sizeof(*hour_anchors);
    for (size_t i = 0; i + 1 < count; ++i) {
        double start = (double)hour_anchors[i].hour, end = (double)hour_anchors[i + 1].hour;
        if (remaining <= start)
            break;
        double t = remaining < end ? (remaining - start) / (end - start) : 1.0;
        area += glint_interval_area(i, t);
        if (remaining <= end)
            break;
    }
    return area;
}

bool waterfront_glitter_clock(double unwrapped_hour, double cycle_seconds, double *seconds)
{
    if (!isfinite(unwrapped_hour) || !isfinite(cycle_seconds) || !(cycle_seconds > 0))
        return false;
    double value = (glint_primitive(unwrapped_hour) - glint_primitive(12.0)) * cycle_seconds / 24.0;
    if (!isfinite(value))
        return false;
    *seconds = value;
    return true;
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
