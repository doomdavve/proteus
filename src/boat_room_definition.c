#include "boat_room_definition.h"
#include "boat_location.h"

#include <math.h>
#include <string.h>

enum {
    ROOM_MAX_TEXT = BOAT_ROOM_NAME_BYTES,
    ROOM_RACE_RULES = 9,
    ROOM_MATCH_RULES = 9,
    ROOM_COMBAT_FLOATS = 13,
    ROOM_COMBAT_TICKS = 10,
};

typedef struct {
    uint8_t *bytes;
    size_t capacity, size, at;
    bool reading, ok;
} RoomWire;

static uint64_t integer(RoomWire *w, uint64_t value, unsigned count)
{
    uint8_t b[8];
    if (w->reading) {
        if (w->at + count > w->size) {
            w->ok = false;
            return 0;
        }
        memcpy(b, w->bytes + w->at, count);
        w->at += count;
        value = 0;
        for (unsigned i = 0; i < count; ++i)
            value = (value << 8) | b[i];
        return value;
    }
    if (w->at + count > w->capacity) {
        w->ok = false;
        return 0;
    }
    for (unsigned i = 0; i < count; ++i)
        b[count - i - 1] = (uint8_t)(value >> (i * 8));
    memcpy(w->bytes + w->at, b, count);
    w->at += count;
    return value;
}

static float real32(RoomWire *w, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    bits = (uint32_t)integer(w, bits, 4);
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value))
        w->ok = false;
    return value;
}

static double real64(RoomWire *w, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    bits = integer(w, bits, 8);
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value))
        w->ok = false;
    return value;
}

// A bounded printable string; the wire carries a u8 length and no NUL.
static void text(RoomWire *w, char *value, size_t capacity)
{
    size_t length = 0;
    if (!w->reading)
        while (length + 1 < capacity && value[length])
            ++length;
    length = (size_t)integer(w, length, 1);
    if (length >= capacity) {
        w->ok = false;
        return;
    }
    if (w->reading) {
        if (w->at + length > w->size) {
            w->ok = false;
            return;
        }
        memcpy(value, w->bytes + w->at, length);
        w->at += length;
        value[length] = 0;
        for (size_t i = 0; i < length; ++i)
            if ((unsigned char)value[i] < 0x20 || (unsigned char)value[i] > 0x7e)
                w->ok = false;
    } else {
        if (w->at + length > w->capacity) {
            w->ok = false;
            return;
        }
        memcpy(w->bytes + w->at, value, length);
        w->at += length;
    }
}

static void gate_wire(RoomWire *w, BoatRaceGate *gate)
{
    gate->centre.x = real32(w, gate->centre.x);
    gate->centre.y = real32(w, gate->centre.y);
    gate->direction.x = real32(w, gate->direction.x);
    gate->direction.y = real32(w, gate->direction.y);
    gate->half_width = real32(w, gate->half_width);
}

static void course_wire(RoomWire *w, BoatRaceCourse *c)
{
    c->gate_count = (unsigned)integer(w, c->gate_count, 1);
    if (c->gate_count < 1 || c->gate_count > BOAT_RACE_GATES) {
        w->ok = false;
        return;
    }
    gate_wire(w, &c->start);
    for (unsigned i = 0; i < c->gate_count; ++i)
        gate_wire(w, &c->gates[i]);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        c->grid[i].x = real32(w, c->grid[i].x);
        c->grid[i].y = real32(w, c->grid[i].y);
    }
    BoatVesselWake *v = &c->source;
    v->origin.x = real32(w, v->origin.x);
    v->origin.y = real32(w, v->origin.y);
    v->direction.x = real32(w, v->direction.x);
    v->direction.y = real32(w, v->direction.y);
    v->started = real64(w, v->started);
    v->speed = real32(w, v->speed);
    v->height = real32(w, v->height);
    v->duration = real32(w, v->duration);
    v->lifetime = real32(w, v->lifetime);
    v->wavelength = real32(w, v->wavelength);
    v->spread = real32(w, v->spread);
    v->half_width = real32(w, v->half_width);
    BoatStorm *s = &c->storm;
    s->centre.x = real32(w, s->centre.x);
    s->centre.y = real32(w, s->centre.y);
    s->velocity.x = real32(w, s->velocity.x);
    s->velocity.y = real32(w, s->velocity.y);
    s->radius = real32(w, s->radius);
    s->height = real32(w, s->height);
    s->started = real64(w, s->started);
    s->build = real32(w, s->build);
}

