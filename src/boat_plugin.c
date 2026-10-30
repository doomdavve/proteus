#include "boat_plugin.h"

#include "boat_json.h"
#include "boat_plugin_lua.h"
#include "raymath.h"
#include "waterfront_harbor.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const BoatPluginLimits boat_plugin_default_limits = {
    .manifest_bytes = BOAT_PLUGIN_MANIFEST_MAX,
    .source_bytes = BOAT_PLUGIN_SOURCE_MAX,
    .memory_bytes = 2u << 20,
    .instructions = 1000000,
};

// Authored bounds (docs/PLUGIN_API.md): coordinates and ranges in metres, speeds in m/s.
#define COORDINATE_MAX 20000.0
#define SHELTER_RADIUS_MAX 1000.0
#define ARRIVAL_SPEED_MAX 50.0
#define EXPOSURE_RADIUS_MAX 5000.0
#define EXPOSURE_HEIGHT_MAX 5.0
#define EXPOSURE_BUILD_MAX 600.0
#define TAU 6.283185307179586

const char *boat_plugin_status_name(BoatPluginStatus status)
{
    static const char *const names[]
        = { "ok", "limit", "io", "manifest", "syntax", "runtime", "memory", "budget", "schema" };
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "unknown";
}

static BoatPluginStatus diagnose(BoatPluginDiagnostic *d, BoatPluginStatus status, const char *file,
    const char *field, const char *format, ...)
{
    if (!d)
        return status;
    d->status = status;
    d->line = 0;
    snprintf(d->file, sizeof(d->file), "%s", file);
    snprintf(d->field, sizeof(d->field), "%s", field);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(d->message, sizeof(d->message), format, arguments);
    va_end(arguments);
    return status;
}

static const BoatPluginLimits *bounded(const BoatPluginLimits *limits)
{
    return limits ? limits : &boat_plugin_default_limits;
}

// Manifest ------------------------------------------------------------------------------------

static const char *const MANIFEST_KEYS[]
    = { "id", "name", "api", "kind", "entry", "location", "capabilities", "state_version" };
#define MANIFEST_KEY_COUNT (sizeof(MANIFEST_KEYS) / sizeof(MANIFEST_KEYS[0]))

static bool identifier(const char *id)
{
    if (!(*id >= 'a' && *id <= 'z'))
        return false;
    for (; *id; ++id)
        if (!((*id >= 'a' && *id <= 'z') || (*id >= '0' && *id <= '9') || strchr("._-", *id)))
            return false;
    return true;
}

// A plain file name in the package directory: no separators, no leading dot, no "..".
static bool plain_file(const char *name)
{
    return *name && *name != '.' && !strpbrk(name, "/\\:") && !strstr(name, "..");
}

