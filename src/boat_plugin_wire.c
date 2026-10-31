#include "boat_plugin_wire.h"

#include "boat_plugin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// A bounded byte writer/reader. Nothing here trusts the reader's input.
typedef struct {
    unsigned char *bytes;
    size_t capacity, size, at;
    bool reading, ok;
} Wire;

static uint64_t integer(Wire *w, uint64_t v, unsigned n)
{
    unsigned char b[8];
    if (w->reading) {
        if (w->at + n > w->size) {
            w->ok = false;
            return 0;
        }
        memcpy(b, w->bytes + w->at, n);
        w->at += n;
        v = 0;
        for (unsigned i = 0; i < n; ++i)
            v = (v << 8) | b[i];
        return v;
    }
    if (w->bytes) {
        if (w->at + n > w->capacity) {
            w->ok = false;
            return 0;
        }
        for (unsigned i = 0; i < n; ++i)
            b[n - i - 1] = (unsigned char)(v >> (i * 8));
        memcpy(w->bytes + w->at, b, n);
    }
    w->at += n;
    return v;
}

static double number(Wire *w, double v)
{
    uint64_t bits;
    _Static_assert(sizeof(v) == sizeof(bits), "wire requires binary64 doubles");
    memcpy(&bits, &v, sizeof(bits));
    bits = integer(w, bits, 8);
    memcpy(&v, &bits, sizeof(v));
    if (!isfinite(v))
        w->ok = false;
    return v;
}

static void string(Wire *w, char *s, size_t capacity)
{
    size_t n = (size_t)integer(w, w->reading ? 0 : strlen(s), 4);
    if (n >= capacity) {
        w->ok = false;
        return;
    }
    if (w->reading) {
        if (w->at + n > w->size || memchr(w->bytes + w->at, 0, n)) {
            w->ok = false;
            return;
        }
        memcpy(s, w->bytes + w->at, n);
        w->at += n;
        s[n] = 0;
    } else if (w->bytes) {
        if (w->at + n > w->capacity) {
            w->ok = false;
            return;
        }
        memcpy(w->bytes + w->at, s, n);
        w->at += n;
    } else
        w->at += n;
}

static void traffic_wire(Wire *w, WaterfrontRouteDefinition *routes)
{
    routes->actor_count = (unsigned)integer(w, routes->actor_count, 4);
    if (routes->actor_count > WATERFRONT_ROUTE_ACTORS) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < routes->actor_count; ++i) {
        WaterfrontRouteActor *a = &routes->actors[i];
        string(w, a->id, sizeof(a->id));
        a->shape = (unsigned char)integer(w, a->shape, 1);
        a->recipe = (unsigned char)integer(w, a->recipe, 1);
        a->retain = (unsigned char)integer(w, a->retain, 1);
        if (a->retain != 0xff && a->retain > WATERFRONT_ROUTE_ACTORS)
            w->ok = false;
        unsigned bridge = (unsigned)integer(w, a->bridge ? 1u : 0u, 1);
        if (bridge > 1 || a->shape >= WATERFRONT_ROUTE_SHAPES
            || a->recipe >= WATERFRONT_CRAFT_RECIPES)
            w->ok = false;
        a->bridge = bridge != 0;
        a->speed = (float)number(w, a->speed);
        a->accel = (float)number(w, a->accel);
        a->phase = (float)number(w, a->phase);
        a->period = number(w, a->period);
        a->point_count = (unsigned)integer(w, a->point_count, 4);
        a->stop_count = (unsigned)integer(w, a->stop_count, 4);
        if (a->point_count > WATERFRONT_ROUTE_POINT_CAPACITY
            || a->stop_count > WATERFRONT_ROUTE_STOPS) {
            w->ok = false;
            return;
        }
        for (unsigned p = 0; p < a->point_count; ++p) {
            a->points[p].x = (float)number(w, a->points[p].x);
            a->points[p].z = (float)number(w, a->points[p].z);
            a->points[p].radius = (float)number(w, a->points[p].radius);
        }
        for (unsigned k = 0; k < a->stop_count; ++k) {
            string(w, a->stops[k].id, sizeof(a->stops[k].id));
            a->stops[k].segment = (unsigned char)integer(w, a->stops[k].segment, 1);
            a->stops[k].dwell = (float)number(w, a->stops[k].dwell);
        }
        a->origin_x = (float)number(w, a->origin_x);
        a->origin_z = (float)number(w, a->origin_z);
        a->long_radius = (float)number(w, a->long_radius);
        a->short_radius = (float)number(w, a->short_radius);
        a->start_distance = (float)number(w, a->start_distance);
        a->turn_radius = (float)number(w, a->turn_radius);
    }
}