static void race_rules_wire(RoomWire *w, BoatRaceRules *r)
{
    r->grid_ticks = integer(w, r->grid_ticks, 4);
    r->countdown_ticks = integer(w, r->countdown_ticks, 4);
    r->results_ticks = integer(w, r->results_ticks, 4);
    r->after_winner_ticks = integer(w, r->after_winner_ticks, 4);
    r->cap_ticks = integer(w, r->cap_ticks, 4);
    r->recovery_ticks = integer(w, r->recovery_ticks, 4);
    r->skip_ticks = integer(w, r->skip_ticks, 4);
    r->park_ticks = integer(w, r->park_ticks, 4);
    r->min_players = (unsigned)integer(w, r->min_players, 1);
}

static void match_rules_wire(RoomWire *w, BoatMatchRules *r)
{
    r->round_ticks = integer(w, r->round_ticks, 4);
    r->intermission_ticks = integer(w, r->intermission_ticks, 4);
    r->credit_ticks = integer(w, r->credit_ticks, 4);
    r->grace_ticks = integer(w, r->grace_ticks, 4);
    r->respawn_ticks = integer(w, r->respawn_ticks, 4);
    r->skip_ticks = integer(w, r->skip_ticks, 4);
    r->contact_impulse = real32(w, r->contact_impulse);
    r->wake_height = real32(w, r->wake_height);
    r->min_players = (unsigned)integer(w, r->min_players, 1);
}

static void combat_rules_wire(RoomWire *w, BoatCombatRules *r)
{
    r->health = real32(w, r->health);
    r->damage = real32(w, r->damage);
    r->bullet_speed = real32(w, r->bullet_speed);
    r->bullet_range = real32(w, r->bullet_range);
    r->grenade_distance = real32(w, r->grenade_distance);
    r->wave_height = real32(w, r->wave_height);
    r->wave_speed = real32(w, r->wave_speed);
    r->wave_range = real32(w, r->wave_range);
    r->cannon_pressure = real32(w, r->cannon_pressure);
    r->pressure_recovery = real32(w, r->pressure_recovery);
    r->torpedo_damage = real32(w, r->torpedo_damage);
    r->torpedo_speed = real32(w, r->torpedo_speed);
    r->torpedo_range = real32(w, r->torpedo_range);
    r->torpedo_ticks = (uint32_t)integer(w, r->torpedo_ticks, 4);
    r->arming_ticks = (uint32_t)integer(w, r->arming_ticks, 4);
    r->torpedo_ammo = (uint32_t)integer(w, r->torpedo_ammo, 4);
    r->fire_ticks = (uint32_t)integer(w, r->fire_ticks, 4);
    r->heat_ticks = (uint32_t)integer(w, r->heat_ticks, 4);
    r->cool_ticks = (uint32_t)integer(w, r->cool_ticks, 4);
    r->grenade_ticks = (uint32_t)integer(w, r->grenade_ticks, 4);
    r->splash_ticks = (uint32_t)integer(w, r->splash_ticks, 4);
    r->fuse_ticks = (uint32_t)integer(w, r->fuse_ticks, 4);
    r->grace_ticks = (uint32_t)integer(w, r->grace_ticks, 4);
}

