#include "boat_water.h"
#include "boat_clock.h"
#include "boat_model.h"
#include "raymath.h"

#include <math.h>
#include <stdlib.h>

// Internal queries borrow the settings: BoatWater carries its whole wake history by value.
static BoatWaterSample query(const BoatWater *water, Vector2 position, double time);

BoatWaterSample boat_water_query_borrowed(const BoatWater *water, Vector2 position, double time)
{
    return query(water, position, time);
}

static float pose_height(const BoatWater *water, Vector3 p, double time)
{
    return query(water, (Vector2) { -p.x, p.z }, time).height;
}

Transform3D boat_water_pose(
    BoatWater water, double time, Vector3 p, float yaw, float length, float beam)
{
    Matrix heading = MatrixRotateY(yaw);
    Vector3 fore = Vector3Add(p, Vector3Transform((Vector3) { 0, 0, -length * .35f }, heading));
    Vector3 aft = Vector3Add(p, Vector3Transform((Vector3) { 0, 0, length * .35f }, heading));
    Vector3 left = Vector3Add(p, Vector3Transform((Vector3) { -beam * .35f, 0, 0 }, heading));
    Vector3 right = Vector3Add(p, Vector3Transform((Vector3) { beam * .35f, 0, 0 }, heading));
    float f = pose_height(&water, fore, time), a = pose_height(&water, aft, time);
    float l = pose_height(&water, left, time), r = pose_height(&water, right, time);
    p.y = (f + a + l + r) * .25f;
    float pitch = atanf((f - a) / (length * .7f));
    float roll = atanf((r - l) / (beam * .7f));
    Matrix rotation
        = MatrixMultiply(MatrixMultiply(MatrixRotateZ(roll), MatrixRotateX(pitch)), heading);
    return (Transform3D) {
        .position = p, .rotation = boat_model_rotation(rotation), .scale = { 1, 1, 1 }
    };
}

// Three low crossing components. Frequencies approximate deep-water dispersion at this scale.
BoatWater boat_water_gentle(void)
{
    return (BoatWater) { .amplitude = .13f,
        .wavelength = 16,
        .angular_speed = 1.96f,
        .direction = { .6f, -.8f },
        .crossing = { { .amplitude = .06f,
                          .wavelength = 9,
                          .angular_speed = 2.62f,
                          .direction = { -.8f, -.6f } },
            { .amplitude = .025f,
                .wavelength = 5.5f,
                .angular_speed = 3.35f,
                .direction = { .96f, .28f } } } };
}

BoatWater boat_water_test(bool waves, bool current, bool wake)
{
    BoatWater water = waves ? boat_water_gentle() : (BoatWater) { 0 };
    if (current) {
        water.current = (Vector2) { .85f, -.25f };
        water.current_width = 32;
    }
    water.wake_amplitude = wake ? .38f : 0;
    return water;
}

static bool displaced(const BoatWater *water)
{
    return water->amplitude != 0 || water->wake_amplitude != 0 || water->player_wake_enabled
        || water->objects || water->traffic_objects || water->vessel.height > 0
        || water->radial != NULL || (water->storm.radius > 0 && water->storm.height > 0);
}

bool boat_water_displaced(BoatWater water) { return displaced(&water); }

void boat_water_emit_wake(
    BoatWater *water, Vector2 start, Vector2 end, double emitted, float duration, float speed)
{
    Vector2 delta = Vector2Subtract(end, start);
    float distance = Vector2Length(delta);
    if (!water->player_wake_enabled || speed <= 2 || distance < .2f || duration <= 0)
        return;
    water->player_wake[water->player_wake_next]
        = (BoatWakeSegment) { .position = Vector2Scale(Vector2Add(start, end), .5f),
              .direction = Vector2Scale(delta, 1 / distance),
              .length = fmaxf(distance, 1.2f),
              .emitted = emitted - duration * .5f,
              .amplitude = .65f * Clamp((speed - 2) / 10, 0, 1),
              .inverse_speed = duration / distance };
    water->player_wake_next = (water->player_wake_next + 1) % BOAT_WAKE_SEGMENTS;
    if (water->player_wake_count < BOAT_WAKE_SEGMENTS)
        ++water->player_wake_count;
}

