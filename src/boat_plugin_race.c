#include "boat_plugin_lua.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static BoatPluginStatus fail(BoatPluginDiagnostic *d, BoatPluginStatus status, const char *file,
    const char *field, const char *message)
{
    d->status = status;
    d->line = 0;
    snprintf(d->file, sizeof(d->file), "%s", file);
    snprintf(d->field, sizeof(d->field), "%s", field);
    snprintf(d->message, sizeof(d->message), "%s", message);
    return status;
}

// Validate before conversion: no float overflow or integer truncation can hide a bad edit.
static bool fields(BoatPluginDiagnostic *d, const char *file, const char *path,
    const char *const *names, const double *values, const double *low, const double *high,
    unsigned count, bool integers)
{
    for (unsigned i = 0; i < count; ++i)
        if (!isfinite(values[i]) || values[i] < low[i] || values[i] > high[i]
            || (integers && floor(values[i]) != values[i])) {
            char at[64], message[160];
            snprintf(at, sizeof(at), "%s.%s", path, names[i]);
            snprintf(message, sizeof(message), "must be %sfinite in [%g, %g]",
                integers ? "an integer and " : "", low[i], high[i]);
            fail(d, BOAT_PLUGIN_SCHEMA, file, at, message);
            return false;
        }
    return true;
}

static BoatRaceGate gate(const double *v)
{
    return (BoatRaceGate) { .centre = { (float)v[0], (float)v[1] },
        .direction = { (float)v[2], (float)v[3] },
        .half_width = (float)v[4] };
}