static void route_wire(RoomWire *w, WaterfrontRouteDefinition *routes)
{
    routes->actor_count = (unsigned)integer(w, routes->actor_count, 1);
    if (routes->actor_count > WATERFRONT_ROUTE_ACTORS) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < routes->actor_count; ++i) {
        WaterfrontRouteActor *a = &routes->actors[i];
        text(w, a->id, sizeof(a->id));
        a->shape = (unsigned char)integer(w, a->shape, 1);
        a->recipe = (unsigned char)integer(w, a->recipe, 1);
        a->retain = (unsigned char)integer(w, a->retain, 1);
        uint8_t bridge = (uint8_t)integer(w, a->bridge ? 1u : 0u, 1);
        if (bridge > 1 || a->shape >= WATERFRONT_ROUTE_SHAPES
            || a->recipe >= WATERFRONT_CRAFT_RECIPES)
            w->ok = false;
        a->bridge = bridge != 0;
        a->speed = real32(w, a->speed);
        a->accel = real32(w, a->accel);
        a->phase = real32(w, a->phase);
        a->period = real64(w, a->period);
        a->point_count = (unsigned)integer(w, a->point_count, 1);
        a->stop_count = (unsigned)integer(w, a->stop_count, 1);
        if (a->point_count > WATERFRONT_ROUTE_POINT_CAPACITY
            || a->stop_count > WATERFRONT_ROUTE_STOPS) {
            w->ok = false;
            return;
        }
        for (unsigned p = 0; p < a->point_count; ++p) {
            a->points[p].x = real32(w, a->points[p].x);
            a->points[p].z = real32(w, a->points[p].z);
            a->points[p].radius = real32(w, a->points[p].radius);
        }
        for (unsigned s = 0; s < a->stop_count; ++s) {
            text(w, a->stops[s].id, sizeof(a->stops[s].id));
            a->stops[s].segment = (unsigned char)integer(w, a->stops[s].segment, 1);
            a->stops[s].dwell = real32(w, a->stops[s].dwell);
        }
        a->origin_x = real32(w, a->origin_x);
        a->origin_z = real32(w, a->origin_z);
        a->long_radius = real32(w, a->long_radius);
        a->short_radius = real32(w, a->short_radius);
        a->start_distance = real32(w, a->start_distance);
        a->turn_radius = real32(w, a->turn_radius);
    }
}

static void flock_wire(RoomWire *w, WaterfrontFlockDefinition *flocks)
{
    flocks->flock_count = (unsigned)integer(w, flocks->flock_count, 1);
    if (flocks->flock_count > WATERFRONT_FLOCKS) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < flocks->flock_count; ++i) {
        WaterfrontFlock *f = &flocks->flocks[i];
        text(w, f->id, sizeof(f->id));
        f->recipe = (unsigned char)integer(w, f->recipe, 1);
        if (f->recipe >= WATERFRONT_FLOCK_RECIPES)
            w->ok = false;
        f->x = real32(w, f->x);
        f->z = real32(w, f->z);
        f->count = (unsigned)integer(w, f->count, 1);
        f->seed = (unsigned)integer(w, f->seed, 4);
        f->phase = real32(w, f->phase);
        f->radius = real32(w, f->radius);
        f->height = real32(w, f->height);
        f->rate = real32(w, f->rate);
        uint8_t follow
            = (uint8_t)integer(w, f->follow_actor < 0 ? 0xffu : (unsigned)f->follow_actor, 1);
        f->follow_actor = follow == 0xff ? -1 : follow;
        if (f->follow_actor >= WATERFRONT_ROUTE_ACTORS)
            w->ok = false;
        uint8_t implicit = (uint8_t)integer(w, f->implicit_cues ? 1u : 0u, 1);
        if (implicit > 1)
            w->ok = false;
        f->implicit_cues = implicit != 0;
        f->cue_count = (unsigned)integer(w, f->cue_count, 1);
        if (f->cue_count > WATERFRONT_FLOCK_CUES) {
            w->ok = false;
            return;
        }
        for (unsigned c = 0; c < f->cue_count; ++c) {
            WaterfrontFlockCue *cue = &f->cues[c];
            cue->start_tick = (unsigned)integer(w, cue->start_tick, 4);
            cue->end_tick = (unsigned)integer(w, cue->end_tick, 4);
            cue->fade_ticks = (unsigned)integer(w, cue->fade_ticks, 4);
            cue->kind = (unsigned char)integer(w, cue->kind, 1);
            if (cue->kind >= WATERFRONT_FLOCK_CUE_KINDS)
                w->ok = false;
            cue->amplitude = real32(w, cue->amplitude);
        }
    }
}

