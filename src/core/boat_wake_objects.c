#include "boat_wake_objects.h"
#include "raymath.h"
#include <math.h>
#include <string.h>

bool boat_wake_objects_init(BoatWakeObjects *w, unsigned life, float height)
{
    if (!w || life < 240 || life > BOAT_OBJECT_LIFE_TICKS || !isfinite(height) || height < .1f
        || height > 3)
        return false;
    *w = (BoatWakeObjects) { .life_ticks = life, .crest_height = height };
    return true;
}

void boat_wake_objects_advance(BoatWakeObjects *w)
{
    ++w->tick;
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        BoatWakeObject *o = &w->objects[i];
        if (o->amplitude && w->tick - o->born >= w->life_ticks) {
            o->amplitude = 0;
            --w->live;
            ++w->expired;
        }
    }
}

bool boat_wake_objects_track(
    BoatWakeObjects *w, unsigned boat, Vector2 stern, float speed, float wet)
{
    if (!w || boat >= BOAT_OBJECT_BOATS || !isfinite(stern.x) || !isfinite(stern.y)
        || !isfinite(speed) || speed < 0 || !isfinite(wet) || wet < 0 || wet > 1)
        return false;
    BoatWakeObjectEmitter *e = &w->emitters[boat];
    if (e->initialized && e->tracked == w->tick)
        return false;
    bool restart = !e->initialized || w->tick - e->tracked != 1 || speed <= 3 || wet <= .05f;
    float target = w->crest_height * Clamp((speed - 3) / 12, 0, 1) * wet;
    e->tracked = w->tick;
    e->initialized = true;
    if (restart) {
        e->start = stern;
        e->started = w->tick;
        e->strength = speed > 3 && wet > .05f ? target : 0;
        e->strength_sum = 0;
        return true;
    }
    // Brief wetness/speed fluctuations must not cut a notch into an entire piece.
    e->strength += (target - e->strength) * (1 - expf(-1 / (120 * .35f)));
    e->strength_sum += e->strength;
    if (w->tick - e->started < BOAT_OBJECT_EMIT_TICKS)
        return true;
    Vector2 delta = Vector2Subtract(stern, e->start);
    float length = Vector2Length(delta);
    if (length >= .5f) {
        BoatWakeObject *o = &w->objects[boat * BOAT_OBJECT_PER_BOAT + e->next];
        if (o->amplitude != 0)
            return false; // A caller violated the emission/lifetime capacity contract.
        *o = (BoatWakeObject) { .position = Vector2Scale(Vector2Add(e->start, stern), .5f),
            .direction = Vector2Scale(delta, 1 / length),
            .length = fmaxf(length, 2),
            .amplitude = e->strength_sum / (float)(w->tick - e->started),
            .inverse_speed = (float)(w->tick - e->started) / (120 * length),
            .born = w->tick };
        e->next = (e->next + 1) % BOAT_OBJECT_PER_BOAT;
        ++w->emitted;
        ++w->live;
        if (w->live > w->peak_live)
            w->peak_live = w->live;
    }
    e->start = stern;
    e->started = w->tick;
    e->strength_sum = 0;
    return true;
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

/* BEGIN OBJECT RECIPE. Matching height/gradient recipe is in both water shaders.
 * Age is relative integer ticks. The crest moves at 2.4 m/s, with a steeper outer
 * launch face and a broad inner landing slope followed by a smaller signed trough.
 * Pieces are independent: there is no connected ribbon inversion at tight turns. */
BoatWakeObjectSample boat_wake_objects_query(const BoatWakeObjects *w, Vector2 p)
{
    return boat_wake_objects_query_at(w, p, w ? w->tick : 0);
}

static BoatWakeObjectSample query_range(
    const BoatWakeObjects *w, Vector2 p, uint64_t tick, unsigned first, unsigned end)
{
    BoatWakeObjectSample s = { 0 };
    if (!w || tick < w->tick)
        return s;
    float life = w->life_ticks / 120.0f;
    for (unsigned i = first; i < end; ++i) {
        const BoatWakeObject *o = &w->objects[i];
        if (!o->amplitude)
            continue;
        Vector2 delta = Vector2Subtract(p, o->position);
        float u = Vector2DotProduct(delta, o->direction);
        if (fabsf(u) >= o->length)
            continue;
        Vector2 normal = { -o->direction.y, o->direction.x };
        float v = Vector2DotProduct(delta, normal), radius = sqrtf(v * v + .25f);
        float clock = (float)(tick - o->born) / 120;
        float age = clock - u * o->inverse_speed;
        float q = radius - .9f - 2.4f * age;
        if (q <= -7 || q >= 2)
            continue;
        float ds, dt;
        float shape = bump(q, q < 0 ? 4 : 2, &ds) - .28f * bump(q + 4.5f, 2.5f, &dt);
        ds -= .28f * dt;
        float dr, df, db;
        // Local age agrees across adjoining straight-running pieces. Finish fading
        // even the youngest supported point before the object's hard expiry tick.
        float rise = smooth(age / .65f, &dr);
        float fade_start = .75f * life - o->length * o->inverse_speed;
        float fall = 1 - smooth((age - fade_start) / (.25f * life), &df);
        float birth = smooth(clock / .3f, &db);
        float envelope = birth * rise * fall;
        float envelope_age = birth * (dr / .65f * fall - rise * df / (.25f * life));
        float envelope_t = db / .3f * rise * fall + envelope_age;
        float angle = PI * u / (2 * o->length), c = cosf(angle);
        float window = c * c, window_u = -PI / o->length * c * sinf(angle);
        Vector2 qg = Vector2Add(
            Vector2Scale(normal, v / radius), Vector2Scale(o->direction, 2.4f * o->inverse_speed));
        float a = o->amplitude;
        s.height += a * window * envelope * shape;
        float narrow = fmaxf(0, 1 - q * q / (.45f * .45f));
        float breakup
            = .55f + .225f * sinf(2.3f * u + 3 * age) + .225f * sinf(5.1f * u - 4.2f * age);
        s.crest_foam += fminf(1, fmaxf(0, a / 1.8f)) * window * envelope * narrow * narrow * narrow
            * breakup;
        Vector2 geometry = Vector2Scale(
            Vector2Add(Vector2Scale(o->direction, window_u * shape), Vector2Scale(qg, window * ds)),
            envelope);
        Vector2 age_gradient = Vector2Scale(o->direction, -o->inverse_speed);
        s.gradient = Vector2Add(s.gradient,
            Vector2Scale(
                Vector2Add(geometry, Vector2Scale(age_gradient, window * shape * envelope_age)),
                a));
        s.vertical_velocity += a * window * (envelope_t * shape - envelope * ds * 2.4f);
    }
    return s;
}

BoatWakeObjectSample boat_wake_objects_query_at(const BoatWakeObjects *w, Vector2 p, uint64_t tick)
{
    return query_range(w, p, tick, 0, BOAT_OBJECT_CAPACITY);
}

BoatWakeObjectSample boat_wake_objects_query_emitter(
    const BoatWakeObjects *w, unsigned emitter, Vector2 p)
{
    if (emitter >= BOAT_OBJECT_BOATS)
        return (BoatWakeObjectSample) { 0 };
    return query_range(w, p, w ? w->tick : 0, emitter * BOAT_OBJECT_PER_BOAT,
        (emitter + 1) * BOAT_OBJECT_PER_BOAT);
}

int boat_wake_objects_pack(const BoatWakeObjects *w, float *origins, float *parameters)
{
    int count = 0;
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        BoatWakeObject o = w->objects[i];
        if (!o.amplitude)
            continue;
        float a[] = { -o.position.x, o.position.y, -o.direction.x, o.direction.y };
        float b[] = { o.length, (float)(w->tick - o.born) / 120, o.amplitude, o.inverse_speed };
        memcpy(origins + count * 4, a, sizeof(a));
        memcpy(parameters + count * 4, b, sizeof(b));
        ++count;
    }
    return count;
}