int boat_water_wake_uniforms(BoatWater water, double time, float *origins, float *parameters)
{
    int count = 0;
    if (!water.player_wake_enabled)
        return 0;
    for (int i = 0; i < water.player_wake_count; ++i) {
        BoatWakeSegment s = water.player_wake[i];
        double age = time - s.emitted; // Bounded by the gate below before narrowing.
        if (age > BOAT_WAKE_LIFETIME + 1)
            continue;
        float a[] = { -s.position.x, s.position.y, -s.direction.x, s.direction.y };
        float b[] = { s.length, (float)age, s.amplitude, s.inverse_speed };
        for (int j = 0; j < 4; ++j) {
            origins[count * 4 + j] = a[j];
            parameters[count * 4 + j] = b[j];
        }
        ++count;
    }
    return count;
}

void boat_water_wave_uniforms(BoatWater water, double time, float *parameters)
{
    const BoatWave waves[]
        = { { water.amplitude, water.wavelength, water.angular_speed, water.direction },
              water.crossing[0], water.crossing[1] };
    for (int i = 0; i < 3; ++i) {
        float k = waves[i].wavelength > 0 ? 2 * PI / waves[i].wavelength : 0;
        parameters[i * 4] = -k * waves[i].direction.x; // Simulation X is reflected.
        parameters[i * 4 + 1] = k * waves[i].direction.y;
        parameters[i * 4 + 2] = waves[i].wavelength > 0 ? waves[i].amplitude : 0;
        parameters[i * 4 + 3] = boat_phase(waves[i].angular_speed, time);
    }
}

static float crossing_clock(const BoatWater *water, double time)
{
    // Emission ends at 12 s and each packet lives 12 s; later times are permanently calm.
    return water->wake_amplitude != 0 && time > 0 && time < 24 ? (float)time : -1;
}

float boat_water_crossing_clock(BoatWater water, double time)
{
    return crossing_clock(&water, time);
}

static Vector2 current_at(const BoatWater *water, Vector2 position)
{
    float weight = 1;
    if (water->current_width > 0) {
        float x = (position.x - water->current_center) / water->current_width;
        weight = fmaxf(0, 1 - x * x);
        weight *= weight;
    }
    return Vector2Scale(water->current, weight);
}

Vector2 boat_water_current(BoatWater water, Vector2 position)
{
    return current_at(&water, position);
}

bool boat_water_wake_source(BoatWater water, double time, Vector2 *position)
{
    float t = (float)fmin(fmax(time, 0), 12); // Inactive positions are clamped to the path.
    *position = Vector2Add(water.wake_offset, (Vector2) { -24 + 4 * t, -8 });
    return water.wake_amplitude != 0 && time >= 0 && time <= 12;
}

// Compact wave packet with analytic spatial/time derivatives. Keep the matching GLSL
// recipe in waterfront_water.vs/fs in sync. It is a scripted wake, not fluid simulation.
static void crossing_wake(const BoatWater *water, Vector2 p, double time, float *height,
    Vector2 *gradient, float *vertical_speed)
{
    float clock = crossing_clock(water, time);
    if (clock < 0)
        return;
    p = Vector2Subtract(p, water->wake_offset);
    float emitted = (p.x + 24) / 4;
    float age = clock - emitted;
    if (emitted <= 0 || emitted >= 12 || age <= 0 || age >= 12)
        return;
    float radius = sqrtf((p.y + 8) * (p.y + 8) + .25f);
    float q = radius - 2.4f * age;
    if (fabsf(q) >= 5)
        return;
    float k = 2 * PI / 6;
    float edge = 1 - q * q / 25;
    float f = sinf(k * q) * edge * edge * edge;
    float df = k * cosf(k * q) * edge * edge * edge - sinf(k * q) * 6 * q / 25 * edge * edge;
    float window = sinf(PI * emitted / 12), envelope = sinf(PI * age / 12);
    float dw = PI / 12 * sinf(2 * PI * emitted / 12);
    float de = PI / 12 * sinf(2 * PI * age / 12);
    window *= window;
    envelope *= envelope;
    float a = water->wake_amplitude;
    *height += a * window * envelope * f;
    gradient->x += a / 4 * ((dw * envelope - window * de) * f + window * envelope * df * 2.4f);
    gradient->y += a * window * envelope * df * (p.y + 8) / radius;
    *vertical_speed += a * window * (de * f - envelope * df * 2.4f);
}