static void scene_wire(RoomWire *w, BoatSceneDefinition *s)
{
    s->version = (unsigned)integer(w, s->version, 1);
    unsigned flags = (unsigned)integer(w,
        (s->entrance_present ? 1u : 0u) | (s->berths_present ? 2u : 0u)
            | (s->modules_present ? 4u : 0u) | (s->traffic_present ? 8u : 0u)
            | (s->gulls_present ? 16u : 0u),
        1);
    if (flags > 31 || s->version != BOAT_SCENE_VERSION) {
        w->ok = false;
        return;
    }
    s->entrance_present = flags & 1u;
    s->berths_present = flags & 2u;
    s->modules_present = flags & 4u;
    s->traffic_present = flags & 8u;
    s->gulls_present = flags & 16u;
    if (s->entrance_present) {
        s->entrance_x = real32(w, s->entrance_x);
        s->entrance_z = real32(w, s->entrance_z);
        s->entrance_width = real32(w, s->entrance_width);
    }
    s->berth_count = (unsigned)integer(w, s->berth_count, 1);
    s->module_count = (unsigned)integer(w, s->module_count, 1);
    if (s->berth_count > BOAT_SCENE_BERTHS || s->module_count > BOAT_SCENE_MODULES) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < s->berth_count; ++i) {
        BoatSceneBerth *b = &s->berths[i];
        text(w, b->id, sizeof(b->id));
        b->recipe = (unsigned char)integer(w, b->recipe, 1);
        b->mooring = (unsigned char)integer(w, b->mooring, 1);
        if (b->recipe >= BOAT_SCENE_BERTH_RECIPES || b->mooring > BOAT_SCENE_MOORING_PONTOON)
            w->ok = false;
        b->x = real32(w, b->x);
        b->z = real32(w, b->z);
        b->heading = real32(w, b->heading);
    }
    for (unsigned i = 0; i < s->module_count; ++i) {
        BoatSceneModule *m = &s->modules[i];
        text(w, m->id, sizeof(m->id));
        m->kind = (unsigned char)integer(w, m->kind, 1);
        if (m->kind >= BOAT_SCENE_MODULE_KINDS)
            w->ok = false;
        m->x = real32(w, m->x);
        m->z = real32(w, m->z);
        m->heading = real32(w, m->heading);
        m->length = real32(w, m->length);
        m->width = real32(w, m->width);
        m->deck_height = real32(w, m->deck_height);
        m->anchor_x = real32(w, m->anchor_x);
        m->anchor_z = real32(w, m->anchor_z);
        text(w, m->attaches, sizeof(m->attaches));
    }
    if (s->traffic_present)
        route_wire(w, &s->traffic);
    if (s->gulls_present)
        flock_wire(w, &s->gulls);
}

static void definition_wire(RoomWire *w, BoatRoomDefinition *r)
{
    r->version = (unsigned)integer(w, r->version, 1);
    r->mode = (unsigned char)integer(w, r->mode, 1);
    r->location = (unsigned char)integer(w, r->location, 1);
    unsigned flags = (unsigned)integer(w,
        ((r->mode == BOAT_ROOM_MODE_RACE || r->mode == BOAT_ROOM_MODE_DEATHMATCH) ? 1u : 0u)
            | (r->has_scene ? 2u : 0u),
        1);
    if (flags > 3) {
        w->ok = false;
        return;
    }
    bool has_rules = flags & 1u;
    r->has_scene = flags & 2u;
    if (r->version != BOAT_ROOM_VERSION || r->mode >= BOAT_ROOM_MODE_LIMIT
        || r->location >= BOAT_LOCATION_COUNT)
        w->ok = false;
    text(w, r->id, sizeof(r->id));
    text(w, r->name, sizeof(r->name));
    if (has_rules && r->mode == BOAT_ROOM_MODE_RACE) {
        course_wire(w, &r->course);
        race_rules_wire(w, &r->race_rules);
    } else if (has_rules) {
        match_rules_wire(w, &r->match_rules);
        combat_rules_wire(w, &r->combat_rules);
    } else {
        r->course = (BoatRaceCourse) { 0 };
        r->race_rules = (BoatRaceRules) { 0 };
        r->match_rules = (BoatMatchRules) { 0 };
        r->combat_rules = (BoatCombatRules) { 0 };
    }
    if (r->has_scene)
        scene_wire(w, &r->scene);
}