BoatPluginStatus boat_plugin_parse_manifest(const char *json, size_t size,
    const BoatPluginLimits *limits, BoatPluginManifest *out, BoatPluginDiagnostic *d)
{
    const char *file = "plugin.json";
    limits = bounded(limits);
    if (d)
        *d = (BoatPluginDiagnostic) { 0 };
    if (size > limits->manifest_bytes || size > BOAT_PLUGIN_MANIFEST_MAX)
        return diagnose(d, BOAT_PLUGIN_LIMIT, file, "", "is %zu bytes; the limit is %zu", size,
            limits->manifest_bytes < BOAT_PLUGIN_MANIFEST_MAX ? limits->manifest_bytes
                                                              : (size_t)BOAT_PLUGIN_MANIFEST_MAX);
    jsmntok_t tokens[64];
    int count = boat_json_parse(json, size, tokens, 64);
    if (count < 0)
        return diagnose(d, BOAT_PLUGIN_MANIFEST, file, "", "is not one well-formed JSON object");
    // Every member is known and appears once.
    for (int i = 1; i < count; i = boat_json_next(tokens, count, i + 1)) {
        char key[32];
        if (!boat_json_text(json, &tokens[i], key, sizeof(key)))
            return diagnose(d, BOAT_PLUGIN_MANIFEST, file, "", "has an unreadable field name");
        bool known = false;
        for (size_t k = 0; k < MANIFEST_KEY_COUNT; ++k)
            known |= !strcmp(key, MANIFEST_KEYS[k]);
        if (!known)
            return diagnose(d, BOAT_PLUGIN_MANIFEST, file, key, "is not a known field");
        if (boat_json_member(json, tokens, count, 0, key) == -2)
            return diagnose(d, BOAT_PLUGIN_MANIFEST, file, key, "appears more than once");
    }
    int at[MANIFEST_KEY_COUNT];
    for (size_t k = 0; k < MANIFEST_KEY_COUNT; ++k)
        if ((at[k] = boat_json_member(json, tokens, count, 0, MANIFEST_KEYS[k])) < 0)
            return diagnose(d, BOAT_PLUGIN_MANIFEST, file, MANIFEST_KEYS[k], "is missing");
    BoatPluginManifest m = { 0 };
    int api, state;
    if (!boat_json_text(json, &tokens[at[0]], m.id, sizeof(m.id)) || !identifier(m.id))
        return diagnose(d, BOAT_PLUGIN_MANIFEST, file, "id",
            "must be 1-63 characters of a-z, 0-9, '.', '_' or '-', starting with a letter");
    if (!boat_json_text(json, &tokens[at[1]], m.name, sizeof(m.name)))
        return diagnose(
            d, BOAT_PLUGIN_MANIFEST, file, "name", "must be 1-63 printable ASCII characters");
    if (!boat_json_int(json, &tokens[at[2]], &api) || api != BOAT_PLUGIN_API)
        return diagnose(
            d, BOAT_PLUGIN_MANIFEST, file, "api", "must be %d, the supported API", BOAT_PLUGIN_API);
    if (!boat_json_text(json, &tokens[at[3]], m.kind, sizeof(m.kind))
        || (strcmp(m.kind, "voyage") && strcmp(m.kind, "race")))
        return diagnose(d, BOAT_PLUGIN_MANIFEST, file, "kind", "must be voyage or race");
    if (!boat_json_text(json, &tokens[at[4]], m.entry, sizeof(m.entry)) || !plain_file(m.entry)
        || strcmp(m.entry, "main.lua"))
        return diagnose(d, BOAT_PLUGIN_MANIFEST, file, "entry", "must be \"main.lua\"");
    if (!boat_json_text(json, &tokens[at[5]], m.location, sizeof(m.location))
        || strcmp(m.location, !strcmp(m.kind, "race") ? "river" : "outer"))
        return diagnose(d, BOAT_PLUGIN_MANIFEST, file, "location",
            "must match kind: outer for voyage, river for race");
    const char *grant = !strcmp(m.kind, "race") ? "race.define" : "voyage.define";
    const jsmntok_t *capabilities = &tokens[at[6]];
    if (capabilities->type != JSMN_ARRAY || capabilities->size < 1
        || capabilities->size > (!strcmp(m.kind, "race") ? 1 : 4))
        return diagnose(d, BOAT_PLUGIN_MANIFEST, file, "capabilities",
            !strcmp(m.kind, "race") ? "must contain exactly race.define"
                                    : "must contain voyage.define and optional grants");
    char capability[32];
    if (!boat_json_text(json, &tokens[at[6] + 1], capability, sizeof(capability))
        || strcmp(capability, grant))
        return diagnose(
            d, BOAT_PLUGIN_MANIFEST, file, "capabilities[1]", "is not granted for this kind");
    // Voyage grants are a canonical prefix: scene.define, objective.hud, objective.timers.
    // A package cannot reorder or repeat them, and timers require HUD.
    int order = 0;
    for (int i = 1; i < capabilities->size; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "capabilities[%u]", i + 1);
        if (!boat_json_text(json, &tokens[at[6] + i + 1], capability, sizeof(capability)))
            return diagnose(d, BOAT_PLUGIN_MANIFEST, file, path, "is not a string");
        if (!strcmp(capability, "scene.define") && order == 0) {
            m.scene = true;
            order = 1;
        } else if (!strcmp(capability, "objective.hud") && order <= 1) {
            m.objective = true;
            order = 2;
        } else if (!strcmp(capability, "objective.timers") && order == 2) {
            m.timers = true;
            order = 3;
        } else
            return diagnose(d, BOAT_PLUGIN_MANIFEST, file, path,
                "is not granted in the order scene.define, objective.hud, objective.timers");
    }
    if (!boat_json_int(json, &tokens[at[7]], &state) || state < 1 || state > 65535
        || (!m.objective && state != BOAT_PLUGIN_STATE_VERSION))
        return diagnose(
            d, BOAT_PLUGIN_MANIFEST, file, "state_version", "must be an integer in 1..65535");
    m.api = (unsigned)api;
    m.state_version = (unsigned)state;
    *out = m;
    return BOAT_PLUGIN_OK;
}

// Voyage ---------------------------------------------------------------------------------------

typedef struct {
    BoatPluginDiagnostic *diagnostic;
    const char *file;
} Check;

static bool finite_in(
    Check *c, const char *path, double value, double low, double high, const char *units)
{
    if (isfinite(value) && value >= low && value <= high)
        return true;
    if (!isfinite(value))
        diagnose(c->diagnostic, BOAT_PLUGIN_SCHEMA, c->file, path, "must be a finite number");
    else
        diagnose(c->diagnostic, BOAT_PLUGIN_SCHEMA, c->file, path,
            "is %g; it must be from %g to %g %s", value, low, high, units);
    return false;
}

static bool coordinates(Check *c, const char *path, double x, double z)
{
    char at[64];
    snprintf(at, sizeof(at), "%s.x", path);
    if (!finite_in(c, at, x, -COORDINATE_MAX, COORDINATE_MAX, "m"))
        return false;
    snprintf(at, sizeof(at), "%s.z", path);
    return finite_in(c, at, z, -COORDINATE_MAX, COORDINATE_MAX, "m");
}

static bool shelter_name(const char *name)
{
    size_t n = strlen(name);
    if (name[0] == ' ' || name[n - 1] == ' ')
        return false;
    for (; *name; ++name)
        if (!((*name >= 'A' && *name <= 'Z') || (*name >= '0' && *name <= '9')
                || strchr(" .-'", *name)))
            return false;
    return true;
}

