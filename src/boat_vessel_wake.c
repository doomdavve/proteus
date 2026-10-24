#include "boat_vessel_wake.h"
#include "raymath.h"
#include <math.h>

bool boat_vessel_wake_valid(BoatVesselWake w)
{
    return isfinite(w.origin.x) && isfinite(w.origin.y) && isfinite(w.direction.x)
        && isfinite(w.direction.y) && fabsf(Vector2LengthSqr(w.direction) - 1) < .0001f
        && isfinite(w.started) && isfinite(w.speed) && w.speed > 0 && w.speed <= 30
        && isfinite(w.height) && w.height > 0 && w.height <= 8 && isfinite(w.duration)
        && w.duration >= 1 && w.duration <= 60 && isfinite(w.lifetime) && w.lifetime >= 1
        && w.lifetime <= 60 && isfinite(w.wavelength) && w.wavelength >= 2 && w.wavelength <= 40
        && isfinite(w.spread) && w.spread >= 1 && w.spread <= 10 && isfinite(w.half_width)
        && w.half_width >= 2 && w.half_width <= 80;
}

static float clock_at(BoatVesselWake w, double time)
{
    double age = time - w.started;
    return w.height > 0 && age >= 0 && age < w.duration + w.lifetime ? (float)age : -1;
}

BoatVesselWakeSample boat_vessel_wake_query(BoatVesselWake w, Vector2 p, double time)
{
    BoatVesselWakeSample sample = { 0 };
    float clock = clock_at(w, time);
    if (clock < 0)
        return sample;
    p = Vector2Subtract(p, w.origin);
    Vector2 side = { -w.direction.y, w.direction.x };
    float emitted = Vector2DotProduct(p, w.direction) / w.speed;
    float age = clock - emitted;
    if (emitted <= 0 || emitted >= w.duration || age <= 0 || age >= w.lifetime)
        return sample;
    float transverse = Vector2DotProduct(p, side);
    float radius = sqrtf(transverse * transverse + 4);
    float q = radius - w.spread * age;
    if (fabsf(q) >= w.half_width)
        return sample;
    float k = 2 * PI / w.wavelength;
    float edge = 1 - q * q / (w.half_width * w.half_width);
    // Centre the compact packet on its largest crest; the cubic envelope tapers
    // the two outer crests for the authored 14 m half-width / 8 m wavelength.
    float f = cosf(k * q) * edge * edge * edge;
    float df = -k * sinf(k * q) * edge * edge * edge
        - cosf(k * q) * 6 * q / (w.half_width * w.half_width) * edge * edge;
    float window = sinf(PI * emitted / w.duration);
    float envelope = sinf(PI * age / w.lifetime);
    float dw = PI / w.duration * sinf(2 * PI * emitted / w.duration);
    float de = PI / w.lifetime * sinf(2 * PI * age / w.lifetime);
    window *= window;
    envelope *= envelope;
    sample.height = w.height * window * envelope * f;
    float along = w.height / w.speed
        * ((dw * envelope - window * de) * f + window * envelope * df * w.spread);
    float across = w.height * window * envelope * df * transverse / radius;
    sample.gradient = Vector2Add(Vector2Scale(w.direction, along), Vector2Scale(side, across));
    sample.vertical_velocity = w.height * window * (de * f - envelope * df * w.spread);
    return sample;
}

bool boat_vessel_wake_position(BoatVesselWake w, double time, Vector2 *position)
{
    float clock = clock_at(w, time);
    if (clock < 0)
        return false;
    *position = Vector2Add(w.origin, Vector2Scale(w.direction, w.speed * fminf(clock, w.duration)));
    return true;
}

void boat_vessel_wake_uniforms(BoatVesselWake w, double time, float values[12])
{
    const float packed[12] = { -w.origin.x, w.origin.y, -w.direction.x, w.direction.y, w.speed,
        w.height, w.duration, w.lifetime, clock_at(w, time), w.wavelength, w.spread, w.half_width };
    for (int i = 0; i < 12; ++i)
        values[i] = packed[i];
}
