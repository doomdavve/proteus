#include "wake_geometry.h"
#include "raymath.h"
#include <math.h>
#include <string.h>

const char *r2_name(R2Mode mode)
{
    const char *names[] = { "baseline", "short", "open", "angle45", "short_matched" };
    return mode < R2_MODES ? names[mode] : "invalid";
}

bool r2_short(R2Mode mode) { return mode == R2_SHORT || mode == R2_MATCHED; }

void r2_init(R2Field *w, R2Mode mode) { *w = (R2Field) { .mode = mode }; }

void r2_advance(R2Field *w)
{
    ++w->tick;
    for (unsigned i = 0; i < R2_CAPACITY; ++i)
        if (w->records[i].amplitude && w->tick - w->records[i].born >= BOAT_OBJECT_LIFE_TICKS) {
            w->records[i].amplitude = 0;
            --w->live;
            ++w->expired;
        }
}

// Production tracking copied deliberately, with only private cadence/capacity parameters.
// The contract compares every baseline record/emitter with production through full saturation.
bool r2_track(R2Field *w, unsigned boat, R2Source source)
{
    if (boat >= BOAT_OBJECT_BOATS)
        return false;
    Vector2 stern = source.position;
    float speed = source.speed, wet = source.wet;
    unsigned interval = r2_short(w->mode) ? 30 : BOAT_OBJECT_EMIT_TICKS;
    unsigned slots = r2_short(w->mode) ? R2_SLOTS : BOAT_OBJECT_PER_BOAT;
    BoatWakeObjectEmitter *e = &w->emitters[boat];
    if (e->initialized && e->tracked == w->tick)
        return false;
    bool restart = !e->initialized || w->tick - e->tracked != 1 || speed <= 3 || wet <= .05f;
    float target = BOAT_OBJECT_DEFAULT_HEIGHT * Clamp((speed - 3) / 12, 0, 1) * wet;
    e->tracked = w->tick;
    e->initialized = true;
    if (restart) {
        e->start = stern;
        e->started = w->tick;
        e->strength = speed > 3 && wet > .05f ? target : 0;
        e->strength_sum = 0;
        return true;
    }
    e->strength += (target - e->strength) * (1 - expf(-1 / (120 * .35f)));
    e->strength_sum += e->strength;
    if (w->tick - e->started < interval)
        return true;
    Vector2 delta = Vector2Subtract(stern, e->start);
    float length = Vector2Length(delta);
    if (length >= .5f) {
        BoatWakeObject *o = &w->records[boat * R2_SLOTS + e->next];
        if (o->amplitude)
            return false;
        *o = (BoatWakeObject) { .position = Vector2Scale(Vector2Add(e->start, stern), .5f),
            .direction = Vector2Scale(delta, 1 / length),
            .length = fmaxf(length, 2),
            .amplitude = e->strength_sum / (float)(w->tick - e->started),
            .inverse_speed = (float)(w->tick - e->started) / (120 * length),
            .born = w->tick };
        e->next = (e->next + 1) % slots;
        ++w->births;
        ++w->live;
        if (w->live > w->peak)
            w->peak = w->live;
    }
    e->start = stern;
    e->started = w->tick;
    e->strength_sum = 0;
    return true;
}

R2Source r2_source(const char *path, unsigned tick, unsigned emitter, bool dispersed)
{
    float t = tick / 120.0f, phase = emitter * PI / 2;
    R2Source s = { .speed = 15, .wet = 1 };
    if (!strcmp(path, "straight") || !strcmp(path, "crossing")) {
        s.position = (Vector2) { 15 * (t - 25) * cosf(phase), 15 * (t - 25) * sinf(phase) };
    } else if (!strcmp(path, "eight")) {
        float a = t * .35f + phase;
        s.position = (Vector2) { 30 * sinf(a), 20 * sinf(2 * a) };
        s.speed = hypotf(10.5f * cosf(a), 14 * cosf(2 * a));
    } else {
        float radius = !strcmp(path, "wide") ? 50 : 12;
        float travel = t;
        if (!strcmp(path, "reverse")) {
            float cycle = fmodf(t, 24);
            travel = cycle <= 12 ? cycle : 24 - cycle;
        }
        float a = travel * 15 / radius + phase;
        s.position = (Vector2) { radius * cosf(a), radius * sinf(a) };
    }
    if (dispersed)
        s.position.x += 300 * emitter;
    return s;
}

static float smooth(float t, float *derivative)
{
    t = Clamp(t, 0, 1);
    *derivative = 6 * t * (1 - t);
    return t * t * (3 - 2 * t);
}

static float bump(float q, float width, float *slope)
{
    float x = q / width;
    if (fabsf(x) >= 1) {
        *slope = 0;
        return 0;
    }
    float a = 1 - x * x;
    *slope = -6 * x / width * a * a;
    return a * a * a;
}