size_t boat_room_definition_encode(const BoatRoomDefinition *room, uint8_t *out, size_t capacity)
{
    RoomWire w = { .bytes = out, .capacity = capacity, .reading = false, .ok = true };
    BoatRoomDefinition copy = *room;
    definition_wire(&w, &copy);
    if (!w.ok || w.at > BOAT_ROOM_BYTES_MAX)
        return 0;
    return w.at;
}

static void copy_text(char *out, size_t capacity, const char *text)
{
    size_t n = 0;
    while (text[n] && n + 1 < capacity) {
        out[n] = text[n];
        ++n;
    }
    out[n] = 0;
}

static void fail(BoatRoomDiagnostic *d, const char *field, const char *message)
{
    if (!d)
        return;
    copy_text(d->field, sizeof(d->field), field);
    copy_text(d->message, sizeof(d->message), message);
}

// "prefix[index]" into OUT, without stdio: the core library has no file access.
static void indexed(char *out, size_t capacity, const char *prefix, unsigned index)
{
    size_t n = 0;
    while (prefix[n] && n + 1 < capacity) {
        out[n] = prefix[n];
        ++n;
    }
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
    out[n] = 0;
}

bool boat_room_definition_decode(
    const uint8_t *bytes, size_t size, BoatRoomDefinition *room, BoatRoomDiagnostic *diagnostic)
{
    if (!bytes || size > BOAT_ROOM_BYTES_MAX) {
        fail(diagnostic, "", "exceeds the 16 KiB room definition limit");
        return false;
    }
    RoomWire w = { .bytes = (uint8_t *)bytes, .size = size, .reading = true, .ok = true };
    static BoatRoomDefinition decoded;
    decoded = (BoatRoomDefinition) { 0 };
    definition_wire(&w, &decoded);
    if (!w.ok || w.at != w.size) {
        fail(diagnostic, "", w.ok ? "has trailing bytes" : "is malformed or truncated");
        return false;
    }
    if (decoded.has_scene) {
        BoatSceneDiagnostic scene;
        if (!boat_scene_validate(&decoded.scene, &scene)) {
            fail(diagnostic, scene.field, scene.message);
            return false;
        }
    }
    BoatRoomDiagnostic check;
    if (!boat_room_definition_validate(&decoded, &check)) {
        if (diagnostic)
            *diagnostic = check;
        return false;
    }
    *room = decoded;
    return true;
}