static void flock_wire(Wire *w, WaterfrontFlockDefinition *flocks)
{
    flocks->flock_count = (unsigned)integer(w, flocks->flock_count, 4);
    if (flocks->flock_count > WATERFRONT_FLOCKS) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < flocks->flock_count; ++i) {
        WaterfrontFlock *f = &flocks->flocks[i];
        string(w, f->id, sizeof(f->id));
        f->recipe = (unsigned char)integer(w, f->recipe, 1);
        if (f->recipe >= WATERFRONT_FLOCK_RECIPES)
            w->ok = false;
        f->x = (float)number(w, f->x);
        f->z = (float)number(w, f->z);
        f->count = (unsigned)integer(w, f->count, 4);
        f->seed = (unsigned)integer(w, f->seed, 4);
        f->phase = (float)number(w, f->phase);
        f->radius = (float)number(w, f->radius);
        f->height = (float)number(w, f->height);
        f->rate = (float)number(w, f->rate);
        unsigned follow = (unsigned)integer(w, (unsigned)(f->follow_actor + 1), 1);
        if (follow > WATERFRONT_ROUTE_ACTORS)
            w->ok = false;
        else
            f->follow_actor = (int)follow - 1;
        if (f->count > WATERFRONT_FLOCK_BIRDS)
            w->ok = false;
        unsigned implicit = (unsigned)integer(w, f->implicit_cues ? 1u : 0u, 1);
        if (implicit > 1)
            w->ok = false;
        f->implicit_cues = implicit != 0;
        f->cue_count = (unsigned)integer(w, f->cue_count, 4);
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
            cue->amplitude = (float)number(w, cue->amplitude);
        }
    }
}

static void scene_wire(Wire *w, BoatSceneDefinition *s)
{
    s->version = (unsigned)integer(w, s->version, 4);
    unsigned flags = (unsigned)integer(w,
        (s->entrance_present ? 1u : 0u) | (s->berths_present ? 2u : 0u)
            | (s->modules_present ? 4u : 0u) | (s->traffic_present ? 8u : 0u)
            | (s->gulls_present ? 16u : 0u),
        1);
    if (flags > 31 || s->version > BOAT_SCENE_VERSION) {
        w->ok = false;
        return;
    }
    s->entrance_present = flags & 1u;
    s->berths_present = flags & 2u;
    s->modules_present = flags & 4u;
    s->traffic_present = flags & 8u;
    s->gulls_present = flags & 16u;
    if (s->entrance_present) {
        s->entrance_x = (float)number(w, s->entrance_x);
        s->entrance_z = (float)number(w, s->entrance_z);
        s->entrance_width = (float)number(w, s->entrance_width);
    }
    s->berth_count = (unsigned)integer(w, s->berth_count, 4);
    s->module_count = (unsigned)integer(w, s->module_count, 4);
    if (s->berth_count > BOAT_SCENE_BERTHS || s->module_count > BOAT_SCENE_MODULES) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < s->berth_count; ++i) {
        BoatSceneBerth *b = &s->berths[i];
        string(w, b->id, sizeof(b->id));
        b->recipe = (unsigned char)integer(w, b->recipe, 1);
        b->mooring = (unsigned char)integer(w, b->mooring, 1);
        if (b->recipe >= BOAT_SCENE_BERTH_RECIPES || b->mooring > BOAT_SCENE_MOORING_PONTOON)
            w->ok = false;
        b->x = (float)number(w, b->x);
        b->z = (float)number(w, b->z);
        b->heading = (float)number(w, b->heading);
    }
    for (unsigned i = 0; i < s->module_count; ++i) {
        BoatSceneModule *m = &s->modules[i];
        string(w, m->id, sizeof(m->id));
        m->kind = (unsigned char)integer(w, m->kind, 1);
        if (m->kind >= BOAT_SCENE_MODULE_KINDS)
            w->ok = false;
        m->x = (float)number(w, m->x);
        m->z = (float)number(w, m->z);
        m->heading = (float)number(w, m->heading);
        m->anchor_x = (float)number(w, m->anchor_x);
        m->anchor_z = (float)number(w, m->anchor_z);
        string(w, m->attaches, sizeof(m->attaches));
    }
    if (s->traffic_present)
        traffic_wire(w, &s->traffic);
    if (s->gulls_present)
        flock_wire(w, &s->gulls);
}