BoatWakeObjectSample r2_piece(const BoatWakeObject *o, R2Mode mode, Vector2 p, double seconds)
{
    BoatWakeObjectSample result = { 0 };
    if (!o->amplitude || seconds < o->born / 120.0 || seconds >= (o->born + 1920) / 120.0)
        return result;
    Vector2 d = Vector2Subtract(p, o->position), n = { -o->direction.y, o->direction.x };
    float u = Vector2DotProduct(d, o->direction), v = Vector2DotProduct(d, n);
    float clock = (float)(seconds * 120.0 - o->born) / 120, s = u, su = 1, excess = 0, eu = 0;
    bool rounded = mode == R2_OPEN || mode == R2_ANGLE;
    if (!rounded && fabsf(u) >= o->length)
        return result;
    if (rounded) {
        float delta = fminf(.1f * o->length, .25f), x = fabsf(u) - .5f * o->length;
        float derivative = 0;
        if (x > 0 && x < delta) {
            excess = x * x / (2 * delta);
            derivative = x / delta;
        } else if (x >= delta) {
            excess = x - delta / 2;
            derivative = 1;
        }
        float sign = u < 0 ? -1 : 1;
        eu = sign * derivative;
        s = u - sign * excess;
        su = 1 - derivative;
    }
    float radius = sqrtf(v * v + excess * excess + .25f);
    float ru = excess * eu / radius, rv = v / radius;
    // Optional A1 control: retain A0 births/windows, align mature wave phase and the
    // straight 15 m/s fade offset to the 60-tick reference. Birth gates remain literal.
    float age = clock - s * o->inverse_speed - (mode == R2_MATCHED ? .125f : 0),
          age_u = -o->inverse_speed * su;
    float q = radius - .9f - 2.4f * age;
    if (q <= -7 || q >= 2)
        return result;
    float ds, dt, dr, df, db;
    float shape = bump(q, q < 0 ? 4 : 2, &ds) - .28f * bump(q + 4.5f, 2.5f, &dt);
    ds -= .28f * dt;
    float rise = smooth(age / .65f, &dr);
    float fade_start = 12 - o->length * o->inverse_speed - (mode == R2_MATCHED ? .25f : 0);
    float fall = 1 - smooth((age - fade_start) / 4, &df);
    float birth = smooth(clock / .3f, &db);
    float envelope = birth * rise * fall;
    float envelope_age = birth * (dr / .65f * fall - rise * df / 4);
    float envelope_t = db / .3f * rise * fall + envelope_age;
    float angle = PI * s / (2 * o->length), c = cosf(angle);
    float window = c * c, window_u = -PI / o->length * c * sinf(angle) * su;
    float mask = 1, mask_u = 0, mask_v = 0;
    if (mode == R2_ANGLE) {
        const float from = .5f, width = .7071067811865475f - .5f;
        float gamma = excess / radius, derivative;
        mask = 1 - smooth((gamma - from) / width, &derivative);
        mask_u = -derivative / width * (eu / radius - excess * ru / (radius * radius));
        mask_v = derivative / width * excess * rv / (radius * radius);
    }
    float weight = window * mask, weight_u = window_u * mask + window * mask_u;
    float weight_v = window * mask_v;
    float qu = ru + 2.4f * o->inverse_speed * su, qv = rv;
    float hu = o->amplitude
        * ((weight_u * envelope + weight * envelope_age * age_u) * shape
            + weight * envelope * ds * qu);
    float hv = o->amplitude * (weight_v * envelope * shape + weight * envelope * ds * qv);
    result.height = o->amplitude * weight * envelope * shape;
    result.gradient = Vector2Add(Vector2Scale(o->direction, hu), Vector2Scale(n, hv));
    result.vertical_velocity = o->amplitude * weight * (envelope_t * shape - envelope * ds * 2.4f);
    float narrow = fmaxf(0, 1 - q * q / (.45f * .45f));
    float breakup = .55f + .225f * sinf(2.3f * s + 3 * age) + .225f * sinf(5.1f * s - 4.2f * age);
    result.crest_foam = fminf(1, fmaxf(0, o->amplitude / 1.8f)) * weight * envelope * narrow
        * narrow * narrow * breakup;
    return result;
}

BoatWakeObjectSample r2_query(const R2Field *w, Vector2 p, double seconds)
{
    BoatWakeObjectSample result = { 0 };
    unsigned slots = r2_short(w->mode) ? R2_SLOTS : BOAT_OBJECT_PER_BOAT;
    for (unsigned e = 0; e < BOAT_OBJECT_BOATS; ++e)
        for (unsigned i = 0; i < slots; ++i) {
            BoatWakeObjectSample s = r2_piece(&w->records[e * R2_SLOTS + i], w->mode, p, seconds);
            result.height += s.height;
            result.gradient = Vector2Add(result.gradient, s.gradient);
            result.vertical_velocity += s.vertical_velocity;
            result.crest_foam += s.crest_foam;
        }
    return result;
}
