#include "render/waterfront_grade.h"
#include "render/waterfront_lighting.h"

#include "raylib.h"
#include "raymath.h"

// The approved sunset and dusk presets are shaded in full by WaterfrontLighting; their grades
// stay neutral until a reviewed change. Bloom is the only difference from the 8-bit look.
static WaterfrontGrade neutral(float phase)
{
    return (WaterfrontGrade) { .phase = phase,
        .exposure = 1,
        .contrast = 1,
        .saturation = 1,
        .gain = { 1, 1, 1 },
        .bloom_threshold = 1,
        .bloom_knee = .25f,
        .bloom_strength = .2f };
}

static WaterfrontGrade day(void) { return neutral(WATERFRONT_DAYLIGHT_DAY); }

static WaterfrontGrade sunset(void) { return neutral(WATERFRONT_DAYLIGHT_SUNSET); }

static WaterfrontGrade dusk(void) { return neutral(WATERFRONT_DAYLIGHT_DUSK); }

static WaterfrontGrade mix(WaterfrontGrade a, WaterfrontGrade b, float t, float phase)
{
    return (WaterfrontGrade) { .phase = phase,
        .exposure = Lerp(a.exposure, b.exposure, t),
        .contrast = Lerp(a.contrast, b.contrast, t),
        .saturation = Lerp(a.saturation, b.saturation, t),
        .lift = Vector3Lerp(a.lift, b.lift, t),
        .gain = Vector3Lerp(a.gain, b.gain, t),
        .bloom_threshold = Lerp(a.bloom_threshold, b.bloom_threshold, t),
        .bloom_knee = Lerp(a.bloom_knee, b.bloom_knee, t),
        .bloom_strength = Lerp(a.bloom_strength, b.bloom_strength, t) };
}

WaterfrontGrade waterfront_grade(float phase)
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

bool waterfront_grade_hour(float hour, WaterfrontGrade *grade)
{
    float wrapped;
    if (!waterfront_hour_normalize(hour, &wrapped))
        return false;
    (void)wrapped;
    *grade = neutral(WATERFRONT_DAYLIGHT_DAY);
    return true;
}

bool waterfront_grade_neutral(const WaterfrontGrade *grade)
{
    return grade->exposure == 1 && grade->contrast == 1 && grade->saturation == 1
        && grade->lift.x == 0 && grade->lift.y == 0 && grade->lift.z == 0 && grade->gain.x == 1
        && grade->gain.y == 1 && grade->gain.z == 1;
}