static float wake_smooth(float start, float end, float x, float *derivative)
{
    float t = Clamp((x - start) / (end - start), 0, 1);
    *derivative = 6 * t * (1 - t) / (end - start);
    return t * t * (3 - 2 * t);
}

// A path-aligned packet expands from both sides of each travelled segment. Smooth
// normalization bounds overlapping/looped wakes, including all derivative terms.
static void player_wake(const BoatWater *water, Vector2 p, double time, float *height,
    Vector2 *gradient, float *vertical_speed)
{
    if (!water->player_wake_enabled)
        return;
    float sum = 0, weight = 0, sum_t = 0, weight_t = 0;
    Vector2 sum_g = { 0 }, weight_g = { 0 };
    for (int i = 0; i < water->player_wake_count; ++i) {
        const BoatWakeSegment *s = &water->player_wake[i];
        double elapsed = time - s->emitted;
        if (elapsed < -1 || elapsed > BOAT_WAKE_LIFETIME + 1)
            continue;
        float clock = (float)elapsed;
        Vector2 delta = Vector2Subtract(p, s->position);
        Vector2 normal = { -s->direction.y, s->direction.x };
        float u = Vector2DotProduct(delta, s->direction);
        if (fabsf(u) >= s->length)
            continue;
        float age = clock - u * s->inverse_speed;
        if (age <= .4f || age >= BOAT_WAKE_LIFETIME)
            continue;
        float v = Vector2DotProduct(delta, normal);
        float radius = sqrtf(v * v + .25f);
        float q = radius - .9f - 1.6f * age;
        if (fabsf(q) >= 4.5f)
            continue;
        float a = 1 - u * u / (s->length * s->length), b = 1 - q * q / 20.25f;
        float wl = a * a * a, wq = b * b * b;
        float dl = -6 * u / (s->length * s->length) * a * a;
        float dq = -6 * q / 20.25f * b * b;
        Vector2 age_g = Vector2Scale(s->direction, -s->inverse_speed);
        Vector2 q_g = Vector2Subtract(Vector2Scale(normal, v / radius), Vector2Scale(age_g, 1.6f));
        float w = wl * wq, wt = -1.6f * wl * dq;
        Vector2 wg = Vector2Add(Vector2Scale(s->direction, dl * wq), Vector2Scale(q_g, wl * dq));
        float dr, df;
        float rise = wake_smooth(.4f, 1.4f, age, &dr);
        float fall = 1 - wake_smooth(14, BOAT_WAKE_LIFETIME, age, &df);
        float envelope = rise * fall, de = dr * fall - rise * df;
        // Crests outrun the packet: phase speed 3 m/s, envelope speed 1.6 m/s.
        float phase = q - 1.4f * age;
        Vector2 phase_g = Vector2Subtract(q_g, Vector2Scale(age_g, 1.4f));
        float k = 2 * PI / 6, wave = sinf(k * phase), slope = k * cosf(k * phase);
        float value = s->amplitude * envelope * wave;
        Vector2 value_g = Vector2Scale(
            Vector2Add(Vector2Scale(age_g, de * wave), Vector2Scale(phase_g, envelope * slope)),
            s->amplitude);
        sum += w * value;
        sum_g = Vector2Add(sum_g, Vector2Add(Vector2Scale(wg, value), Vector2Scale(value_g, w)));
        sum_t += wt * value + w * s->amplitude * (de * wave - 3.0f * envelope * slope);
        weight += w;
        weight_g = Vector2Add(weight_g, wg);
        weight_t += wt;
    }
    float divisor = sqrtf(1 + weight * weight);
    float correction = sum * weight / (1 + weight * weight);
    *height += sum / divisor;
    *gradient = Vector2Add(*gradient,
        Vector2Scale(Vector2Subtract(sum_g, Vector2Scale(weight_g, correction)), 1 / divisor));
    *vertical_speed += (sum_t - correction * weight_t) / divisor;
}