static void voyage_payload(Wire *w, BoatPluginVoyageData *v)
{
    unsigned flags = (unsigned)integer(
        w, (v->callbacks ? 1u : 0u) | (v->migrates ? 2u : 0u) | (v->has_scene ? 4u : 0u), 1);
    if (flags > 7) {
        w->ok = false;
        return;
    }
    v->callbacks = flags & 1u;
    v->migrates = flags & 2u;
    v->has_scene = flags & 4u;
    if (v->has_scene)
        scene_wire(w, &v->scene);
    v->shelter_count = (unsigned)integer(w, v->shelter_count, 4);
    if (v->shelter_count < 2 || v->shelter_count > BOAT_VOYAGE_SHELTERS) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < v->shelter_count; ++i) {
        BoatPluginShelterData *s = &v->shelters[i];
        string(w, s->id, sizeof(s->id));
        string(w, s->name, sizeof(s->name));
        s->x = number(w, s->x);
        s->z = number(w, s->z);
        s->heading = number(w, s->heading);
        s->radius = number(w, s->radius);
    }
    v->arrival.x = number(w, v->arrival.x);
    v->arrival.z = number(w, v->arrival.z);
    v->arrival.radius = number(w, v->arrival.radius);
    v->arrival.max_speed = number(w, v->arrival.max_speed);
    v->exposure.x = number(w, v->exposure.x);
    v->exposure.z = number(w, v->exposure.z);
    v->exposure.radius = number(w, v->exposure.radius);
    v->exposure.height = number(w, v->exposure.height);
    v->exposure.build = number(w, v->exposure.build);
    v->exposed_fraction = number(w, v->exposed_fraction);
    v->weather_range = number(w, v->weather_range);
}

static void race_payload(Wire *w, BoatPluginRaceData *r)
{
    r->gate_count = (unsigned)integer(w, r->gate_count, 4);
    if (r->gate_count < 1 || r->gate_count > BOAT_RACE_GATES) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < 5; ++i)
        r->start[i] = number(w, r->start[i]);
    for (unsigned g = 0; g < r->gate_count; ++g)
        for (unsigned i = 0; i < 5; ++i)
            r->gates[g][i] = number(w, r->gates[g][i]);
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS * 2; ++i)
        (&r->grid[0][0])[i] = number(w, (&r->grid[0][0])[i]);
    for (unsigned i = 0; i < 12; ++i)
        r->source[i] = number(w, r->source[i]);
    for (unsigned i = 0; i < 8; ++i)
        r->storm[i] = number(w, r->storm[i]);
    for (unsigned i = 0; i < 9; ++i)
        r->rules[i] = number(w, r->rules[i]);
    unsigned flags = (unsigned)integer(w, r->has_scene ? 1u : 0u, 1);
    if (flags > 1) {
        w->ok = false;
        return;
    }
    r->has_scene = flags != 0;
    if (r->has_scene)
        scene_wire(w, &r->scene);
}