static bool validate(Check *c, const BoatPluginVoyageData *v)
{
    char at[64];
    unsigned last = v->shelter_count - 1;
    for (unsigned i = 0; i < v->shelter_count; ++i) {
        const BoatPluginShelterData *s = &v->shelters[i];
        char path[48];
        snprintf(path, sizeof(path), "voyage.shelters[%u]", i + 1);
        snprintf(at, sizeof(at), "%s.id", path);
        if (!identifier(s->id) || strchr(s->id, '.') || strchr(s->id, '-')) {
            diagnose(c->diagnostic, BOAT_PLUGIN_SCHEMA, c->file, at,
                "must be a-z, 0-9 or '_', starting with a letter");
            return false;
        }
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(s->id, v->shelters[j].id)) {
                diagnose(c->diagnostic, BOAT_PLUGIN_SCHEMA, c->file, at,
                    "repeats voyage.shelters[%u].id \"%s\"", j + 1, s->id);
                return false;
            }
        snprintf(at, sizeof(at), "%s.name", path);
        if (!shelter_name(s->name)) {
            diagnose(c->diagnostic, BOAT_PLUGIN_SCHEMA, c->file, at,
                "must be upper-case A-Z, 0-9, space, '.', '-' or ''' for the HUD font");
            return false;
        }
        if (!coordinates(c, path, s->x, s->z))
            return false;
        snprintf(at, sizeof(at), "%s.heading", path);
        if (!finite_in(c, at, s->heading, -TAU, TAU, "radians"))
            return false;
        snprintf(at, sizeof(at), "%s.radius", path);
        if (i == last ? s->radius != 0 : !(s->radius > 0)) {
            diagnose(c->diagnostic, BOAT_PLUGIN_SCHEMA, c->file, at,
                i == last ? "must be 0: the destination's shelter is reached only by arriving"
                          : "must be greater than 0");
            return false;
        }
        if (!finite_in(c, at, s->radius, 0, SHELTER_RADIUS_MAX, "m"))
            return false;
    }
    if (!coordinates(c, "voyage.arrival", v->arrival.x, v->arrival.z)
        || !finite_in(c, "voyage.arrival.radius", v->arrival.radius, 1, SHELTER_RADIUS_MAX, "m")
        || !finite_in(
            c, "voyage.arrival.max_speed", v->arrival.max_speed, 0.5, ARRIVAL_SPEED_MAX, "m/s"))
        return false;
    const BoatPluginShelterData *end = &v->shelters[last];
    if (hypot(end->x - v->arrival.x, end->z - v->arrival.z) > v->arrival.radius) {
        snprintf(at, sizeof(at), "voyage.shelters[%u]", last + 1);
        diagnose(c->diagnostic, BOAT_PLUGIN_SCHEMA, c->file, at,
            "is the destination's shelter and must lie inside voyage.arrival");
        return false;
    }
    if (!coordinates(c, "voyage.exposure", v->exposure.x, v->exposure.z)
        || !finite_in(c, "voyage.exposure.radius", v->exposure.radius, 0, EXPOSURE_RADIUS_MAX, "m")
        || !finite_in(c, "voyage.exposure.height", v->exposure.height, 0, EXPOSURE_HEIGHT_MAX, "m")
        || !finite_in(c, "voyage.exposure.build", v->exposure.build, 0, EXPOSURE_BUILD_MAX, "s")
        || !finite_in(c, "voyage.exposed_fraction", v->exposed_fraction, 0.05, 1, "")
        || !finite_in(c, "voyage.weather_range", v->weather_range, 0, COORDINATE_MAX, "m"))
        return false;
    // Recovery must not place the boat in the exposed water.
    double exposed = v->exposure.radius * v->exposed_fraction;
    for (unsigned i = 0; i < v->shelter_count; ++i)
        if (hypot(v->shelters[i].x - v->exposure.x, v->shelters[i].z - v->exposure.z) < exposed) {
            snprintf(at, sizeof(at), "voyage.shelters[%u]", i + 1);
            diagnose(c->diagnostic, BOAT_PLUGIN_SCHEMA, c->file, at,
                "lies inside the exposed water, %.0f m from its centre", exposed);
            return false;
        }
    return true;
}

static void bind(BoatPluginVoyage *voyage)
{
    for (unsigned i = 0; i < BOAT_VOYAGE_SHELTERS; ++i)
        voyage->route.shelters[i].name
            = i < voyage->route.shelter_count ? voyage->shelter_names[i] : NULL;
}

void boat_plugin_voyage_copy(BoatPluginVoyage *destination, const BoatPluginVoyage *source)
{
    if (destination == source)
        return;
    *destination = *source;
    bind(destination);
}

static void fold(uint64_t *hash, const void *bytes, size_t size)
{
    const unsigned char *p = bytes;
    for (size_t i = 0; i < size; ++i) {
        *hash ^= p[i];
        *hash *= 1099511628211ull;
    }
}

static void value(uint64_t *hash, const char *name, float number)
{
    fold(hash, name, strlen(name));
    uint32_t bits;
    memcpy(&bits, &number, sizeof(bits));
    unsigned char bytes[4] = { (unsigned char)bits, (unsigned char)(bits >> 8),
        (unsigned char)(bits >> 16), (unsigned char)(bits >> 24) };
    fold(hash, bytes, sizeof(bytes));
}