BoatPluginStatus boat_plugin_load_race(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginRace *out, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginDiagnostic scratch;
    BoatPluginDiagnostic *d = diagnostic ? diagnostic : &scratch;
    if (!limits)
        limits = &boat_plugin_default_limits;
    BoatPluginRace r = { 0 };
    BoatPluginStatus status = boat_plugin_parse_manifest(
        bundle->manifest, bundle->manifest_size, limits, &r.manifest, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    if (strcmp(r.manifest.kind, "race"))
        return fail(d, BOAT_PLUGIN_MANIFEST, "plugin.json", "kind", "requires a race host");
    const char *file = r.manifest.entry;
    if (bundle->source_size > limits->source_bytes || bundle->source_size > BOAT_PLUGIN_SOURCE_MAX)
        return fail(d, BOAT_PLUGIN_LIMIT, file, "", "source exceeds the host limit");
    BoatPluginRaceData data;
    status = boat_plugin_lua_race(bundle->source, bundle->source_size, file, limits, &data, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    return boat_plugin_decode_race(&r.manifest, &data, out, d);
}

BoatPluginStatus boat_plugin_decode_race(const BoatPluginManifest *manifest,
    const BoatPluginRaceData *data, BoatPluginRace *out, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginDiagnostic scratch;
    BoatPluginDiagnostic *d = diagnostic ? diagnostic : &scratch;
    if (boat_plugin_kind_parse(manifest->kind) != BOAT_PLUGIN_KIND_RACE)
        return fail(d, BOAT_PLUGIN_MANIFEST, "plugin.json", "kind", "requires a race host");
    const char *file = manifest->entry;
    BoatPluginRace r = { .manifest = *manifest };
    static const char *const gate_names[] = { BOAT_PLUGIN_GATE_FIELDS };
    static const double gate_low[] = { -20000, -20000, -1, -1, 4 };
    static const double gate_high[] = { 20000, 20000, 1, 1, 500 };
    for (unsigned i = 0; i <= data->gate_count; ++i) {
        char path[32];
        if (i)
            snprintf(path, sizeof(path), "race.gates[%u]", i);
        else
            snprintf(path, sizeof(path), "race.start");
        const double *v = i ? data->gates[i - 1] : data->start;
        if (!fields(d, file, path, gate_names, v, gate_low, gate_high, 5, false))
            return BOAT_PLUGIN_SCHEMA;
        if (fabs(hypot(v[2], v[3]) - 1) > .01)
            return fail(d, BOAT_PLUGIN_SCHEMA, file, path, "direction must have unit length");
        if (i)
            r.course.gates[i - 1] = gate(v);
        else
            r.course.start = gate(v);
    }
    r.course.gate_count = data->gate_count;
    static const char *const point_names[] = { "x", "z" };
    static const double point_low[] = { -20000, -20000 }, point_high[] = { 20000, 20000 };
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "race.grid[%u]", i + 1);
        if (!fields(d, file, path, point_names, data->grid[i], point_low, point_high, 2, false))
            return BOAT_PLUGIN_SCHEMA;
        r.course.grid[i] = (Vector2) { (float)data->grid[i][0], (float)data->grid[i][1] };
    }
    static const char *const source_names[] = { BOAT_PLUGIN_SOURCE_FIELDS };
    static const double source_low[] = { -20000, -20000, -1, -1, -600, .1, .001, 1, 1, 2, 1, 2 };
    static const double source_high[] = { 20000, 20000, 1, 1, 600, 30, 5, 60, 60, 40, 10, 80 };
    static const char *const storm_names[] = { BOAT_PLUGIN_STORM_FIELDS };
    static const double storm_low[] = { -20000, -20000, -50, -50, 0, 0, -600, 0 };
    static const double storm_high[] = { 20000, 20000, 50, 50, 5000, 5, 600, 600 };
    static const char *const rule_names[] = { BOAT_PLUGIN_RULE_FIELDS };
    static const double rule_low[] = { 0, 1, 0, 1, 1, 0, 0, 0, 1 };
    static const double rule_high[]
        = { 432000, 432000, 432000, 432000, 432000, 432000, 432000, 432000, BOAT_FLEET_SLOTS };
    if (!fields(
            d, file, "race.source", source_names, data->source, source_low, source_high, 12, false)
        || !fields(d, file, "race.storm", storm_names, data->storm, storm_low, storm_high, 8, false)
        || !fields(d, file, "race.rules", rule_names, data->rules, rule_low, rule_high, 9, true))
        return BOAT_PLUGIN_SCHEMA;
    const double *v = data->source;
    r.course.source = (BoatVesselWake) { .origin = { (float)v[0], (float)v[1] },
        .direction = { (float)v[2], (float)v[3] },
        .started = v[4],
        .speed = (float)v[5],
        .height = (float)v[6],
        .duration = (float)v[7],
        .lifetime = (float)v[8],
        .wavelength = (float)v[9],
        .spread = (float)v[10],
        .half_width = (float)v[11] };
    if (!boat_vessel_wake_valid(r.course.source))
        return fail(d, BOAT_PLUGIN_SCHEMA, file, "race.source", "invalid ferry wake recipe");
    v = data->storm;
    r.course.storm = (BoatStorm) { .centre = { (float)v[0], (float)v[1] },
        .velocity = { (float)v[2], (float)v[3] },
        .radius = (float)v[4],
        .height = (float)v[5],
        .started = v[6],
        .build = (float)v[7] };
    v = data->rules;
    r.rules = (BoatRaceRules) { .grid_ticks = (uint64_t)v[0],
        .countdown_ticks = (uint64_t)v[1],
        .results_ticks = (uint64_t)v[2],
        .after_winner_ticks = (uint64_t)v[3],
        .cap_ticks = (uint64_t)v[4],
        .recovery_ticks = (uint64_t)v[5],
        .skip_ticks = (uint64_t)v[6],
        .park_ticks = (uint64_t)v[7],
        .min_players = (unsigned)v[8] };
    if (r.rules.skip_ticks > r.rules.recovery_ticks)
        return fail(
            d, BOAT_PLUGIN_SCHEMA, file, "race.rules.skip_ticks", "must not exceed recovery_ticks");
    BoatRace native;
    if (!boat_race_init(&native, &r.rules, &r.course))
        return fail(d, BOAT_PLUGIN_SCHEMA, file, "race", "invalid native course or rules");
    r.digest = boat_plugin_race_digest(&r.course, &r.rules);
    *out = r;
    BoatPluginCost cost = d->cost;
    *d = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_OK, .cost = cost };
    return BOAT_PLUGIN_OK;
}