static void match_payload(Wire *w, BoatPluginMatchData *m)
{
    m->version = number(w, m->version);
    for (unsigned i = 0; i < 9; ++i)
        m->rules[i] = number(w, m->rules[i]);
    for (unsigned i = 0; i < 23; ++i)
        m->combat[i] = number(w, m->combat[i]);
    unsigned flags = (unsigned)integer(w, m->has_scene ? 1u : 0u, 1);
    if (flags > 1) {
        w->ok = false;
        return;
    }
    m->has_scene = flags != 0;
    if (m->has_scene)
        scene_wire(w, &m->scene);
}

static void course_payload(Wire *w, BoatPluginCourseData *c)
{
    for (unsigned i = 0; i < 3; ++i)
        c->start[i] = number(w, c->start[i]);
    c->gate_count = (unsigned)integer(w, c->gate_count, 4);
    if (c->gate_count != BOAT_COURSE_GATES) {
        w->ok = false;
        return;
    }
    for (unsigned g = 0; g < c->gate_count; ++g)
        for (unsigned i = 0; i < 3; ++i)
            c->gates[g][i] = number(w, c->gates[g][i]);
}

static void payload(Wire *w, BoatPluginDecoded *d)
{
    switch (d->kind) {
    case BOAT_PLUGIN_KIND_VOYAGE:
        voyage_payload(w, &d->voyage);
        break;
    case BOAT_PLUGIN_KIND_RACE:
        race_payload(w, &d->race);
        break;
    case BOAT_PLUGIN_KIND_FREE: {
        d->free.version = number(w, d->free.version);
        unsigned flags = (unsigned)integer(w, d->free.has_scene ? 1u : 0u, 1);
        if (flags > 1) {
            w->ok = false;
            break;
        }
        d->free.has_scene = flags != 0;
        if (d->free.has_scene)
            scene_wire(w, &d->free.scene);
        break;
    }
    case BOAT_PLUGIN_KIND_COURSE:
        course_payload(w, &d->course);
        break;
    case BOAT_PLUGIN_KIND_MATCH:
        match_payload(w, &d->match);
        break;
    default:
        w->ok = false;
        break;
    }
}

static bool encode(
    const BoatPluginDecoded *decoded, unsigned char *bytes, size_t capacity, size_t *size)
{
    Wire w = { .bytes = bytes, .capacity = capacity, .reading = false, .ok = true };
    integer(&w, BOAT_PLUGIN_WIRE_VERSION, 4);
    integer(&w, (unsigned)decoded->kind, 1);
    integer(&w, decoded->manifest_size, 4);
    if (decoded->manifest_size > BOAT_PLUGIN_MANIFEST_MAX)
        w.ok = false;
    else if (w.bytes) {
        if (w.at + decoded->manifest_size > w.capacity)
            w.ok = false;
        else {
            memcpy(w.bytes + w.at, decoded->manifest, decoded->manifest_size);
            w.at += decoded->manifest_size;
        }
    } else
        w.at += decoded->manifest_size;
    integer(&w, decoded->source_size, 4);
    if (decoded->source_size > BOAT_PLUGIN_SOURCE_MAX) {
        w.ok = false;
    } else if (w.bytes) {
        if (w.at + decoded->source_size > w.capacity)
            w.ok = false;
        else {
            memcpy(w.bytes + w.at, decoded->source, decoded->source_size);
            w.at += decoded->source_size;
        }
    } else
        w.at += decoded->source_size;
    BoatPluginDecoded copy = *decoded;
    payload(&w, &copy);
    if (size)
        *size = w.at;
    return w.ok;
}

size_t boat_plugin_wire_size(const BoatPluginDecoded *decoded)
{
    size_t size = 0;
    if (!encode(decoded, NULL, 0, &size))
        return 0;
    return size;
}

bool boat_plugin_wire_encode(
    const BoatPluginDecoded *decoded, unsigned char *bytes, size_t capacity, size_t *size)
{
    return encode(decoded, bytes, capacity, size);
}

