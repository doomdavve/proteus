#include "waterfront_flock.h"

#include "boat_clock.h"
#include "raymath.h"

#include <math.h>
#include <string.h>

static const char *const cue_names[WATERFRONT_FLOCK_CUE_KINDS] = { "feeding", "alarm" };

const char *waterfront_flock_cue_name(WaterfrontFlockCueKind kind)
{
    return kind < WATERFRONT_FLOCK_CUE_KINDS ? cue_names[kind] : "unknown";
}

static const WaterfrontFlock compiled[WATERFRONT_FLOCKS] = {
    { .id = "marina", .x = -15, .z = 38, .count = 8, .radius = 13, .height = 5,
        .rate = .29f, .follow_actor = -1, .implicit_cues = true },
    { .id = "river", .x = -16, .z = -20, .count = 8, .radius = 13, .height = 7,
        .rate = -.315f, .follow_actor = -1, .implicit_cues = true },
    { .id = "cruiser", .x = 70, .z = -245, .count = 8, .radius = 13, .height = 9,
        .rate = .34f, .follow_actor = 1, .implicit_cues = true },
    { .id = "south", .x = -135, .z = -235, .count = 8, .radius = 13, .height = 11,
        .rate = -.365f, .follow_actor = -1, .implicit_cues = true },
};

static WaterfrontFlockDefinition defaults;
static bool defaults_ready;

const WaterfrontFlockDefinition *waterfront_flock_defaults(void)
{
    if (!defaults_ready) {
        defaults.flock_count = WATERFRONT_FLOCKS;
        for (unsigned i = 0; i < WATERFRONT_FLOCKS; ++i)
            defaults.flocks[i] = compiled[i];
        defaults_ready = true;
    }
    return &defaults;
}

bool waterfront_flock_has_content(const WaterfrontFlockDefinition *flocks)
{
    return flocks && flocks->flock_count > 0;
}

static bool finite_in(float value, float low, float high)
{
    return isfinite(value) && value >= low && value <= high;
}

static bool identifier(const char *text)
{
    if (!(*text >= 'a' && *text <= 'z'))
        return false;
    for (; *text; ++text)
        if (!((*text >= 'a' && *text <= 'z') || (*text >= '0' && *text <= '9') || *text == '_'))
            return false;
    return true;
}

static void fail(WaterfrontFlockDiagnostic *d, const char *field, const char *message)
{
    if (!d)
        return;
    size_t n = 0;
    for (; field[n] && n + 1 < sizeof(d->field); ++n)
        d->field[n] = field[n];
    d->field[n] = 0;
    n = 0;
    for (; message[n] && n + 1 < sizeof(d->message); ++n)
        d->message[n] = message[n];
    d->message[n] = 0;
}

static void indexed_path(
    char *out, size_t capacity, const char *prefix, unsigned index, const char *suffix)
{
    size_t n = 0;
    for (const char *p = prefix; *p && n + 1 < capacity; ++p)
        out[n++] = *p;
    if (n + 1 < capacity)
        out[n++] = '[';
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + index % 10);
        index /= 10;
    } while (index && count < sizeof(digits));
    while (count && n + 1 < capacity)
        out[n++] = digits[--count];
    if (n + 1 < capacity)
        out[n++] = ']';
    for (const char *p = suffix; *p && n + 1 < capacity; ++p)
        out[n++] = *p;
    out[n] = 0;
}