uint64_t boat_plugin_route_digest(const BoatVoyageRoute *route)
{
    uint64_t hash = 14695981039346656037ull;
    unsigned char count = (unsigned char)route->shelter_count;
    fold(&hash, "shelters", 8);
    fold(&hash, &count, 1);
    for (unsigned i = 0; i < route->shelter_count; ++i) {
        const BoatVoyageShelter *s = &route->shelters[i];
        fold(&hash, "name", 4);
        fold(&hash, s->name, strlen(s->name) + 1);
        value(&hash, "x", s->position.x);
        value(&hash, "z", s->position.y);
        value(&hash, "heading", s->heading);
        value(&hash, "radius", s->radius);
    }
    value(&hash, "arrival.x", route->arrival_centre.x);
    value(&hash, "arrival.z", route->arrival_centre.y);
    value(&hash, "arrival.radius", route->arrival_radius);
    value(&hash, "arrival.speed", route->arrival_speed);
    value(&hash, "exposure.x", route->exposure.centre.x);
    value(&hash, "exposure.z", route->exposure.centre.y);
    value(&hash, "exposure.velocity.x", route->exposure.velocity.x);
    value(&hash, "exposure.velocity.z", route->exposure.velocity.y);
    value(&hash, "exposure.radius", route->exposure.radius);
    value(&hash, "exposure.height", route->exposure.height);
    value(&hash, "exposure.started", (float)route->exposure.started);
    value(&hash, "exposure.build", route->exposure.build);
    value(&hash, "exposed_fraction", route->exposed_fraction);
    value(&hash, "weather_range", route->weather_range);
    return hash;
}