uint64_t boat_room_definition_digest(const BoatRoomDefinition *room)
{
    static uint8_t bytes[BOAT_ROOM_BYTES_MAX];
    size_t size = boat_room_definition_encode(room, bytes, sizeof(bytes));
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static bool finite_in(float value, float low, float high)
{
    return isfinite(value) && value >= low && value <= high;
}

static bool race_rules_valid(const BoatRaceRules *r)
{
    return r->countdown_ticks > 0 && r->countdown_ticks <= UINT32_MAX && r->cap_ticks > 0
        && r->cap_ticks <= UINT32_MAX && r->grid_ticks <= UINT32_MAX
        && r->results_ticks <= UINT32_MAX && r->after_winner_ticks <= UINT32_MAX
        && r->recovery_ticks <= UINT32_MAX && r->skip_ticks <= r->recovery_ticks
        && r->park_ticks <= UINT32_MAX && r->min_players >= 1 && r->min_players <= BOAT_FLEET_SLOTS;
}

static bool match_rules_valid(const BoatMatchRules *r)
{
    return r->round_ticks > 0 && r->round_ticks <= UINT32_MAX && r->intermission_ticks <= UINT32_MAX
        && r->credit_ticks > 0 && r->credit_ticks <= UINT32_MAX && r->grace_ticks <= UINT32_MAX
        && r->respawn_ticks <= UINT32_MAX && r->skip_ticks <= r->respawn_ticks
        && isfinite(r->contact_impulse) && r->contact_impulse > 0 && r->contact_impulse <= 1e5f
        && isfinite(r->wake_height) && r->wake_height > 0 && r->wake_height <= 100
        && r->min_players >= 1 && r->min_players <= BOAT_FLEET_SLOTS;
}

static bool combat_rules_valid(const BoatCombatRules *r)
{
    const float values[ROOM_COMBAT_FLOATS]
        = { r->health, r->damage, r->bullet_speed, r->bullet_range, r->grenade_distance,
              r->wave_height, r->wave_speed, r->wave_range, r->cannon_pressure,
              r->pressure_recovery, r->torpedo_damage, r->torpedo_speed, r->torpedo_range };
    const float low[ROOM_COMBAT_FLOATS] = { 1, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1 };
    const float high[ROOM_COMBAT_FLOATS]
        = { 1e5f, 1e5f, 1e4f, 1e4f, 1e4f, 100, 1e4f, 1e4f, 100, 100, 1e5f, 1e4f, 1e4f };
    for (unsigned i = 0; i < ROOM_COMBAT_FLOATS; ++i)
        if (!finite_in(values[i], low[i], high[i]))
            return false;
    const uint32_t ticks[ROOM_COMBAT_TICKS]
        = { r->torpedo_ticks, r->arming_ticks, r->torpedo_ammo, r->fire_ticks, r->heat_ticks,
              r->cool_ticks, r->grenade_ticks, r->splash_ticks, r->fuse_ticks, r->grace_ticks };
    for (unsigned i = 0; i < ROOM_COMBAT_TICKS; ++i)
        if (ticks[i] > 432000)
            return false;
    return r->torpedo_ammo > 0;
}

bool boat_race_course_validate(
    const BoatRaceCourse *c, const BoatRaceRules *rules, BoatRoomDiagnostic *diagnostic)
{
    if (!c || !rules || c->gate_count < 1 || c->gate_count > BOAT_RACE_GATES) {
        fail(diagnostic, "course.gates", "must contain 1 to 8 gates");
        return false;
    }
    for (unsigned i = 0; i <= c->gate_count; ++i) {
        const BoatRaceGate *g = i ? &c->gates[i - 1] : &c->start;
        char path[32];
        if (i)
            indexed(path, sizeof(path), "course.gates", i);
        else
            copy_text(path, sizeof(path), "course.start");
        if (!finite_in(g->centre.x, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT)
            || !finite_in(g->centre.y, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT)) {
            fail(diagnostic, path, "must be finite within 20000 m");
            return false;
        }
        float length = hypotf(g->direction.x, g->direction.y);
        if (!isfinite(length) || fabsf(length - 1) > .01f) {
            fail(diagnostic, path, "direction must have unit length");
            return false;
        }
        if (!finite_in(g->half_width, 1, 500)) {
            fail(diagnostic, path, "half_width must be 1..500 m");
            return false;
        }
        if (!i)
            continue;
        const BoatRaceGate *from = i == 1 ? &c->start : &c->gates[i - 2];
        float distance = hypotf(g->centre.x - from->centre.x, g->centre.y - from->centre.y);
        float ahead = (g->centre.x - from->centre.x) * from->direction.x
            + (g->centre.y - from->centre.y) * from->direction.y;
        float onward = (g->centre.x - from->centre.x) * g->direction.x
            + (g->centre.y - from->centre.y) * g->direction.y;
        if (distance < 20 || distance > 5000 || ahead < 10 || onward < 10) {
            fail(diagnostic, path, "must lie 20..5000 m ahead of the previous line");
            return false;
        }
    }
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        char path[32];
        indexed(path, sizeof(path), "course.grid", i + 1);
        float dx = c->grid[i].x - c->start.centre.x;
        float dz = c->grid[i].y - c->start.centre.y;
        if (!finite_in(c->grid[i].x, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT)
            || !finite_in(c->grid[i].y, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT)) {
            fail(diagnostic, path, "must be finite within 20000 m");
            return false;
        }
        if (dx * c->start.direction.x + dz * c->start.direction.y > -10 || hypotf(dx, dz) > 150) {
            fail(diagnostic, path, "must be safe water behind the start");
            return false;
        }
        for (unsigned j = 0; j < i; ++j)
            if (hypotf(c->grid[i].x - c->grid[j].x, c->grid[i].y - c->grid[j].y) < 13) {
                fail(diagnostic, path, "grid hulls need 13 m separation");
                return false;
            }
    }
    const BoatVesselWake *v = &c->source;
    if (v->height > 0 && !boat_vessel_wake_valid(*v)) {
        fail(diagnostic, "course.source", "is not a valid ferry wake recipe");
        return false;
    }
    if (!finite_in(c->storm.centre.x, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT)
        || !finite_in(c->storm.centre.y, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT)
        || !finite_in(c->storm.velocity.x, -50, 50) || !finite_in(c->storm.velocity.y, -50, 50)
        || !finite_in(c->storm.radius, 0, 5000) || !finite_in(c->storm.height, 0, 5)
        || !isfinite(c->storm.started) || !finite_in(c->storm.build, 0, 600)) {
        fail(diagnostic, "course.storm", "is outside the weather envelope");
        return false;
    }
    if (!race_rules_valid(rules)) {
        fail(diagnostic, "course.rules", "has an invalid round, recovery or player count");
        return false;
    }
    return true;
}