// Compact smooth crest: q=1-((distance-radius)/2)^2, height=A*q^2.
// The radius envelope and its time derivative both vanish at the configured range limit.
BoatStormSample boat_water_radial_query(BoatRadialWave w, Vector2 position, double time)
{
    BoatStormSample out = { 0 };
    double age = time - boat_tick_seconds(w.born);
    if (!w.active || age < 0 || w.range <= 0)
        return out;
    float radius = (float)age * w.speed;
    if (radius >= w.range)
        return out;
    Vector2 delta = Vector2Subtract(position, w.centre);
    float distance = Vector2Length(delta), x = (distance - radius) / 2;
    if (fabsf(x) >= 1)
        return out;
    float q = 1 - x * x, fade = 1 - radius / w.range;
    float slope = -2 * w.height * fade * fade * x * q;
    out.height = w.height * fade * fade * q * q;
    out.gradient = distance > 1e-5f ? Vector2Scale(delta, slope / distance) : (Vector2) { 0 };
    out.vertical_velocity = -w.speed * slope - 2 * w.height * fade * w.speed / w.range * q * q;
    return out;
}

void boat_water_radial_uniforms(const BoatRadialWave *waves, double time, float p[32])
{
    for (unsigned i = 0; i < BOAT_RADIAL_WAVES; ++i) {
        BoatRadialWave w = waves ? waves[i] : (BoatRadialWave) { 0 };
        float radius = (float)(time - boat_tick_seconds(w.born)) * w.speed;
        p[8 * i] = -w.centre.x;
        p[8 * i + 1] = w.centre.y;
        p[8 * i + 2] = radius;
        p[8 * i + 3] = w.active && radius >= 0 && radius < w.range ? w.height : 0;
        p[8 * i + 4] = w.speed;
        p[8 * i + 5] = w.range;
        p[8 * i + 6] = p[8 * i + 7] = 0;
    }
}

Vector2 boat_water_storm_centre(BoatStorm storm, double time)
{
    double age = time - storm.started;
    return (Vector2) { storm.centre.x + (float)(storm.velocity.x * age),
        storm.centre.y + (float)(storm.velocity.y * age) };
}

float boat_water_storm_grown(BoatStorm storm, double time, float *rate)
{
    if (rate)
        *rate = 0;
    if (storm.build <= 0)
        return 1;
    double age = time - storm.started;
    if (age <= 0)
        return 0;
    if (age >= storm.build)
        return 1;
    float u = (float)(age / storm.build);
    if (rate)
        *rate = 6 * u * (1 - u) / storm.build;
    return u * u * (3 - 2 * u);
}

void boat_water_storm_uniforms(BoatStorm storm, double time, float parameters[12])
{
    Vector2 centre = boat_water_storm_centre(storm, time);
    parameters[0] = -centre.x;
    parameters[1] = centre.y;
    parameters[2] = storm.radius;
    // The shaders take the grown height; its growth rate only matters to hull physics.
    parameters[3] = storm.height * boat_water_storm_grown(storm, time, NULL);
    parameters[4] = boat_phase(3.2f, time - storm.started);
    parameters[5] = boat_phase(3.75f, time - storm.started);
    parameters[6] = boat_phase(2.45f, time - storm.started);
    parameters[7] = boat_phase(4.15f, time - storm.started);
    parameters[8] = -storm.velocity.x;
    parameters[9] = storm.velocity.y;
    parameters[10] = boat_phase(.37f, time - storm.started);
    parameters[11] = 0;
}