bool waterfront_flock_validate(
    const WaterfrontFlockDefinition *flocks, WaterfrontFlockDiagnostic *diagnostic)
{
    if (diagnostic)
        diagnostic->field[0] = diagnostic->message[0] = 0;
    if (!flocks)
        return true;
    if (flocks->flock_count > WATERFRONT_FLOCKS) {
        fail(diagnostic, "scene.gulls.flocks", "exceeds the four-flock capacity");
        return false;
    }
    unsigned birds = 0;
    for (unsigned i = 0; i < flocks->flock_count; ++i) {
        const WaterfrontFlock *f = &flocks->flocks[i];
        char path[64], at[64];
        indexed_path(path, sizeof(path), "scene.gulls.flocks", i + 1, "");
        indexed_path(at, sizeof(at), "scene.gulls.flocks", i + 1, ".id");
        if (!f->id[0] || !identifier(f->id)) {
            fail(diagnostic, at, "must be a-z, 0-9 or '_', starting with a letter");
            return false;
        }
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(f->id, flocks->flocks[j].id)) {
                fail(diagnostic, at, "repeats an existing flock ID");
                return false;
            }
        if (f->count < 1 || f->count > WATERFRONT_FLOCK_BIRDS) {
            indexed_path(at, sizeof(at), "scene.gulls.flocks", i + 1, ".count");
            fail(diagnostic, at, "must be 1..8 birds");
            return false;
        }
        birds += f->count;
        if (birds > WATERFRONT_FLOCK_MAX_BIRDS) {
            fail(diagnostic, "scene.gulls.flocks", "exceeds the 32-bird capacity");
            return false;
        }
        if (!finite_in(f->x, -WATERFRONT_FLOCK_HALF_EXTENT, WATERFRONT_FLOCK_HALF_EXTENT)
            || !finite_in(f->z, -WATERFRONT_FLOCK_HALF_EXTENT, WATERFRONT_FLOCK_HALF_EXTENT)) {
            indexed_path(at, sizeof(at), "scene.gulls.flocks", i + 1, ".centre");
            fail(diagnostic, at, "must be finite within 20000 m");
            return false;
        }
        if (!finite_in(f->radius, .5f, 200) || !finite_in(f->height, .5f, 200)
            || !finite_in(f->rate, -5, 5) || f->rate == 0) {
            indexed_path(at, sizeof(at), "scene.gulls.flocks", i + 1, ".region");
            fail(diagnostic, at, "has an invalid radius, height or turn rate");
            return false;
        }
        if (f->follow_actor < -1 || f->follow_actor >= WATERFRONT_ROUTE_ACTORS) {
            indexed_path(at, sizeof(at), "scene.gulls.flocks", i + 1, ".follow");
            fail(diagnostic, at, "names an unknown traffic actor");
            return false;
        }
        if (f->cue_count > WATERFRONT_FLOCK_CUES) {
            fail(diagnostic, path, "has more than 16 cues");
            return false;
        }
        for (unsigned c = 0; c < f->cue_count; ++c) {
            const WaterfrontFlockCue *cue = &f->cues[c];
            if (cue->kind >= WATERFRONT_FLOCK_CUE_KINDS || cue->start_tick >= cue->end_tick
                || cue->fade_ticks > cue->end_tick - cue->start_tick
                || !finite_in(cue->amplitude, 0, 1)) {
                indexed_path(at, sizeof(at), "scene.gulls.flocks", i + 1, ".cues");
                fail(diagnostic, at, "has an invalid interval, kind, fade or amplitude");
                return false;
            }
        }
    }
    return true;
}

