#include "wake_capture.h"
#include "raymath.h"
#include <math.h>
#include <string.h>

static bool finite_point(Vector2 p) { return isfinite(p.x) && isfinite(p.y); }
static bool same_point(Vector2 a, Vector2 b) { return a.x == b.x && a.y == b.y; }

static bool same_span(R2Span a, R2Span b)
{
    return same_point(a.start, b.start) && same_point(a.end, b.end) && a.started == b.started
        && a.ended == b.ended && a.serial == b.serial;
}

static bool witness_valid(
    const R2Capture *c, unsigned emitter, uint64_t identity, uint64_t epoch, R2Span span)
{
    for (unsigned i = 0; i < BOAT_OBJECT_CAPACITY; ++i) {
        const R2Descriptor *d = &c->descriptors[i];
        if (d->span.serial == span.serial)
            return i / BOAT_OBJECT_PER_BOAT == emitter && d->identity == identity
                && d->epoch == epoch && d->eligible && same_span(d->span, span);
    }
    return c->field.tick >= span.ended && c->field.tick - span.ended >= BOAT_OBJECT_LIFE_TICKS;
}

static bool span_valid(R2Span s, uint64_t tick, uint64_t births)
{
    return finite_point(s.start) && finite_point(s.end) && s.serial && s.serial <= births
        && s.ended <= tick && s.ended >= BOAT_OBJECT_EMIT_TICKS
        && s.ended - s.started == BOAT_OBJECT_EMIT_TICKS
        && isfinite(Vector2Distance(s.start, s.end)) && Vector2Distance(s.start, s.end) >= .5f;
}

static float curvature(R2Span a, R2Span b)
{
    Vector2 da = Vector2Subtract(a.end, a.start), db = Vector2Subtract(b.end, b.start);
    double cross = (double)da.x * db.y - (double)da.y * db.x;
    double dot = (double)da.x * db.x + (double)da.y * db.y;
    // Strict |theta| < pi/2. Dot classification rejects exact right angles without PI rounding.
    if (dot <= 0)
        return 0;
    double theta = atan2(cross, dot);
    double length = (hypot(da.x, da.y) + hypot(db.x, db.y)) * .5;
    return (float)(2 * sin(theta * .5) / length);
}

void r2_capture_init(R2Capture *c)
{
    memset(c, 0, sizeof(*c));
    r2_init(&c->field, R2_BASELINE);
}

bool r2_capture_restart(R2Capture *c, unsigned emitter)
{
    if (emitter >= BOAT_OBJECT_BOATS || c->sources[emitter].epoch == UINT64_MAX)
        return false;
    R2CaptureSource *s = &c->sources[emitter];
    ++s->epoch;
    s->active = s->has_previous = s->span_eligible = false;
    memset(&s->previous, 0, sizeof(s->previous));
    c->field.emitters[emitter].initialized = false;
    return true;
}

bool r2_capture_advance(R2Capture *c)
{
    if (c->field.tick == UINT64_MAX)
        return false;
    r2_advance(&c->field);
    for (unsigned e = 0; e < BOAT_OBJECT_BOATS; ++e)
        for (unsigned i = 0; i < BOAT_OBJECT_PER_BOAT; ++i)
            if (!c->field.records[e * R2_SLOTS + i].amplitude)
                memset(&c->descriptors[e * BOAT_OBJECT_PER_BOAT + i], 0, sizeof(R2Descriptor));
    return true;
}

bool r2_capture_track(R2Capture *c, unsigned emitter, uint64_t identity, R2Source source)
{
    if (emitter >= BOAT_OBJECT_BOATS)
        return false;
    BoatWakeObjectEmitter *e = &c->field.emitters[emitter];
    R2CaptureSource *s = &c->sources[emitter];
    if (s->epoch == UINT64_MAX || c->field.births == UINT64_MAX)
        return false;
    if (e->initialized && e->tracked == c->field.tick)
        return false;
    if (!identity || !finite_point(source.position) || !isfinite(source.speed)
        || !isfinite(source.wet) || source.speed < 0 || source.wet < 0 || source.wet > 1) {
        if (!r2_capture_restart(c, emitter))
            return false;
        return false;
    }
    bool active = source.speed > 3 && source.wet > .05f;
    bool missed = e->initialized && c->field.tick - e->tracked != 1;
    bool identity_changed = s->identity != identity;
    bool restart = !e->initialized || missed || !active || identity_changed;
    if (!s->epoch || identity_changed || missed || active != s->active) {
        if (s->epoch == UINT64_MAX)
            return false;
        ++s->epoch;
        s->has_previous = s->span_eligible = false;
        memset(&s->previous, 0, sizeof(s->previous));
    }
    // Identity changes are explicit source resets. Wet/slow transitions retain raw baseline births.
    if (identity_changed)
        e->initialized = false;
    s->identity = identity;
    s->active = active;
    Vector2 start = e->start;
    uint64_t started = e->started, births = c->field.births;
    unsigned slot = e->next;
    bool ended = !restart && c->field.tick - e->started >= BOAT_OBJECT_EMIT_TICKS;
    bool eligible = s->span_eligible;
    if (births == UINT64_MAX)
        return false;
    if (ended) {
        Vector2 delta = Vector2Subtract(source.position, start);
        float length = Vector2Length(delta);
        Vector2 midpoint = Vector2Scale(Vector2Add(start, source.position), .5f);
        if (!finite_point(delta) || !finite_point(midpoint) || !isfinite(length)) {
            if (!r2_capture_restart(c, emitter))
                return false;
            return false;
        }
        if (length >= .5f && c->field.records[emitter * R2_SLOTS + slot].amplitude)
            return false;
        if (length < .5f && s->epoch == UINT64_MAX)
            return false;
    }
    if (!r2_track(&c->field, emitter, source))
        return false;
    if (restart || ended)
        s->span_eligible = active;
    if (c->field.births == births) {
        if (ended) {
            if (s->epoch == UINT64_MAX)
                return false;
            ++s->epoch;
            s->has_previous = false; // Omitted/invalid short chord breaks adjacency.
            memset(&s->previous, 0, sizeof(s->previous));
        }
        return true;
    }
    R2Descriptor *d = &c->descriptors[emitter * BOAT_OBJECT_PER_BOAT + slot];
    memset(d, 0, sizeof(*d));
    d->span = (R2Span) { .start = start,
        .end = source.position,
        .started = started,
        .ended = c->field.tick,
        .serial = c->field.births };
    d->identity = identity;
    d->epoch = s->epoch;
    d->eligible = eligible;
    d->paired = eligible && s->has_previous && s->previous.ended == started
        && same_point(s->previous.end, start);
    if (d->paired) {
        d->predecessor = s->previous;
        d->curvature = curvature(d->predecessor, d->span);
    }
    s->has_previous = eligible;
    s->previous = eligible ? d->span : (R2Span) { 0 };
    return true;
}