BoatStormSample boat_water_storm_query(BoatStorm storm, Vector2 position, double time)
{
    BoatStormSample sample = { 0 };
    float rate;
    float grown = boat_water_storm_grown(storm, time, &rate);
    if (storm.radius <= 0 || storm.height <= 0 || (grown <= 0 && rate <= 0))
        return sample;
    Vector2 p = Vector2Subtract(position, boat_water_storm_centre(storm, time));
    float r2 = Vector2LengthSqr(p) / (storm.radius * storm.radius);
    if (r2 >= 1)
        return sample;
    float edge = 1 - r2, envelope = edge * edge * edge;
    Vector2 envelope_gradient = Vector2Scale(p, -6 * edge * edge / (storm.radius * storm.radius));
    // A directional, non-repeating mixture: nearby headings avoid an egg-carton crossing
    // grid, while long phase bends and asymmetric crests break up parallel regular ridges.
    // Keep the matching production shader recipe in sync.
    const Vector2 wave[] = { { .27f, -.32f }, { .38f, -.26f }, { .16f, -.30f }, { .47f, -.18f } };
    const float speed[] = { 3.2f, 3.75f, 2.45f, 4.15f };
    const float amplitude[] = { .8f, .4f, .22f, .12f };
    const float offset[] = { 0, 1.1f, 2.7f, 4.2f };
    const Vector2 bend_wave = { .013f, .017f };
    float bend_phase = Vector2DotProduct(p, bend_wave) - boat_phase(.37f, time - storm.started);
    for (int i = 0; i < 4; ++i) {
        float bend = bend_phase + offset[i] * .73f;
        float bend_slope = .85f * cosf(bend);
        float phase = Vector2DotProduct(p, wave[i]) + .85f * sinf(bend) + offset[i]
            - boat_phase(speed[i], time - storm.started);
        float h = storm.height * amplitude[i] * (sinf(phase) + .12f * sinf(2 * phase + .4f));
        float slope = storm.height * amplitude[i] * (cosf(phase) + .24f * cosf(2 * phase + .4f));
        Vector2 phase_gradient = Vector2Add(wave[i], Vector2Scale(bend_wave, bend_slope));
        sample.height += envelope * h;
        sample.gradient = Vector2Add(sample.gradient,
            Vector2Add(Vector2Scale(envelope_gradient, h),
                Vector2Scale(phase_gradient, envelope * slope)));
        sample.vertical_velocity -= envelope * slope * (speed[i] + .37f * bend_slope);
    }
    // Translating the compact envelope and its local crests also moves the surface at a
    // fixed world point. Include that term in the physical vertical velocity.
    sample.vertical_velocity -= Vector2DotProduct(sample.gradient, storm.velocity);
    // A growing patch also lifts its crests: d(grown * h)/dt adds rate * h.
    if (grown < 1) {
        sample.vertical_velocity = grown * sample.vertical_velocity + rate * sample.height;
        sample.height *= grown;
        sample.gradient = Vector2Scale(sample.gradient, grown);
    }
    return sample;
}