// Canonical little-endian IEEE double values; float fields are promoted exactly. No padding,
// pointers or native endianness enter the digest. Authored integer ticks are at most 432000.
static void hash_number(uint64_t *hash, double v)
{
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    for (unsigned i = 0; i < 8; ++i) {
        *hash ^= (bits >> (8 * i)) & 255;
        *hash *= 1099511628211ull;
    }
}

uint64_t boat_plugin_race_digest(const BoatRaceCourse *c, const BoatRaceRules *r)
{
    uint64_t hash = 14695981039346656037ull;
    hash_number(&hash, c->gate_count);
    for (unsigned i = 0; i <= c->gate_count; ++i) {
        const BoatRaceGate *g = i ? &c->gates[i - 1] : &c->start;
        const double values[]
            = { g->centre.x, g->centre.y, g->direction.x, g->direction.y, g->half_width };
        for (unsigned j = 0; j < 5; ++j)
            hash_number(&hash, values[j]);
    }
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        hash_number(&hash, c->grid[i].x);
        hash_number(&hash, c->grid[i].y);
    }
    const BoatVesselWake *v = &c->source;
    const BoatStorm *s = &c->storm;
    const double values[] = { v->origin.x, v->origin.y, v->direction.x, v->direction.y, v->started,
        v->speed, v->height, v->duration, v->lifetime, v->wavelength, v->spread, v->half_width,
        s->centre.x, s->centre.y, s->velocity.x, s->velocity.y, s->radius, s->height, s->started,
        s->build, (double)r->grid_ticks, (double)r->countdown_ticks, (double)r->results_ticks,
        (double)r->after_winner_ticks, (double)r->cap_ticks, (double)r->recovery_ticks,
        (double)r->skip_ticks, (double)r->park_ticks, r->min_players };
    for (unsigned i = 0; i < sizeof(values) / sizeof(*values); ++i)
        hash_number(&hash, values[i]);
    return hash;
}

BoatPluginChange boat_plugin_race_classify(const BoatPluginRace *a, const BoatPluginRace *b)
{
    if (strcmp(a->manifest.id, b->manifest.id) || strcmp(a->manifest.kind, b->manifest.kind)
        || strcmp(a->manifest.location, b->manifest.location) || a->manifest.api != b->manifest.api
        || a->manifest.state_version != b->manifest.state_version)
        return BOAT_PLUGIN_INCOMPATIBLE;
    if (a->digest != b->digest)
        return BOAT_PLUGIN_RESTART_SESSION;
    return strcmp(a->manifest.name, b->manifest.name) ? BOAT_PLUGIN_RENAMED : BOAT_PLUGIN_UNCHANGED;
}

static Vector2 along(const BoatRaceGate *g, float forward, float side)
{
    return Vector2Add(g->centre,
        Vector2Add(Vector2Scale(g->direction, forward),
            Vector2Scale((Vector2) { -g->direction.y, g->direction.x }, side)));
}