BoatPluginStatus boat_plugin_load_voyage(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginVoyage *out, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginDiagnostic scratch;
    BoatPluginDiagnostic *d = diagnostic ? diagnostic : &scratch;
    limits = bounded(limits);
    BoatPluginManifest manifest;
    BoatPluginStatus status
        = boat_plugin_parse_manifest(bundle->manifest, bundle->manifest_size, limits, &manifest, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    if (strcmp(manifest.kind, "voyage"))
        return diagnose(d, BOAT_PLUGIN_MANIFEST, "plugin.json", "kind", "requires a voyage host");
    if (bundle->source_size > limits->source_bytes || bundle->source_size > BOAT_PLUGIN_SOURCE_MAX)
        return diagnose(d, BOAT_PLUGIN_LIMIT, manifest.entry, "", "is %zu bytes; the limit is %zu",
            bundle->source_size,
            limits->source_bytes < BOAT_PLUGIN_SOURCE_MAX ? limits->source_bytes
                                                          : (size_t)BOAT_PLUGIN_SOURCE_MAX);
    BoatPluginVoyageData data;
    status = boat_plugin_lua_voyage(
        bundle->source, bundle->source_size, manifest.entry, limits, &data, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    return boat_plugin_decode_voyage(&manifest, &data, out, d);
}

BoatPluginStatus boat_plugin_decode_voyage(const BoatPluginManifest *manifest,
    const BoatPluginVoyageData *data, BoatPluginVoyage *out, BoatPluginDiagnostic *d)
{
    if (data->shelter_count < 2 || data->shelter_count > BOAT_VOYAGE_SHELTERS)
        return diagnose(
            d, BOAT_PLUGIN_SCHEMA, manifest->entry, "voyage.shelters", "requires 2..4 shelters");
    if (data->callbacks != manifest->objective)
        return diagnose(d, BOAT_PLUGIN_SCHEMA, manifest->entry, "init/update",
            "callbacks require objective.hud and that capability requires both callbacks");
    if (data->has_scene && !manifest->scene)
        return diagnose(d, BOAT_PLUGIN_SCHEMA, manifest->entry, "scene",
            "requires the scene.define capability");
    if (data->has_scene && data->scene.version != BOAT_SCENE_VERSION)
        return diagnose(d, BOAT_PLUGIN_SCHEMA, manifest->entry, "scene.version",
            "must be %d, the supported scene schema", BOAT_SCENE_VERSION);
    Check check = { d, manifest->entry };
    if (!validate(&check, data))
        return BOAT_PLUGIN_SCHEMA;

    BoatPluginVoyage candidate = { .manifest = *manifest, .migrates = data->migrates };
    if (data->has_scene) {
        candidate.has_scene = true;
        candidate.scene = data->scene;
        BoatSceneDiagnostic scene;
        if (!boat_scene_validate(&candidate.scene, &scene))
            return diagnose(
                d, BOAT_PLUGIN_SCHEMA, manifest->entry, scene.field, "%s", scene.message);
        candidate.scene_digest = boat_scene_digest(&candidate.scene);
    }
    BoatVoyageRoute *route = &candidate.route;
    route->shelter_count = data->shelter_count;
    for (unsigned i = 0; i < data->shelter_count; ++i) {
        const BoatPluginShelterData *s = &data->shelters[i];
        memcpy(candidate.shelter_ids[i], s->id, sizeof(s->id));
        memcpy(candidate.shelter_names[i], s->name, sizeof(s->name));
        route->shelters[i].position = (Vector2) { (float)s->x, (float)s->z };
        route->shelters[i].heading = (float)s->heading;
        route->shelters[i].radius = (float)s->radius;
    }
    bind(&candidate);
    route->arrival_centre = (Vector2) { (float)data->arrival.x, (float)data->arrival.z };
    route->arrival_radius = (float)data->arrival.radius;
    route->arrival_speed = (float)data->arrival.max_speed;
    // Stationary rough water; the voyage sets STARTED.
    route->exposure = (BoatStorm) { .centre = { (float)data->exposure.x, (float)data->exposure.z },
        .radius = (float)data->exposure.radius,
        .height = (float)data->exposure.height,
        .build = (float)data->exposure.build };
    route->exposed_fraction = (float)data->exposed_fraction;
    route->weather_range = (float)data->weather_range;
    candidate.digest = boat_plugin_route_digest(route);
    boat_plugin_voyage_copy(out, &candidate);
    BoatPluginCost cost = d->cost;
    *d = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_OK, .cost = cost };
    return BOAT_PLUGIN_OK;
}

// Staging -------------------------------------------------------------------------------------

static bool same_point(Vector2 a, Vector2 b) { return a.x == b.x && a.y == b.y; }

// Authored berth geometry is checked on the compiled map. The margins match the native contact
// envelope (0.45 m lateral/0.9 m longitudinal) and add a small map-water margin.
static void berth_box(const BoatSceneBerth *b, BoatModelSpec spec, Vector2 *length, Vector2 *beam,
    Vector2 *centre, float *half_length, float *half_beam)
{
    *length = (Vector2) { sinf(b->heading), -cosf(b->heading) };
    *beam = (Vector2) { cosf(b->heading), sinf(b->heading) };
    *centre = (Vector2) { b->x, b->z };
    *half_length = spec.length * .5f + .9f;
    *half_beam = spec.beam * .5f + .45f;
}

static bool box_water(const BoatMap *map, Vector2 centre, Vector2 length, Vector2 beam,
    float half_length, float half_beam, float clearance)
{
    for (int end = -1; end <= 1; ++end)
        for (int side = -1; side <= 1; ++side) {
            Vector2 p = { centre.x + length.x * end * half_length + beam.x * side * half_beam,
                centre.y + length.y * end * half_length + beam.y * side * half_beam };
            if (!boat_map_water(map, p, clearance))
                return false;
        }
    return true;
}

static float box_extent(
    Vector2 axis, Vector2 length, Vector2 beam, float half_length, float half_beam)
{
    return half_length * fabsf(axis.x * length.x + axis.y * length.y)
        + half_beam * fabsf(axis.x * beam.x + axis.y * beam.y);
}

static bool box_overlap(Vector2 a, Vector2 al, Vector2 ab, float ahl, float ahb, Vector2 b,
    Vector2 bl, Vector2 bb, float bhl, float bhb, float margin)
{
    const Vector2 axes[] = { al, ab, bl, bb };
    Vector2 delta = { b.x - a.x, b.y - a.y };
    for (int i = 0; i < 4; ++i) {
        float reach = box_extent(axes[i], al, ab, ahl, ahb) + box_extent(axes[i], bl, bb, bhl, bhb)
            + margin;
        if (fabsf(delta.x * axes[i].x + delta.y * axes[i].y) > reach)
            return false;
    }
    return true;
}

static bool box_contains(Vector2 centre, Vector2 length, Vector2 beam, float half_length,
    float half_beam, Vector2 point, float margin)
{
    Vector2 delta = { point.x - centre.x, point.y - centre.y };
    return fabsf(delta.x * length.x + delta.y * length.y) < half_length + margin
        && fabsf(delta.x * beam.x + delta.y * beam.y) < half_beam + margin;
}

static void module_box(const BoatSceneModule *m, Vector2 *length, Vector2 *beam, Vector2 *centre,
    float *half_length, float *half_beam)
{
    WaterfrontModuleSpec spec = waterfront_harbor_module_spec(m->kind);
    *length = (Vector2) { sinf(m->heading), -cosf(m->heading) };
    *beam = (Vector2) { cosf(m->heading), sinf(m->heading) };
    *centre = (Vector2) { m->x, m->z };
    *half_length = spec.length * .5f;
    *half_beam = spec.beam * .5f;
}

static BoatPluginStatus check_scene_modules(
    const BoatPluginVoyage *voyage, const BoatMap *map, BoatPluginDiagnostic *d)
{
    const BoatSceneDefinition *scene = &voyage->scene;
    Vector2 centre[BOAT_SCENE_MODULES], length[BOAT_SCENE_MODULES], beam[BOAT_SCENE_MODULES];
    float half_length[BOAT_SCENE_MODULES], half_beam[BOAT_SCENE_MODULES];
    bool solid[BOAT_SCENE_MODULES] = { false };
    for (unsigned i = 0; i < scene->module_count; ++i) {
        const BoatSceneModule *m = &scene->modules[i];
        char path[32];
        snprintf(path, sizeof(path), "scene.marina.modules[%u]", i + 1);
        if (m->kind == BOAT_SCENE_MODULE_GANGWAY) {
            const BoatSceneModule *target = boat_scene_find_module(scene, m->attaches);
            if (!target)
                return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                    "attaches to a missing pontoon");
            Vector2 landing = { target->x + 1.175f * cosf(target->heading),
                target->z + 1.175f * sinf(target->heading) };
            float span = hypotf(m->anchor_x - landing.x, m->anchor_z - landing.y);
            if (span < BOAT_SCENE_GANGWAY_MIN_LENGTH || span > BOAT_SCENE_GANGWAY_MAX_LENGTH)
                return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                    "gangway span %.1f m is outside 2..20 m", span);
            if (boat_map_water(map, (Vector2) { m->anchor_x, m->anchor_z }, 0))
                return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                    "gangway shore anchor is not land");
            if (!boat_map_water(map, landing, 0))
                return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                    "gangway landing is not water");
            continue;
        }
        module_box(m, &length[i], &beam[i], &centre[i], &half_length[i], &half_beam[i]);
        solid[i] = true;
        if (m->kind == BOAT_SCENE_MODULE_QUAY) {
            if (boat_map_water(map, centre[i], 0))
                return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                    "quay module must stand on land");
        } else if (!box_water(
                       map, centre[i], length[i], beam[i], half_length[i], half_beam[i], .5f))
            return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                "pontoon module is not open water of the compiled map");
        for (unsigned b = 0; b < scene->berth_count; ++b) {
            const BoatSceneBerth *berth = &scene->berths[b];
            BoatModelSpec spec = boat_scene_berth_spec((BoatSceneBerthRecipe)berth->recipe);
            Vector2 bc, bl, bb;
            float bhl, bhb;
            berth_box(berth, spec, &bl, &bb, &bc, &bhl, &bhb);
            if (box_overlap(centre[i], length[i], beam[i], half_length[i], half_beam[i], bc, bl, bb,
                    bhl, bhb, .3f))
                return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                    "overlaps scene.marina.berths[%u]", b + 1);
        }
    }
    for (unsigned b = 0; b < scene->berth_count; ++b) {
        const BoatSceneBerth *berth = &scene->berths[b];
        if (berth->mooring != BOAT_SCENE_MOORING_PONTOON)
            continue;
        float best = INFINITY;
        for (unsigned i = 0; i < scene->module_count; ++i) {
            const BoatSceneModule *m = &scene->modules[i];
            if (m->kind != BOAT_SCENE_MODULE_PONTOON && m->kind != BOAT_SCENE_MODULE_PONTOON_SMALL)
                continue;
            best = fminf(best, hypotf(berth->x - m->x, berth->z - m->z));
        }
        if (best > 20) {
            char path[32];
            snprintf(path, sizeof(path), "scene.marina.berths[%u]", b + 1);
            return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                "mooring is pontoon but no authored pontoon is within rope reach");
        }
    }
    for (unsigned i = 0; i < scene->module_count; ++i)
        for (unsigned j = i + 1; j < scene->module_count; ++j) {
            if (!solid[i] || !solid[j])
                continue;
            char path[32];
            snprintf(path, sizeof(path), "scene.marina.modules[%u]", j + 1);
            if (box_overlap(centre[i], length[i], beam[i], half_length[i], half_beam[i], centre[j],
                    length[j], beam[j], half_length[j], half_beam[j], .3f))
                return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                    "overlaps scene.marina.modules[%u]", i + 1);
        }
    return BOAT_PLUGIN_OK;
}

