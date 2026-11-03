#ifndef PROTEUS_WATERFRONT_LIGHTING_H
#define PROTEUS_WATERFRONT_LIGHTING_H

#include "raylib.h"

// Fixed daylight phases. Zero is the original daytime look and the zero-initialized default.
#define WATERFRONT_DAYLIGHT_DAY 0.0f
#define WATERFRONT_DAYLIGHT_SUNSET 0.5f
#define WATERFRONT_DAYLIGHT_DUSK 1.0f

// The presentation light of one frame: sky, sun, fill, haze, water and window light that agree
// about the time of day. Evaluated from an explicit normalized daylight phase, never the wall
// clock, and owned by the caller, which hands the same value to the sky panorama and to the CPU
// and GPU renderers with their water and reflections. Colours are 0..1 unless noted.
typedef struct {
    float phase; // Clamped 0 (day) .. 1 (dusk) phase this was evaluated from.
    Vector3 sun_direction; // Unit, toward the sun; also the shadow and glitter direction.
    // GPU shading: ambient fill plus the sun's colour times its intensity.
    Vector3 ambient, direct;
    float direct_intensity;
    // CPU flat shading: brightness is ambient + direct * N.L, times the tint.
    float cpu_ambient, cpu_direct;
    Vector3 cpu_tint;
    // Sky panorama, in 0..255 channel values: the vertical gradient reaches the zenith at
    // sky_gradient elevation; a halo and disc around the sun; a warm glow along the horizon
    // under the sun (none by day); the cloud deck's underside and its thin rim.
    Vector3 sky_horizon, sky_zenith;
    float sky_gradient;
    Vector3 sun_halo, sun_disc, horizon_glow;
    float halo_strength, halo_radius, disc_strength, glow_strength;
    Vector3 cloud_underside, cloud_rim;
    // Distance haze over scenery.
    Vector3 fog;
    float fog_start, fog_end, fog_opacity;
    // Water: multiplies the body colour and foam; the brightness left in sun shadow (1 casts
    // none); sun glitter colour (zero hides it); and a multiplier on the specular highlight.
    Vector3 water;
    float water_shadow;
    Vector3 glint, highlight;
    // Glitter character: a road of reflected sun (0 by day), the share of single-pixel sparkles
    // kept (below 1 they also fade out with distance) and their wave speed.
    float glint_sheen, glint_sparkle, glint_speed;
    // Windows: 0 shows daytime glass; 1 lights occupied windows in window_lit and multiplies
    // the others by window_dark.
    float window_light;
    Vector3 window_lit, window_dark;
    // Multiplies self-coloured surfaces that are not lights, such as gulls and traffic wakes.
    Vector3 unlit;
} WaterfrontLighting;

// The light at PHASE (clamped to 0..1), interpolating day, sunset and dusk. Each fixed phase
// returns its preset exactly; phase zero reproduces the original daytime constants.
WaterfrontLighting waterfront_lighting(float phase);
// True when A and B paint the same sky panorama.
bool waterfront_lighting_same_sky(const WaterfrontLighting *a, const WaterfrontLighting *b);
// A daylight phase named day, sunset or dusk, or a number in 0..1. False otherwise.
bool waterfront_daylight_parse(const char *text, float *phase);

#endif
