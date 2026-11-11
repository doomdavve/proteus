#ifndef PROTEUS_WATERFRONT_GRADE_H
#define PROTEUS_WATERFRONT_GRADE_H

#include "raylib.h"

// The presentation grade of the HDR scene: restrained bloom from light above the threshold,
// then a compact colour grade before the clamp to the 8-bit target. Each named daylight phase
// has one, blended like WaterfrontLighting from the same phase, so there is no second clock.
// Colours are display values, as the renderers shade; there is no tone curve.
typedef struct {
    float phase; // Clamped 0 (day) .. 1 (dusk) phase this was evaluated from.
    float exposure; // Multiplies the scene and its bloom; 1 is neutral.
    float contrast; // About mid grey; 1 is neutral.
    float saturation; // 1 is neutral, 0 grey.
    Vector3 lift; // Shadows move toward this colour by its amount; zero is neutral.
    Vector3 gain; // Multiplies the result; one is neutral.
    // Bloom takes the part of each pixel above the threshold, eased in over the knee, blurs it
    // and adds this share. With the threshold at 1 only unclamped light reaches it.
    float bloom_threshold, bloom_knee, bloom_strength;
} WaterfrontGrade;

// The grade at PHASE (clamped to 0..1), interpolating day, sunset and dusk; each fixed phase
// returns its preset exactly.
WaterfrontGrade waterfront_grade(float phase);
// A neutral grade for a full-day HOUR. New hours do not reinterpret the legacy evening phase:
// the result keeps its phase at WATERFRONT_DAYLIGHT_DAY. Invalid HOUR returns false and leaves
// *grade untouched.
bool waterfront_grade_hour(float hour, WaterfrontGrade *grade);
// True when the colour grade leaves every clamped colour unchanged (bloom aside).
bool waterfront_grade_neutral(const WaterfrontGrade *grade);

#endif