BoatPluginStatus boat_plugin_race_check_location(const BoatPluginRace *r,
    const BoatWorldDefinition *location, const BoatMap *map, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginDiagnostic scratch = { 0 };
    BoatPluginDiagnostic *d = diagnostic ? diagnostic : &scratch;
    const char *file = r->manifest.entry;
    if (!location || strcmp(location->name, "river") || !map)
        return fail(d, BOAT_PLUGIN_MANIFEST, "plugin.json", "location", "requires the river map");
    // Use normalized native gates for the same crossing/recovery geometry as the race engine.
    BoatRace native;
    if (!boat_race_init(&native, &r->rules, &r->course))
        return fail(d, BOAT_PLUGIN_SCHEMA, file, "race", "invalid native race");
    const BoatRaceCourse *c = &native.course;
    BoatWorldResources resources = { .definition = location, .map = *map };
    BoatWorld world;
    boat_world_init(&world, &resources, (BoatWater) { 0 });
    for (unsigned i = 0; i <= c->gate_count; ++i) {
        const BoatRaceGate *g = i ? &c->gates[i - 1] : &c->start;
        char path[32];
        if (i)
            snprintf(path, sizeof(path), "race.gates[%u]", i);
        else
            snprintf(path, sizeof(path), "race.start");
        // Full-width finish endpoints can reach the shore; skill gates need hull clearance.
        for (int edge = -1; edge <= 1; ++edge)
            if (!boat_map_water(map, along(g, 0, edge * g->half_width), i == c->gate_count ? 0 : 8))
                return fail(d, BOAT_PLUGIN_SCHEMA, file, path, "gate opening is not navigable");
        Boat boat;
        bool safe = false;
        for (unsigned k = 0; k < 14; ++k)
            if (boat_race_recovery_candidate(g, i == c->gate_count, k, &boat)
                && boat_hull_clear(&boat, &world, 0))
                safe = true;
        if (!safe)
            return fail(
                d, BOAT_PLUGIN_SCHEMA, file, path, "has no safe native recovery/parking point");
        if (!i)
            continue;
        const BoatRaceGate *from = i == 1 ? &c->start : &c->gates[i - 2];
        float distance = Vector2Distance(from->centre, g->centre);
        if (distance < 20 || distance > 5000
            || Vector2DotProduct(Vector2Subtract(g->centre, from->centre), from->direction) < 10
            || Vector2DotProduct(Vector2Subtract(g->centre, from->centre), g->direction) < 10)
            return fail(d, BOAT_PLUGIN_SCHEMA, file, path, "degenerate or backward course segment");
        unsigned steps = (unsigned)ceilf(distance / 5);
        for (unsigned k = 0; k <= steps; ++k)
            for (int lane = -1; lane <= 1; ++lane) {
                Vector2 a = along(from, 0, lane * fminf(21, from->half_width - 4));
                Vector2 b = along(g, 0, lane * fminf(21, g->half_width - 4));
                if (!boat_map_water(map, Vector2Lerp(a, b, (float)k / steps), 12))
                    return fail(
                        d, BOAT_PLUGIN_SCHEMA, file, path, "approach lanes cross the shore");
            }
    }
    for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "race.grid[%u]", i + 1);
        Vector2 delta = Vector2Subtract(c->grid[i], c->start.centre);
        if (Vector2DotProduct(delta, c->start.direction) > -10 || Vector2Length(delta) > 150
            || !boat_map_water(map, c->grid[i], 8))
            return fail(d, BOAT_PLUGIN_SCHEMA, file, path, "must be safe water behind the start");
        Boat boat;
        boat_reset(&boat);
        boat.position = c->grid[i];
        boat.heading = atan2f(c->start.direction.x, -c->start.direction.y);
        if (!boat_hull_clear(&boat, &world, 0))
            return fail(d, BOAT_PLUGIN_SCHEMA, file, path, "grid hull overlaps a native contact");
        for (unsigned j = 0; j < i; ++j)
            if (Vector2Distance(c->grid[i], c->grid[j]) < 13)
                return fail(d, BOAT_PLUGIN_SCHEMA, file, path, "grid hulls need 13 m separation");
    }
    const BoatVesselWake *v = &c->source;
    unsigned steps = (unsigned)ceilf(v->speed * v->duration / 5);
    for (unsigned k = 0; k <= steps; ++k) {
        Vector2 centre
            = Vector2Add(v->origin, Vector2Scale(v->direction, v->speed * v->duration * k / steps));
        for (int end = -1; end <= 1; end += 2)
            for (int edge = -1; edge <= 1; edge += 2) {
                Vector2 corner = Vector2Add(centre,
                    Vector2Add(Vector2Scale(v->direction, end * BOAT_VESSEL_LENGTH * .5f),
                        Vector2Scale((Vector2) { -v->direction.y, v->direction.x },
                            edge * BOAT_VESSEL_BEAM * .5f)));
                if (!boat_map_water(map, corner, 1))
                    return fail(d, BOAT_PLUGIN_SCHEMA, file, "race.source",
                        "ferry footprint crosses shore");
            }
    }
    d->status = BOAT_PLUGIN_OK;
    return BOAT_PLUGIN_OK;
}
