#include "waterfront_grade.h"
#include "waterfront_lighting.h"

#include <math.h>
#include <stdio.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Grade check failed at line %d: %s\n", __LINE__, #condition);          \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

enum { FIELDS = sizeof(WaterfrontGrade) / sizeof(float) };
_Static_assert(sizeof(WaterfrontGrade) % sizeof(float) == 0, "a grade holds only floats");

static const float *fields(const WaterfrontGrade *g) { return (const float *)g; }

int main(void)
{
    const float phases[]
        = { WATERFRONT_DAYLIGHT_DAY, WATERFRONT_DAYLIGHT_SUNSET, WATERFRONT_DAYLIGHT_DUSK };
    for (int i = 0; i < 3; ++i) {
        WaterfrontGrade g = waterfront_grade(phases[i]);
        CHECK(g.phase == phases[i]);
        // The approved presets keep their 8-bit colours: neutral grades, and bloom only from
        // light above 1, which no preset authors yet.
        CHECK(waterfront_grade_neutral(&g));
        CHECK(g.bloom_threshold >= 1 && g.bloom_knee > 0);
        CHECK(g.bloom_strength >= 0 && g.bloom_strength <= .25f);
    }
    // Clamping, as the lighting frame does.
    CHECK(waterfront_grade(-1).phase == WATERFRONT_DAYLIGHT_DAY);
    CHECK(waterfront_grade(NAN).phase == WATERFRONT_DAYLIGHT_DAY);
    CHECK(waterfront_grade(2).phase == WATERFRONT_DAYLIGHT_DUSK);
    // Every field moves continuously across the sweep and stays finite.
    WaterfrontGrade previous = waterfront_grade(0);
    for (int step = 1; step <= 2048; ++step) {
        float phase = step / 2048.0f;
        WaterfrontGrade g = waterfront_grade(phase);
        CHECK(fabsf(g.phase - phase) < 1e-6f);
        for (int f = 1; f < FIELDS; ++f) {
            CHECK(isfinite(fields(&g)[f]));
            CHECK(fabsf(fields(&g)[f] - fields(&previous)[f]) <= 1.0f / 512);
        }
        previous = g;
    }
    // Neutral means each colour operation is the identity.
    WaterfrontGrade g = waterfront_grade(0);
    g.contrast = 1.1f;
    CHECK(!waterfront_grade_neutral(&g));
    g = waterfront_grade(0);
    g.lift.z = .01f;
    CHECK(!waterfront_grade_neutral(&g));
    g = waterfront_grade(0);
    g.bloom_strength = 0;
    CHECK(waterfront_grade_neutral(&g));
    puts("Waterfront grade checks passed.");
    return 0;
}