// L3.2: sample every authored craft over the combined river timetable. The hull envelope is
// inflated by half a sample step of travel, so a thin diagonal cannot slip between samples.
static BoatPluginStatus check_scene_traffic(
    const BoatPluginVoyage *voyage, const BoatMap *map, BoatPluginDiagnostic *d)
{
    const BoatSceneDefinition *scene = &voyage->scene;
    if (!scene->traffic_present || !scene->traffic.actor_count)
        return BOAT_PLUGIN_OK;
    const double step = .25, horizon = 4160; // The four river periods repeat together here.
    int samples = (int)(horizon / step);
    for (unsigned i = 0; i < scene->traffic.actor_count; ++i) {
        const WaterfrontRouteActor *a = &scene->traffic.actors[i];
        // Retained actors keep their compiled schedule; authored craft are sampled against them
        // below, so a retained actor never needs its own full sweep.
        if (a->retain != 0xff)
            continue;
        BoatModelSpec spec = waterfront_traffic_spec_routes(&scene->traffic, (int)i);
        char path[48];
        snprintf(path, sizeof(path), "scene.traffic.vessels[%u]", i + 1);
        for (int s = 0; s <= samples; ++s) {
            double time = s * step;
            WaterfrontVesselMotion m
                = waterfront_traffic_motion_routes(&scene->traffic, (int)i, time);
            float speed = Vector3Length(m.velocity);
            float margin = .5f + speed * (float)step * .5f;
            // Route positions are renderer X/Z; the map and marina are simulation X/Z.
            Vector2 centre = { -m.position.x, m.position.z };
            Vector2 length = { sinf(m.heading), -cosf(m.heading) };
            Vector2 beam = { cosf(m.heading), sinf(m.heading) };
            float half_length = spec.length * .5f + margin;
            float half_beam = spec.beam * .5f + margin;
            // An authored craft needs new map/marina clearance and must clear retained craft.
            if (!box_water(map, centre, length, beam, half_length, half_beam, .5f))
                return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                    "swept hull is not open water of the compiled map");
            if (scene->entrance_present
                && box_contains(centre, length, beam, half_length, half_beam,
                    (Vector2) { scene->entrance_x, scene->entrance_z }, .5f))
                return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                    "crosses the declared marina entrance");
            for (unsigned b = 0; b < scene->berth_count; ++b) {
                const BoatSceneBerth *berth = &scene->berths[b];
                BoatModelSpec bs = boat_scene_berth_spec((BoatSceneBerthRecipe)berth->recipe);
                Vector2 bc, bl, bb;
                float bhl, bhb;
                berth_box(berth, bs, &bl, &bb, &bc, &bhl, &bhb);
                if (box_overlap(centre, length, beam, half_length, half_beam, bc, bl, bb, bhl, bhb,
                        .3f))
                    return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                        "crosses scene.marina.berths[%u]", b + 1);
            }
            for (unsigned m = 0; m < scene->module_count; ++m) {
                if (scene->modules[m].kind == BOAT_SCENE_MODULE_GANGWAY)
                    continue;
                Vector2 mc, ml, mb;
                float mhl, mhb;
                module_box(&scene->modules[m], &ml, &mb, &mc, &mhl, &mhb);
                if (box_overlap(centre, length, beam, half_length, half_beam, mc, ml, mb, mhl, mhb,
                        .3f))
                    return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                        "crosses scene.marina.modules[%u]", m + 1);
            }
            for (unsigned j = i + 1; j < scene->traffic.actor_count; ++j) {
                // Two retained compiled actors keep their existing relative schedules.
                if (scene->traffic.actors[j].retain != 0xff)
                    continue;
                BoatModelSpec os = waterfront_traffic_spec_routes(&scene->traffic, (int)j);
                WaterfrontVesselMotion om
                    = waterfront_traffic_motion_routes(&scene->traffic, (int)j, time);
                Vector2 oc = { -om.position.x, om.position.z };
                Vector2 ol = { sinf(om.heading), -cosf(om.heading) };
                Vector2 ob = { cosf(om.heading), sinf(om.heading) };
                if (box_overlap(centre, length, beam, half_length, half_beam, oc, ol, ob,
                        os.length * .5f + .5f, os.beam * .5f + .5f, .3f))
                    return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                        "crosses scene.traffic.vessels[%u]", j + 1);
            }
            // Compare against the compiled service as well: a present list replaces it only
            // after proving that the retained timetable does not intersect the authored craft.
            for (int j = 0; a->retain == 0xff && j < WATERFRONT_TRAFFIC_ALL; ++j) {
                BoatModelSpec os = waterfront_traffic_spec(j);
                WaterfrontVesselMotion om = waterfront_traffic_motion(j, time);
                Vector2 oc = { -om.position.x, om.position.z };
                Vector2 ol = { sinf(om.heading), -cosf(om.heading) };
                Vector2 ob = { cosf(om.heading), sinf(om.heading) };
                if (box_overlap(centre, length, beam, half_length, half_beam, oc, ol, ob,
                        os.length * .5f + .5f, os.beam * .5f + .5f, .3f))
                    return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                        "crosses compiled traffic vessel %d", j + 1);
            }
        }
        (void)a;
    }
    return BOAT_PLUGIN_OK;
}