bool boat_plugin_wire_decode(const unsigned char *bytes, size_t size, BoatPluginDecoded *decoded,
    BoatPluginDiagnostic *diagnostic)
{
    Wire w = { .bytes = (unsigned char *)bytes, .size = size, .reading = true, .ok = true };
    *decoded = (BoatPluginDecoded) { 0 };
    if (integer(&w, 0, 4) != BOAT_PLUGIN_WIRE_VERSION)
        w.ok = false;
    decoded->kind = (BoatPluginKind)integer(&w, 0, 1);
    if (decoded->kind <= BOAT_PLUGIN_KIND_NONE || decoded->kind > BOAT_PLUGIN_KIND_MATCH)
        w.ok = false;
    decoded->manifest_size = (size_t)integer(&w, 0, 4);
    if (decoded->manifest_size > BOAT_PLUGIN_MANIFEST_MAX)
        w.ok = false;
    else if (w.at + decoded->manifest_size > w.size)
        w.ok = false;
    else {
        memcpy(decoded->manifest, w.bytes + w.at, decoded->manifest_size);
        w.at += decoded->manifest_size;
    }
    if (w.ok
        && boat_plugin_parse_manifest(
               decoded->manifest, decoded->manifest_size, NULL, &decoded->parsed, diagnostic)
            != BOAT_PLUGIN_OK)
        w.ok = false;
    if (w.ok && boat_plugin_kind_parse(decoded->parsed.kind) != decoded->kind)
        w.ok = false;
    decoded->source_size = (size_t)integer(&w, 0, 4);
    if (decoded->source_size > BOAT_PLUGIN_SOURCE_MAX)
        w.ok = false;
    else {
        if (w.at + decoded->source_size > w.size)
            w.ok = false;
        else {
            memcpy(decoded->source, w.bytes + w.at, decoded->source_size);
            w.at += decoded->source_size;
        }
    }
    if (w.ok)
        payload(&w, decoded);
    if (!w.ok || w.at != w.size) {
        if (diagnostic) {
            diagnostic->status = BOAT_PLUGIN_SCHEMA;
            snprintf(diagnostic->file, sizeof(diagnostic->file), "%s", "catalog");
            diagnostic->line = 0;
            snprintf(diagnostic->field, sizeof(diagnostic->field), "%s", "");
            snprintf(diagnostic->message, sizeof(diagnostic->message), "%s",
                w.at != w.size && w.ok ? "has trailing bytes" : "is malformed or truncated");
        }
        return false;
    }
    return true;
}

bool boat_plugin_decoded_revision(const BoatPluginDecoded *decoded, BoatPluginRevision *revision,
    BoatPluginDiagnostic *diagnostic)
{
    *revision = (BoatPluginRevision) { 0 };
    revision->manifest = decoded->parsed;
    revision->source_size = decoded->source_size;
    memcpy(revision->source, decoded->source, decoded->source_size);
    BoatPluginStatus status;
    switch (decoded->kind) {
    case BOAT_PLUGIN_KIND_VOYAGE:
        status = boat_plugin_decode_voyage(
            &decoded->parsed, &decoded->voyage, &revision->voyage, diagnostic);
        break;
    case BOAT_PLUGIN_KIND_RACE:
        status = boat_plugin_decode_race(
            &decoded->parsed, &decoded->race, &revision->race, diagnostic);
        break;
    case BOAT_PLUGIN_KIND_FREE:
        status = boat_plugin_decode_free(
            &decoded->parsed, &decoded->free, &revision->free, diagnostic);
        break;
    case BOAT_PLUGIN_KIND_COURSE:
        status = boat_plugin_decode_course(
            &decoded->parsed, &decoded->course, &revision->course, diagnostic);
        break;
    case BOAT_PLUGIN_KIND_MATCH:
        status = boat_plugin_decode_match(
            &decoded->parsed, &decoded->match, &revision->match, diagnostic);
        break;
    default:
        status = BOAT_PLUGIN_MANIFEST;
        break;
    }
    return status == BOAT_PLUGIN_OK;
}