static uint64_t fold(uint64_t hash, const void *bytes, size_t size)
{
    const unsigned char *p = bytes;
    for (size_t i = 0; i < size; ++i) {
        hash ^= p[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t fold_float(uint64_t hash, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    unsigned char bytes[4] = { (unsigned char)bits, (unsigned char)(bits >> 8),
        (unsigned char)(bits >> 16), (unsigned char)(bits >> 24) };
    return fold(hash, bytes, sizeof(bytes));
}

uint64_t waterfront_flock_digest(const WaterfrontFlockDefinition *flocks)
{
    uint64_t hash = 14695981039346656037ull;
    if (!flocks)
        return hash;
    hash = fold(hash, &flocks->flock_count, sizeof(flocks->flock_count));
    for (unsigned i = 0; i < flocks->flock_count && i < WATERFRONT_FLOCKS; ++i) {
        const WaterfrontFlock *f = &flocks->flocks[i];
        hash = fold(hash, f->id, strlen(f->id) + 1);
        hash = fold_float(hash, f->x);
        hash = fold_float(hash, f->z);
        hash = fold(hash, &f->count, sizeof(f->count));
        hash = fold(hash, &f->seed, sizeof(f->seed));
        hash = fold_float(hash, f->radius);
        hash = fold_float(hash, f->height);
        hash = fold_float(hash, f->rate);
        hash = fold(hash, &f->follow_actor, sizeof(f->follow_actor));
        hash = fold(hash, &f->implicit_cues, 1);
        hash = fold(hash, &f->cue_count, sizeof(f->cue_count));
        for (unsigned c = 0; c < f->cue_count; ++c) {
            const WaterfrontFlockCue *cue = &f->cues[c];
            hash = fold(hash, &cue->start_tick, sizeof(cue->start_tick));
            hash = fold(hash, &cue->end_tick, sizeof(cue->end_tick));
            hash = fold(hash, &cue->fade_ticks, sizeof(cue->fade_ticks));
            hash = fold(hash, &cue->kind, 1);
            hash = fold_float(hash, cue->amplitude);
        }
    }
    return hash;
}

// Smooth 0..1 envelope for one authored cue at TIME seconds; half-open ending.
static float cue_envelope(const WaterfrontFlockCue *cue, double time)
{
    double tick = time * 120.0;
    if (tick < cue->start_tick || tick >= cue->end_tick)
        return 0;
    double fade = cue->fade_ticks ? cue->fade_ticks : 0;
    double edge = 1;
    if (fade > 0) {
        edge = fmin((tick - cue->start_tick) / fade, (cue->end_tick - tick) / fade);
        edge = fmin(fmax(edge, 0), 1);
        edge = edge * edge * (3 - 2 * edge);
    }
    return (float)(cue->amplitude * edge);
}

static void cue_envelopes(const WaterfrontFlock *flock, unsigned flock_index, int bird,
    double time, float *feeding, float *alarm)
{
    if (flock->implicit_cues) {
        double social_time = time - bird * .21f;
        float cue = .5f + .5f * sinf(boat_phase(.31f, social_time) + flock_index * 1.9f);
        *feeding = powf(cue, 6);
        float startle = fmaxf(0, sinf(boat_phase(.47f, social_time) + flock_index * 2.3f));
        *alarm = powf(startle, 8);
        return;
    }
    *feeding = *alarm = 0;
    for (unsigned c = 0; c < flock->cue_count; ++c) {
        float value = cue_envelope(&flock->cues[c], time);
        if (flock->cues[c].kind == WATERFRONT_FLOCK_CUE_ALARM)
            *alarm = fmaxf(*alarm, value);
        else
            *feeding = fmaxf(*feeding, value);
    }
    // Alarm overrides feeding, which overrides undisturbed circling.
    *feeding *= 1 - *alarm;
}

static Vector3 flock_path(const WaterfrontFlockDefinition *flocks, unsigned flock_index, int bird,
    double time, const WaterfrontRouteDefinition *traffic, float *alarm)
{
    const WaterfrontFlock *flock = &flocks->flocks[flock_index];
    float feeding;
    cue_envelopes(flock, flock_index, bird, time, &feeding, alarm);
    int phase_bird = bird + (int)flock->seed;
    double social_time = time - phase_bird * .21f;
    float circling = flock->rate;
    float angle = boat_phase(circling, time) + phase_bird * (2 * PI / 8)
        + .14f * sinf(boat_phase(.61f, social_time) + flock_index)
        + .18f * sinf(boat_phase(.23f, time) + phase_bird * 1.3f);
    float radius = flock->radius + (bird % 3) * 3.2f + 5 * feeding + 7 * *alarm
        + 1.2f * sinf(boat_phase(.37f, time) + phase_bird * 2.1f);
    Vector3 center = { -flock->x, 0, flock->z };
    if (flock->follow_actor >= 0) {
        int actors = waterfront_traffic_actor_count(traffic);
        if (flock->follow_actor < actors) {
            WaterfrontVesselMotion boat
                = waterfront_traffic_motion_routes(traffic, flock->follow_actor, time);
            if (Vector3LengthSqr(boat.velocity) > 1e-6f)
                center = Vector3Subtract(
                    boat.position, Vector3Scale(Vector3Normalize(boat.velocity), 18));
        }
    }
    return (Vector3) { center.x + radius * cosf(angle),
        flock->height + (bird % 3) * 1.1f
            + 1.1f * sinf(boat_phase(.83f, time) + phase_bird * 1.7f) - 2.5f * feeding
            + 6 * *alarm,
        center.z + radius * .75f * sinf(angle) };
}

static Vector3 gull_avoid(Vector3 p, Vector3 source, float radius, float strength, float *alarm)
{
    Vector3 away = { p.x - source.x, 0, p.z - source.z };
    float distance2 = Vector3LengthSqr(away);
    float response = expf(-distance2 / (radius * radius));
    *alarm = fmaxf(*alarm, response * strength);
    p = Vector3Add(p, Vector3Scale(away, response * strength * .4f));
    p.y += response * strength * 7;
    return p;
}

static Vector3 flock_position(const WaterfrontFlockDefinition *flocks, unsigned flock_index,
    int bird, double time, const WaterfrontRouteDefinition *traffic, Vector3 player,
    Vector3 velocity, float *alarm)
{
    Vector3 p = flock_path(flocks, flock_index, bird, time, traffic, alarm);
    const WaterfrontFlock *flock = &flocks->flocks[flock_index];
    // Separation comes from unmodified paths so evaluation order cannot change the result.
    Vector3 separation = { 0 };
    for (unsigned neighbor = 0; neighbor < flock->count; ++neighbor) {
        if ((int)neighbor == bird)
            continue;
        float ignored;
        Vector3 delta = Vector3Subtract(
            p, flock_path(flocks, flock_index, (int)neighbor, time, traffic, &ignored));
        float distance2 = Vector3LengthSqr(delta);
        separation = Vector3Add(separation, Vector3Scale(delta, 2 * expf(-distance2 / 16)));
    }
    p = Vector3Add(p, separation);
    float speed = Vector3Length(velocity);
    p = gull_avoid(p, Vector3Add(player, Vector3Scale(velocity, .8f)), 10 + fminf(speed, 12),
        .45f + fminf(speed * .08f, .9f), alarm);
    int actors = traffic ? (int)traffic->actor_count : WATERFRONT_TRAFFIC_VESSELS;
    for (int vessel = 0; vessel < actors; ++vessel) {
        WaterfrontVesselMotion boat = waterfront_traffic_motion_routes(traffic, vessel, time);
        float radius = traffic
            ? (waterfront_traffic_spec_routes(traffic, vessel).length > 20 ? 23 : 9)
            : vessel == 3 ? 23 : 9;
        p = gull_avoid(p, boat.position, radius, .65f, alarm);
    }
    return p;
}

void waterfront_flock_evaluate(const WaterfrontFlockDefinition *flocks,
    const WaterfrontRouteDefinition *traffic, double time, Vector3 player, Vector3 velocity,
    WaterfrontFlockFrame *frame)
{
    const WaterfrontFlockDefinition *def = flocks ? flocks : waterfront_flock_defaults();
    *frame = (WaterfrontFlockFrame) { 0 };
    int index = 0;
    unsigned count = def->flock_count <= WATERFRONT_FLOCKS ? def->flock_count
                                                           : WATERFRONT_FLOCKS;
    for (unsigned group = 0; group < count; ++group) {
        const WaterfrontFlock *flock = &def->flocks[group];
        unsigned birds = flock->count <= WATERFRONT_FLOCK_BIRDS ? flock->count
                                                               : WATERFRONT_FLOCK_BIRDS;
        for (unsigned bird = 0; bird < birds && index < WATERFRONT_FLOCK_MAX_BIRDS; ++bird) {
            float alarm, ignored;
            Vector3 p = flock_position(
                def, group, (int)bird, time, traffic, player, velocity, &alarm);
            const float dt = .06f;
            Vector3 before = flock_position(def, group, (int)bird, time - dt,
                traffic, Vector3Subtract(player, Vector3Scale(velocity, dt)), velocity, &ignored);
            Vector3 after = flock_position(def, group, (int)bird, time + dt, traffic,
                Vector3Add(player, Vector3Scale(velocity, dt)), velocity, &ignored);
            Vector3 forward = Vector3Normalize(Vector3Subtract(after, before));
            Vector3 incoming = Vector3Normalize(Vector3Subtract(p, before));
            Vector3 outgoing = Vector3Normalize(Vector3Subtract(after, p));
            float yaw = atan2f(-forward.z, forward.x);
            float pitch = asinf(Clamp(forward.y, -.8f, .8f));
            float bank = Clamp(-Vector3CrossProduct(incoming, outgoing).y * 22, -.65f, .65f);
            int phase_bird = (int)bird + (int)flock->seed;
            float beat = boat_phase(BOAT_TWO_PI * (1.7f + (index % 5) * .09f), time);
            float phase = beat + index * 1.37f * 2 * PI;
            float effort = .5f + .5f * sinf(boat_phase(.72f, time) + index * 2.1f);
            effort
                = Clamp((effort - .58f) * 2.4f + alarm * .9f + fmaxf(0, forward.y), 0, 1);
            float flap = .65f * effort * sinf(phase);
            float size = .78f + (index % 5) * .045f;
            for (int part = 0; part < WATERFRONT_FLOCK_PARTS; ++part) {
                float trim = .035f * sinf(boat_phase(1.9f, time) + index);
                float wing
                    = part == 1 ? -.12f - flap + trim : part == 2 ? .12f + flap + trim : 0;
                frame->birds[index][part] = (Transform3D) { .position = p,
                    .rotation = { bank + wing, yaw, pitch }, .scale = { size, size, size } };
            }
            ++index;
        }
    }
    frame->count = index;
}
