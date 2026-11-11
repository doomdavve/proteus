#include "boat_marks.h"

#include <math.h>
#include <string.h>

// Without stdio: the core library has no file access.
static void copy_text(char *out, size_t capacity, const char *text)
{
    size_t n = 0;
    while (text[n] && n + 1 < capacity) {
        out[n] = text[n];
        ++n;
    }
    out[n] = 0;
}

static bool fail(BoatMarkDiagnostic *d, const char *field, const char *message)
{
    if (d) {
        copy_text(d->field, sizeof(d->field), field);
        copy_text(d->message, sizeof(d->message), message);
    }
    return false;
}

// "marks[index]" for a one-based INDEX below 100.
static void mark_field(char out[16], unsigned index)
{
    copy_text(out, 16, "marks[");
    size_t n = strlen(out);
    if (index >= 10)
        out[n++] = (char)('0' + index / 10 % 10);
    out[n++] = (char)('0' + index % 10);
    out[n++] = ']';
    out[n] = 0;
}

static size_t text_length(const char *text)
{
    size_t n = 0;
    while (n < BOAT_MARK_TEXT && text[n])
        ++n;
    return n;
}

static bool terminated(const char *text) { return memchr(text, 0, BOAT_MARK_TEXT) != NULL; }

static bool valid_id(const char *id)
{
    if (!terminated(id) || !id[0])
        return false;
    for (const char *c = id; *c; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_'))
            return false;
    return true;
}

static bool valid_name(const char *name)
{
    if (!terminated(name) || !name[0])
        return false;
    size_t n = strlen(name);
    if (name[0] == ' ' || name[n - 1] == ' ')
        return false;
    for (size_t i = 0; i < n; ++i)
        if (name[i] < 0x20 || name[i] > 0x7e)
            return false;
    return true;
}

bool boat_marks_validate(const BoatMarkSet *marks, BoatMarkDiagnostic *d)
{
    if (!marks || marks->count < 1 || marks->count > BOAT_MARKS_MAX)
        return fail(d, "marks", "must list 1 to 16 marks");
    for (unsigned i = 0; i < marks->count; ++i) {
        const BoatMark *m = &marks->items[i];
        char at[16];
        mark_field(at, i + 1);
        if (!valid_id(m->id))
            return fail(d, at, "id must be 1 to 23 of a-z, 0-9 and _");
        if (!valid_name(m->name))
            return fail(
                d, at, "name must be 1 to 23 printable ASCII characters without edge spaces");
        if (!(fabsf(m->x) <= BOAT_MARK_COORDINATE_MAX && fabsf(m->z) <= BOAT_MARK_COORDINATE_MAX))
            return fail(d, at, "x and z must be finite within 20000 m");
        if (!(m->radius >= BOAT_MARK_RADIUS_MIN && m->radius <= BOAT_MARK_RADIUS_MAX))
            return fail(d, at, "radius must be 10 to 200 m");
        for (unsigned j = 0; j < i; ++j) {
            const BoatMark *o = &marks->items[j];
            if (!strcmp(o->id, m->id))
                return fail(d, at, "repeats the id of an earlier mark");
            float dx = m->x - o->x, dz = m->z - o->z, reach = m->radius + o->radius;
            if (dx * dx + dz * dz < reach * reach)
                return fail(d, at, "overlaps an earlier mark");
        }
    }
    return true;
}

bool boat_marks_check_map(
    const BoatMarkSet *marks, const BoatMap *map, Vector2 half_extent, BoatMarkDiagnostic *d)
{
    for (unsigned i = 0; i < marks->count; ++i) {
        const BoatMark *m = &marks->items[i];
        char at[16];
        mark_field(at, i + 1);
        if (!map) {
            if (!(fabsf(m->x) + m->radius < half_extent.x
                    && fabsf(m->z) + m->radius < half_extent.y))
                return fail(d, at, "must lie inside the basin walls");
        } else if (!boat_map_water(map, (Vector2) { m->x, m->z }, BOAT_MARK_WATER_CLEARANCE)) {
            return fail(d, at, "centre must be open water with 4 m clearance");
        }
    }
    return true;
}

static void fold(uint64_t *hash, const void *bytes, size_t size)
{
    const unsigned char *p = bytes;
    for (size_t i = 0; i < size; ++i) {
        *hash ^= p[i];
        *hash *= 1099511628211ull;
    }
}

static void fold_float(uint64_t *hash, float number)
{
    uint32_t bits;
    memcpy(&bits, &number, sizeof(bits));
    unsigned char bytes[4] = { (unsigned char)bits, (unsigned char)(bits >> 8),
        (unsigned char)(bits >> 16), (unsigned char)(bits >> 24) };
    fold(hash, bytes, sizeof(bytes));
}

uint64_t boat_marks_digest(const BoatMarkSet *marks)
{
    uint64_t hash = 14695981039346656037ull;
    fold(&hash, "marks", 6);
    unsigned char count = (unsigned char)marks->count;
    fold(&hash, &count, 1);
    for (unsigned i = 0; i < marks->count && i < BOAT_MARKS_MAX; ++i) {
        const BoatMark *m = &marks->items[i];
        fold(&hash, m->id, text_length(m->id) + 1);
        fold(&hash, m->name, text_length(m->name) + 1);
        fold_float(&hash, m->x);
        fold_float(&hash, m->z);
        fold_float(&hash, m->radius);
    }
    return hash;
}

void boat_marks_tracker_reset(BoatMarkTracker *tracker)
{
    memset(tracker->life, 0, sizeof(tracker->life));
    memset(tracker->inside, BOAT_MARK_NONE, sizeof(tracker->inside));
}

static unsigned char mark_at(const BoatMarkSet *marks, Vector2 position)
{
    for (unsigned i = 0; i < marks->count; ++i) {
        const BoatMark *m = &marks->items[i];
        float dx = position.x - m->x, dz = position.y - m->z;
        if (dx * dx + dz * dz < m->radius * m->radius)
            return (unsigned char)i;
    }
    return BOAT_MARK_NONE;
}

unsigned boat_marks_observe(const BoatMarkSet *marks, BoatMarkTracker *tracker,
    const BoatFleetState *fleet, BoatMarkEntry out[BOAT_FLEET_SLOTS])
{
    unsigned n = 0;
    for (unsigned slot = 0; slot < BOAT_FLEET_SLOTS; ++slot) {
        const BoatFleetSlot *s = &fleet->slots[slot];
        if (s->life != tracker->life[slot]) {
            tracker->life[slot] = s->life;
            tracker->inside[slot] = BOAT_MARK_NONE;
        }
        unsigned char now = s->active ? mark_at(marks, s->boat.position) : BOAT_MARK_NONE;
        if (now != BOAT_MARK_NONE && now != tracker->inside[slot])
            out[n++] = (BoatMarkEntry) { .mark = now, .slot = (unsigned char)slot };
        tracker->inside[slot] = now;
    }
    return n;
}