static BoatWaterSample query(const BoatWater *water, Vector2 position, double time)
{
    BoatWaterSample sample = { .normal = { 0, 1, 0 } };
    Vector2 current = current_at(water, position);
    sample.velocity.x = current.x;
    sample.velocity.z = current.y;
    if (!displaced(water))
        return sample;
    const BoatWave waves[]
        = { { water->amplitude, water->wavelength, water->angular_speed, water->direction },
              water->crossing[0], water->crossing[1] };
    Vector2 gradient = { 0 };
    for (int i = 0; i < 3; ++i) {
        BoatWave w = waves[i];
        if (w.amplitude == 0 || w.wavelength <= 0)
            continue;
        float k = 2 * PI / w.wavelength;
        // The packed phase is shared with both shader stages; see boat_water_wave_uniforms.
        float phase
            = k * Vector2DotProduct(w.direction, position) - boat_phase(w.angular_speed, time);
        float slope = w.amplitude * k * cosf(phase);
        sample.height += w.amplitude * sinf(phase);
        gradient = Vector2Add(gradient, Vector2Scale(w.direction, slope));
        sample.velocity.y -= w.amplitude * w.angular_speed * cosf(phase);
    }
    if (water->radial)
        for (unsigned i = 0; i < BOAT_RADIAL_WAVES; ++i) {
            BoatStormSample ring = boat_water_radial_query(water->radial[i], position, time);
            sample.height += ring.height;
            gradient = Vector2Add(gradient, ring.gradient);
            sample.velocity.y += ring.vertical_velocity;
            sample.crest_foam = fmaxf(sample.crest_foam, Clamp(ring.height / .8f, 0, 1));
        }
    if (water->storm.radius > 0 && water->storm.height > 0) {
        BoatStormSample storm = boat_water_storm_query(water->storm, position, time);
        sample.height += storm.height;
        gradient = Vector2Add(gradient, storm.gradient);
        sample.velocity.y += storm.vertical_velocity;
        sample.crest_foam = Clamp(storm.height / .8f, 0, 1);
    }
    crossing_wake(water, position, time, &sample.height, &gradient, &sample.velocity.y);
    if (water->vessel.height > 0) {
        BoatVesselWakeSample q = boat_vessel_wake_query(water->vessel, position, time);
        sample.height += q.height;
        gradient = Vector2Add(gradient, q.gradient);
        sample.velocity.y += q.vertical_velocity;
    }
    player_wake(water, position, time, &sample.height, &gradient, &sample.velocity.y);
    if (water->objects) {
        BoatWakeObjectSample q = boat_wake_objects_query(water->objects, position);
        sample.height += q.height;
        sample.crest_foam += q.crest_foam;
        gradient = Vector2Add(gradient, q.gradient);
        sample.velocity.y += q.vertical_velocity;
    }
    if (water->traffic_objects) {
        BoatWakeObjectSample q = boat_wake_objects_query(water->traffic_objects, position);
        sample.height += q.height;
        sample.crest_foam += q.crest_foam;
        gradient = Vector2Add(gradient, q.gradient);
        sample.velocity.y += q.vertical_velocity;
    }
    // Material surface velocity includes the current's travel along the local slope.
    sample.velocity.y += Vector2DotProduct(current, gradient);
    sample.normal = Vector3Normalize((Vector3) { -gradient.x, 1, -gradient.y });
    // Horizontal velocity is the authored current; waves add vertical surface motion.
    return sample;
}

BoatWaterSample boat_water_query(BoatWater water, Vector2 position, double time)
{
    return query(&water, position, time);
}

Vector3 boat_water_resistance(
    BoatWaterSample water, Vector3 point_velocity, float damping, float support)
{
    if (support <= 0 || damping <= 0 || water.normal.y <= 0)
        return (Vector3) { 0 };
    Vector3 relative = Vector3Subtract(point_velocity, water.velocity);
    float force = -damping * Vector3DotProduct(relative, water.normal);
    force = fmaxf(force, -support / water.normal.y);
    return Vector3Scale(water.normal, force);
}

BoatWaterSample boat_water_query_field(
    BoatWater water, const BoatWake *field, Vector2 position, double time)
{
    BoatWaterSample s = query(&water, position, time);
    if (!field)
        return s;
    BoatWakeSample q;
    if (!boat_wake_query(field, position.x, position.y, &q))
        return s;
    float gx = -s.normal.x / s.normal.y + (float)q.gradient_x;
    float gz = -s.normal.z / s.normal.y + (float)q.gradient_z;
    s.height += (float)q.height;
    s.normal = Vector3Normalize((Vector3) { -gx, 1, -gz });
    s.velocity = Vector3Add(s.velocity,
        (Vector3) { (float)q.velocity_x, (float)q.vertical_velocity, (float)q.velocity_z });
    return s;
}