bool r2_capture_valid(const R2Capture *c)
{
    const R2Field *w = &c->field;
    if (w->mode != R2_BASELINE || w->live > BOAT_OBJECT_CAPACITY || w->peak < w->live
        || w->peak > BOAT_OBJECT_CAPACITY || w->births < w->expired
        || w->births - w->expired != w->live)
        return false;
    unsigned live = 0;
    for (unsigned e = 0; e < BOAT_OBJECT_BOATS; ++e) {
        const R2CaptureSource *s = &c->sources[e];
        const BoatWakeObjectEmitter *tracker = &w->emitters[e];
        if (tracker->next >= BOAT_OBJECT_PER_BOAT || !finite_point(tracker->start)
            || !isfinite(tracker->strength) || !isfinite(tracker->strength_sum)
            || tracker->strength < 0 || tracker->strength_sum < 0 || tracker->tracked > w->tick
            || tracker->started > tracker->tracked
            || (tracker->initialized && (!s->identity || !s->epoch))
            || (s->active && !tracker->initialized) || (s->span_eligible && !s->active)
            || (s->has_previous
                && (!s->identity || !s->epoch || !s->active
                    || !span_valid(s->previous, w->tick, w->births)
                    || !witness_valid(c, e, s->identity, s->epoch, s->previous)
                    || s->previous.ended != tracker->started
                    || !same_point(s->previous.end, tracker->start))))
            return false;
        for (unsigned i = 0; i < R2_SLOTS; ++i) {
            const BoatWakeObject *o = &w->records[e * R2_SLOTS + i];
            if (i >= BOAT_OBJECT_PER_BOAT) {
                if (o->amplitude)
                    return false;
                continue;
            }
            const R2Descriptor *d = &c->descriptors[e * BOAT_OBJECT_PER_BOAT + i];
            if (!o->amplitude) {
                if (d->span.serial || d->identity || d->epoch || d->curvature || d->paired)
                    return false;
                continue;
            }
            ++live;
            if (!span_valid(d->span, w->tick, w->births) || !d->identity || !d->epoch
                || d->epoch > s->epoch || !isfinite(d->curvature) || !isfinite(o->amplitude)
                || o->amplitude <= 0 || !finite_point(o->position) || !finite_point(o->direction)
                || !isfinite(o->length) || !isfinite(o->inverse_speed) || o->born != d->span.ended
                || w->tick - o->born >= BOAT_OBJECT_LIFE_TICKS)
                return false;
            Vector2 delta = Vector2Subtract(d->span.end, d->span.start);
            float length = Vector2Length(delta);
            if (!same_point(o->position, Vector2Scale(Vector2Add(d->span.start, d->span.end), .5f))
                || !same_point(o->direction, Vector2Scale(delta, 1 / length))
                || o->length != fmaxf(length, 2)
                || o->inverse_speed != (float)(d->span.ended - d->span.started) / (120 * length))
                return false;
            if (d->paired) {
                if (!d->eligible || !span_valid(d->predecessor, w->tick, w->births)
                    || d->predecessor.serial >= d->span.serial
                    || d->predecessor.ended != d->span.started
                    || !same_point(d->predecessor.end, d->span.start)
                    || !witness_valid(c, e, d->identity, d->epoch, d->predecessor)
                    || d->curvature != curvature(d->predecessor, d->span))
                    return false;
            } else if (d->curvature || d->predecessor.serial)
                return false;
            for (unsigned j = 0; j < e * BOAT_OBJECT_PER_BOAT + i; ++j)
                if (c->descriptors[j].span.serial == d->span.serial)
                    return false;
        }
    }
    return live == w->live;
}

static uint64_t checksum(const R2Capture *c)
{
    const unsigned char *bytes = (const unsigned char *)c;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < sizeof(*c); ++i)
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

bool r2_capture_snapshot(const R2Capture *c, R2CaptureSnapshot *snapshot)
{
    if (!r2_capture_valid(c))
        return false;
    memset(snapshot, 0, sizeof(*snapshot));
    memcpy(&snapshot->capture, c, sizeof(*c));
    snapshot->version = 1;
    snapshot->checksum = checksum(&snapshot->capture);
    return true;
}

bool r2_capture_restore(R2Capture *c, const R2CaptureSnapshot *snapshot)
{
    if (snapshot->version != 1 || snapshot->checksum != checksum(&snapshot->capture)
        || !r2_capture_valid(&snapshot->capture))
        return false;
    memcpy(c, &snapshot->capture, sizeof(*c));
    return true;
}