static BoatPluginStatus check_scene_location(const BoatPluginVoyage *voyage,
    const BoatWorldDefinition *location, const BoatMap *map, BoatPluginDiagnostic *d)
{
    const BoatSceneDefinition *scene = &voyage->scene;
    if (!voyage->has_scene
        || (!scene->berths_present && !scene->modules_present && !scene->traffic_present))
        return BOAT_PLUGIN_OK;
    if (!location->mapped)
        return diagnose(d, BOAT_PLUGIN_MANIFEST, "plugin.json", "location",
            "scene marina requires a mapped location");
    if (scene->entrance_present
        && !boat_map_water(
            map, (Vector2) { scene->entrance_x, scene->entrance_z }, scene->entrance_width * .5f))
        return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, "scene.marina.entrance",
            "is not open water with half its declared width clear");
    Vector2 centre[BOAT_SCENE_BERTHS], length[BOAT_SCENE_BERTHS], beam[BOAT_SCENE_BERTHS];
    float half_length[BOAT_SCENE_BERTHS], half_beam[BOAT_SCENE_BERTHS];
    for (unsigned i = 0; i < scene->berth_count; ++i) {
        const BoatSceneBerth *b = &scene->berths[i];
        BoatModelSpec spec = boat_scene_berth_spec((BoatSceneBerthRecipe)b->recipe);
        berth_box(b, spec, &length[i], &beam[i], &centre[i], &half_length[i], &half_beam[i]);
        char path[32];
        snprintf(path, sizeof(path), "scene.marina.berths[%u]", i + 1);
        if (!box_water(map, centre[i], length[i], beam[i], half_length[i], half_beam[i], .5f))
            return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                "hull and mooring excursion are not open water of the %s map", location->name);
        if (scene->entrance_present
            && box_contains(centre[i], length[i], beam[i], half_length[i], half_beam[i],
                (Vector2) { scene->entrance_x, scene->entrance_z }, .5f))
            return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                "overlaps the declared marina entrance");
        // The compiled spawn and every recovery shelter must offer a usable restart.
        if (box_contains(
                centre[i], length[i], beam[i], half_length[i], half_beam[i], location->spawn, 4))
            return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                "blocks the location's spawn and restart");
        for (unsigned s = 0; s < voyage->route.shelter_count; ++s)
            if (box_contains(centre[i], length[i], beam[i], half_length[i], half_beam[i],
                    voyage->route.shelters[s].position, 4))
                return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                    "blocks voyage.shelters[%u] and its recovery", s + 1);
    }
    for (unsigned i = 0; i < scene->berth_count; ++i)
        for (unsigned j = i + 1; j < scene->berth_count; ++j) {
            char path[32];
            snprintf(path, sizeof(path), "scene.marina.berths[%u]", j + 1);
            if (box_overlap(centre[i], length[i], beam[i], half_length[i], half_beam[i], centre[j],
                    length[j], beam[j], half_length[j], half_beam[j], .45f))
                return diagnose(d, BOAT_PLUGIN_SCHEMA, voyage->manifest.entry, path,
                    "overlaps scene.marina.berths[%u] with no hull clearance", i + 1);
        }
    if (scene->modules_present && check_scene_modules(voyage, map, d) != BOAT_PLUGIN_OK)
        return d->status;
    if (check_scene_traffic(voyage, map, d) != BOAT_PLUGIN_OK)
        return d->status;
    return BOAT_PLUGIN_OK;
}