bool boat_room_definition_validate(const BoatRoomDefinition *r, BoatRoomDiagnostic *diagnostic)
{
    if (!r) {
        fail(diagnostic, "", "is missing");
        return false;
    }
    if (r->version != BOAT_ROOM_VERSION) {
        fail(diagnostic, "version", "must be 1");
        return false;
    }
    if (r->mode >= BOAT_ROOM_MODE_LIMIT) {
        fail(diagnostic, "mode", "must be free, deathmatch or race");
        return false;
    }
    if (r->location >= BOAT_LOCATION_COUNT) {
        fail(diagnostic, "location", "is not a compiled location");
        return false;
    }
    if (!r->id[0] || !r->name[0]) {
        fail(diagnostic, "id", "needs a package id and display name");
        return false;
    }
    if (r->mode == BOAT_ROOM_MODE_RACE) {
        if (r->location != BOAT_LOCATION_RIVER) {
            fail(diagnostic, "location", "a race room requires the river location");
            return false;
        }
        if (!boat_race_course_validate(&r->course, &r->race_rules, diagnostic))
            return false;
    } else if (r->mode == BOAT_ROOM_MODE_DEATHMATCH) {
        if (!match_rules_valid(&r->match_rules)) {
            fail(diagnostic, "match.rules", "has an invalid round, credit or spawn timing");
            return false;
        }
        if (!combat_rules_valid(&r->combat_rules)) {
            fail(diagnostic, "match.combat", "has a value outside the combat envelope");
            return false;
        }
    }
    if (r->has_scene) {
        BoatSceneDiagnostic scene;
        if (!boat_scene_validate(&r->scene, &scene)) {
            fail(diagnostic, scene.field, scene.message);
            return false;
        }
    }
    return true;
}

const char *boat_room_mode_name(unsigned mode)
{
    switch (mode) {
    case BOAT_ROOM_MODE_FREE:
        return "free";
    case BOAT_ROOM_MODE_DEATHMATCH:
        return "deathmatch";
    case BOAT_ROOM_MODE_RACE:
        return "race";
    default:
        return "unknown";
    }
}