BoatPluginStatus boat_plugin_check_location(const BoatPluginVoyage *voyage,
    const BoatWorldDefinition *location, const BoatMap *map, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginDiagnostic scratch;
    BoatPluginDiagnostic *d = diagnostic ? diagnostic : &scratch;
    const char *file = voyage->manifest.entry;
    if (strcmp(voyage->manifest.location, location->name) != 0)
        return diagnose(d, BOAT_PLUGIN_MANIFEST, "plugin.json", "location",
            "is \"%s\"; this session is at \"%s\"", voyage->manifest.location, location->name);
    const BoatVoyageRoute *route = &voyage->route;
    if (!same_point(route->shelters[0].position, location->spawn))
        return diagnose(d, BOAT_PLUGIN_SCHEMA, file, "voyage.shelters[1]",
            "must be the location's start, x = %g, z = %g", location->spawn.x, location->spawn.y);
    // Room for the hull at a shelter, and more for arriving and turning in the harbour.
    for (unsigned i = 0; i < route->shelter_count; ++i)
        if (!boat_map_water(map, route->shelters[i].position, 8)) {
            char at[32];
            snprintf(at, sizeof(at), "voyage.shelters[%u]", i + 1);
            return diagnose(d, BOAT_PLUGIN_SCHEMA, file, at,
                "is not open water of the %s map with 8 m clear", location->name);
        }
    if (!boat_map_water(map, route->arrival_centre, 20))
        return diagnose(d, BOAT_PLUGIN_SCHEMA, file, "voyage.arrival",
            "is not open water of the %s map with 20 m clear", location->name);
    if (check_scene_location(voyage, location, map, d) != BOAT_PLUGIN_OK)
        return d->status;
    BoatPluginCost cost = d->cost;
    *d = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_OK, .cost = cost };
    return BOAT_PLUGIN_OK;
}

const char *boat_plugin_change_name(BoatPluginChange change)
{
    static const char *const names[] = { "unchanged", "renamed", "migrate objective",
        "restart voyage", "restart session", "incompatible" };
    return (unsigned)change < sizeof(names) / sizeof(names[0]) ? names[change] : "unknown";
}

static bool same_storm(BoatStorm a, BoatStorm b)
{
    return same_point(a.centre, b.centre) && same_point(a.velocity, b.velocity)
        && a.radius == b.radius && a.height == b.height && a.started == b.started
        && a.build == b.build;
}

BoatPluginChange boat_plugin_classify(
    const BoatPluginVoyage *active, const BoatPluginVoyage *candidate)
{
    const BoatPluginManifest *m = &active->manifest, *n = &candidate->manifest;
    if (strcmp(m->id, n->id) || strcmp(m->kind, n->kind) || strcmp(m->location, n->location)
        || m->api != n->api)
        return BOAT_PLUGIN_INCOMPATIBLE;
    const BoatVoyageRoute *a = &active->route, *b = &candidate->route;
    if (!same_storm(a->exposure, b->exposure) || a->weather_range != b->weather_range)
        return BOAT_PLUGIN_RESTART_SESSION;
    // L1.2 has only an empty envelope, so it changes no runtime content. Once L2 authors
    // berths/modules, any differing authored layout is a physical session restart.
    if (boat_scene_has_content(&active->scene) || boat_scene_has_content(&candidate->scene)) {
        if (active->has_scene != candidate->has_scene
            || active->scene_digest != candidate->scene_digest)
            return BOAT_PLUGIN_RESTART_SESSION;
    }
    bool same = a->shelter_count == b->shelter_count
        && same_point(a->arrival_centre, b->arrival_centre)
        && a->arrival_radius == b->arrival_radius && a->arrival_speed == b->arrival_speed
        && a->exposed_fraction == b->exposed_fraction;
    for (unsigned i = 0; same && i < a->shelter_count; ++i) {
        const BoatVoyageShelter *s = &a->shelters[i], *t = &b->shelters[i];
        same = !strcmp(active->shelter_ids[i], candidate->shelter_ids[i])
            && same_point(s->position, t->position) && s->heading == t->heading
            && s->radius == t->radius;
    }
    if (!same)
        return BOAT_PLUGIN_RESTART_VOYAGE;
    bool renamed = strcmp(m->name, n->name) != 0;
    for (unsigned i = 0; i < a->shelter_count; ++i)
        renamed |= strcmp(active->shelter_names[i], candidate->shelter_names[i]) != 0;
    return renamed ? BOAT_PLUGIN_RENAMED : BOAT_PLUGIN_UNCHANGED;
}
