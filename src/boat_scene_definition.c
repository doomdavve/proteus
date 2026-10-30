#include "boat_scene_definition.h"

#include <math.h>
#include <string.h>

static bool bounded_text(const char *value, size_t capacity)
{
    if (!value[0])
        return false;
    for (size_t i = 0; i < capacity; ++i) {
        if (!value[i])
            return true;
        unsigned char c = (unsigned char)value[i];
        if (c < 32 || c >= 127)
            return false;
    }
    return false; // unterminated
}

bool boat_scene_identifier(const char *text)
{
    if (!(*text >= 'a' && *text <= 'z'))
        return false;
    for (; *text; ++text)
        if (!((*text >= 'a' && *text <= 'z') || (*text >= '0' && *text <= '9') || *text == '_'))
            return false;
    return true;
}

static bool finite_in(float value, float low, float high)
{
    return isfinite(value) && value >= low && value <= high;
}

static void copy_text(char *out, size_t capacity, const char *value)
{
    size_t n = 0;
    while (value[n] && n + 1 < capacity) {
        out[n] = value[n];
        ++n;
    }
    out[n] = 0;
}

static bool fail(BoatSceneDiagnostic *d, const char *field, const char *message)
{
    if (d) {
        copy_text(d->field, sizeof(d->field), field);
        copy_text(d->message, sizeof(d->message), message);
    }
    return false;
}

// Build "PREFIX[INDEX]SUFFIX" without stdio or unbounded formats.
static void indexed(
    char *out, size_t capacity, const char *prefix, unsigned index, const char *suffix)
{
    size_t n = 1;
    out[0] = '\0';
    for (const char *p = prefix; *p && n + 1 < capacity; ++p) {
        out[n - 1] = *p;
        out[n++] = 0;
    }
    if (n + 1 < capacity) {
        out[n - 1] = '[';
        out[n++] = 0;
    }
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + index % 10);
        index /= 10;
    } while (index && count < sizeof(digits));
    while (count && n + 1 < capacity) {
        out[n - 1] = digits[--count];
        out[n++] = 0;
    }
    if (n + 1 < capacity) {
        out[n - 1] = ']';
        out[n++] = 0;
    }
    for (const char *p = suffix; *p && n + 1 < capacity; ++p) {
        out[n - 1] = *p;
        out[n++] = 0;
    }
}

static bool unique(const BoatSceneDefinition *scene, unsigned berth, unsigned module,
    const char *id, BoatSceneDiagnostic *d, const char *field)
{
    for (unsigned i = 0; i < berth; ++i)
        if (!strcmp(scene->berths[i].id, id))
            return fail(d, field, "repeats an existing scene ID");
    for (unsigned i = 0; i < module; ++i)
        if (!strcmp(scene->modules[i].id, id))
            return fail(d, field, "repeats an existing scene ID");
    return true;
}

bool boat_scene_validate(const BoatSceneDefinition *scene, BoatSceneDiagnostic *diagnostic)
{
    char path[64];
    if (!scene)
        return fail(diagnostic, "", "is missing");
    if (scene->version != BOAT_SCENE_VERSION)
        return fail(diagnostic, "scene.version", "must be 1");
    if (scene->berth_count > BOAT_SCENE_BERTHS)
        return fail(diagnostic, "scene.marina.berths", "exceeds the seven-berth capacity");
    if (scene->module_count > BOAT_SCENE_MODULES)
        return fail(diagnostic, "scene.marina.modules", "exceeds the four-module capacity");
    if (!scene->berths_present && scene->berth_count)
        return fail(diagnostic, "scene.marina.berths", "is absent but has entries");
    if (!scene->modules_present && scene->module_count)
        return fail(diagnostic, "scene.marina.modules", "is absent but has entries");
    if (scene->berths_present && scene->berth_count && !scene->entrance_present)
        return fail(diagnostic, "scene.marina.entrance", "is required when berths are present");
    if (scene->entrance_present
        && (!finite_in(scene->entrance_x, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT)
            || !finite_in(scene->entrance_z, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT)))
        return fail(diagnostic, "scene.marina.entrance", "must be finite within 20000 m");
    if (scene->entrance_present
        && !finite_in(
            scene->entrance_width, BOAT_SCENE_ENTRANCE_MIN_WIDTH, BOAT_SCENE_ENTRANCE_MAX_WIDTH))
        return fail(diagnostic, "scene.marina.entrance.width", "must be 4..40 m");
    for (unsigned i = 0; i < scene->berth_count; ++i) {
        const BoatSceneBerth *b = &scene->berths[i];
        indexed(path, sizeof(path), "scene.marina.berths", i + 1, "");
        char at[64];
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".id");
        if (!bounded_text(b->id, sizeof(b->id)) || !boat_scene_identifier(b->id))
            return fail(diagnostic, at, "must be a-z, 0-9 or '_', starting with a letter");
        if (!unique(scene, i, 0, b->id, diagnostic, at))
            return false;
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".recipe");
        if (b->recipe >= BOAT_SCENE_BERTH_RECIPES)
            return fail(diagnostic, at, "is not a registered berth recipe");
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".mooring");
        if (b->mooring > BOAT_SCENE_MOORING_PONTOON)
            return fail(diagnostic, at, "must be pontoon or none");
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".x");
        if (!finite_in(b->x, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT))
            return fail(diagnostic, at, "must be finite within 20000 m");
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".z");
        if (!finite_in(b->z, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT))
            return fail(diagnostic, at, "must be finite within 20000 m");
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".heading");
        if (!finite_in(b->heading, -6.28318531f, 6.28318531f))
            return fail(diagnostic, at, "must be finite within one turn");
    }
    if (!scene->traffic_present && scene->traffic.actor_count)
        return fail(diagnostic, "scene.traffic.vessels", "is absent but has entries");
    if (scene->traffic_present) {
        WaterfrontRouteDiagnostic route;
        if (!waterfront_route_validate(&scene->traffic, &route))
            return fail(diagnostic, route.field, route.message);
        for (unsigned i = 0; i < scene->traffic.actor_count; ++i)
            if (scene->traffic.actors[i].retain == 0xff
                && !(scene->traffic.actors[i].speed > 0))
                return fail(diagnostic, "scene.traffic.vessels",
                    "an authored craft needs a positive speed");
    }
    for (unsigned i = 0; i < scene->module_count; ++i) {
        const BoatSceneModule *m = &scene->modules[i];
        indexed(path, sizeof(path), "scene.marina.modules", i + 1, "");
        char at[64];
        indexed(at, sizeof(at), "scene.marina.modules", i + 1, ".id");
        if (!bounded_text(m->id, sizeof(m->id)) || !boat_scene_identifier(m->id))
            return fail(diagnostic, at, "must be a-z, 0-9 or '_', starting with a letter");
        if (!unique(scene, scene->berth_count, i, m->id, diagnostic, at))
            return false;
        indexed(at, sizeof(at), "scene.marina.modules", i + 1, ".kind");
        if (m->kind >= BOAT_SCENE_MODULE_KINDS)
            return fail(diagnostic, at, "is not a registered module kind");
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".x");
        if (!finite_in(m->x, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT))
            return fail(diagnostic, at, "must be finite within 20000 m");
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".z");
        if (!finite_in(m->z, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT))
            return fail(diagnostic, at, "must be finite within 20000 m");
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".heading");
        if (!finite_in(m->heading, -6.28318531f, 6.28318531f))
            return fail(diagnostic, at, "must be finite within one turn");
        if (m->kind == BOAT_SCENE_MODULE_GANGWAY) {
            indexed(at, sizeof(at), "scene.marina.modules", i + 1, ".anchor");
            if (!finite_in(m->anchor_x, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT)
                || !finite_in(m->anchor_z, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT))
                return fail(diagnostic, at, "must be finite within 20000 m");
            indexed(at, sizeof(at), "scene.marina.modules", i + 1, ".attaches");
            if (!bounded_text(m->attaches, sizeof(m->attaches))
                || !boat_scene_identifier(m->attaches))
                return fail(diagnostic, at, "must name a pontoon module");
            const BoatSceneModule *target = boat_scene_find_module(scene, m->attaches);
            if (!target
                || (target->kind != BOAT_SCENE_MODULE_PONTOON
                    && target->kind != BOAT_SCENE_MODULE_PONTOON_SMALL))
                return fail(diagnostic, at, "must name a pontoon or pontoon_small module");
        } else if (m->attaches[0])
            return fail(diagnostic, at, "is only valid for a gangway");
    }
    return true;
}

bool boat_scene_has_content(const BoatSceneDefinition *scene)
{
    return scene
        && (scene->berths_present || scene->modules_present || scene->entrance_present
            || scene->traffic_present || scene->berth_count || scene->module_count
            || scene->traffic.actor_count);
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
    fold(hash, name, strlen(name) + 1);
    uint32_t bits;
    memcpy(&bits, &number, sizeof(bits));
    unsigned char bytes[4] = { (unsigned char)bits, (unsigned char)(bits >> 8),
        (unsigned char)(bits >> 16), (unsigned char)(bits >> 24) };
    fold(hash, bytes, sizeof(bytes));
}

static void count(uint64_t *hash, const char *name, unsigned number)
{
    fold(hash, name, strlen(name) + 1);
    unsigned char bytes[4] = { (unsigned char)number, (unsigned char)(number >> 8),
        (unsigned char)(number >> 16), (unsigned char)(number >> 24) };
    fold(hash, bytes, sizeof(bytes));
}

uint64_t boat_scene_digest(const BoatSceneDefinition *scene)
{
    uint64_t hash = 14695981039346656037ull;
    if (!scene)
        return hash;
    count(&hash, "version", scene->version);
    unsigned char present[3] = { (unsigned char)scene->entrance_present,
        (unsigned char)scene->berths_present, (unsigned char)scene->modules_present };
    fold(&hash, "present", 8);
    fold(&hash, present, sizeof(present));
    if (scene->entrance_present) {
        value(&hash, "entrance.x", scene->entrance_x);
        value(&hash, "entrance.z", scene->entrance_z);
        value(&hash, "entrance.width", scene->entrance_width);
    }
    count(&hash, "berths", scene->berth_count);
    for (unsigned i = 0; i < scene->berth_count; ++i) {
        const BoatSceneBerth *b = &scene->berths[i];
        fold(&hash, "berth.id", 9);
        fold(&hash, b->id, strlen(b->id) + 1);
        count(&hash, "berth.recipe", b->recipe);
        count(&hash, "berth.mooring", b->mooring);
        value(&hash, "berth.x", b->x);
        value(&hash, "berth.z", b->z);
        value(&hash, "berth.heading", b->heading);
    }
    count(&hash, "modules", scene->module_count);
    for (unsigned i = 0; i < scene->module_count; ++i) {
        const BoatSceneModule *m = &scene->modules[i];
        fold(&hash, "module.id", 10);
        fold(&hash, m->id, strlen(m->id) + 1);
        count(&hash, "module.kind", m->kind);
        value(&hash, "module.x", m->x);
        value(&hash, "module.z", m->z);
        value(&hash, "module.heading", m->heading);
        value(&hash, "module.anchor.x", m->anchor_x);
        value(&hash, "module.anchor.z", m->anchor_z);
        fold(&hash, "module.attaches", 16);
        fold(&hash, m->attaches, strlen(m->attaches) + 1);
    }
    // Keep absent-traffic digests identical to L1/L2 revisions; presence itself matters
    // because a present empty list disables the compiled collection.
    if (scene->traffic_present) {
        uint64_t traffic = waterfront_route_digest(&scene->traffic);
        fold(&hash, "traffic", 8);
        count(&hash, "traffic.actors", scene->traffic.actor_count);
        fold(&hash, &traffic, sizeof(traffic));
    }
    return hash;
}

const char *boat_scene_berth_recipe_name(BoatSceneBerthRecipe recipe)
{
    static const char *const names[BOAT_SCENE_BERTH_RECIPES]
        = { "skiff", "runabout", "sailboat", "cruiser", "speedboat", "sailboat_7m", "workboat" };
    return recipe < BOAT_SCENE_BERTH_RECIPES ? names[recipe] : "unknown";
}

bool boat_scene_berth_recipe_from_name(const char *name, BoatSceneBerthRecipe *recipe)
{
    for (unsigned i = 0; i < BOAT_SCENE_BERTH_RECIPES; ++i)
        if (!strcmp(name, boat_scene_berth_recipe_name((BoatSceneBerthRecipe)i))) {
            *recipe = (BoatSceneBerthRecipe)i;
            return true;
        }
    return false;
}

const char *boat_scene_module_kind_name(BoatSceneModuleKind kind)
{
    static const char *const names[BOAT_SCENE_MODULE_KINDS]
        = { "pontoon", "pontoon_small", "gangway", "quay" };
    return kind < BOAT_SCENE_MODULE_KINDS ? names[kind] : "unknown";
}

bool boat_scene_module_kind_from_name(const char *name, BoatSceneModuleKind *kind)
{
    for (unsigned i = 0; i < BOAT_SCENE_MODULE_KINDS; ++i)
        if (!strcmp(name, boat_scene_module_kind_name((BoatSceneModuleKind)i))) {
            *kind = (BoatSceneModuleKind)i;
            return true;
        }
    return false;
}

const char *boat_scene_mooring_name(BoatSceneMooring mooring)
{
    static const char *const names[] = { "none", "pontoon", "quay" };
    return mooring <= BOAT_SCENE_MOORING_QUAY ? names[mooring] : "unknown";
}

bool boat_scene_mooring_from_name(const char *name, BoatSceneMooring *mooring)
{
    for (unsigned i = 0; i <= BOAT_SCENE_MOORING_PONTOON; ++i)
        if (!strcmp(name, boat_scene_mooring_name((BoatSceneMooring)i))) {
            *mooring = (BoatSceneMooring)i;
            return true;
        }
    return false;
}

BoatModelKind boat_scene_berth_kind(BoatSceneBerthRecipe recipe)
{
    static const BoatModelKind kinds[BOAT_SCENE_BERTH_RECIPES]
        = { BOAT_MODEL_SKIFF, BOAT_MODEL_RUNABOUT, BOAT_MODEL_SAILBOAT, BOAT_MODEL_CRUISER,
              BOAT_MODEL_SPEEDBOAT, BOAT_MODEL_SAILBOAT, BOAT_MODEL_WORKBOAT };
    return recipe < BOAT_SCENE_BERTH_RECIPES ? kinds[recipe] : BOAT_MODEL_SKIFF;
}

BoatModelSpec boat_scene_berth_spec(BoatSceneBerthRecipe recipe)
{
    if (recipe >= BOAT_SCENE_BERTH_RECIPES)
        return (BoatModelSpec) { 0 };
    BoatModelSpec spec = boat_model_preset(boat_scene_berth_kind(recipe));
    spec.fenders = true;
    if (recipe == BOAT_SCENE_BERTH_SAILBOAT_7M) {
        spec.length = 7;
        spec.beam = 2.35f;
        spec.mast_height = 7.5f;
        spec.cabin_length = 2.5f;
        spec.hull = (Color) { 229, 223, 203, 255 };
        spec.trim = (Color) { 75, 117, 108, 255 };
    }
    return spec;
}

const BoatSceneBerth *boat_scene_find_berth(const BoatSceneDefinition *scene, const char *id)
{
    if (!scene || !id)
        return NULL;
    for (unsigned i = 0; i < scene->berth_count; ++i)
        if (!strcmp(scene->berths[i].id, id))
            return &scene->berths[i];
    return NULL;
}

const BoatSceneModule *boat_scene_find_module(const BoatSceneDefinition *scene, const char *id)
{
    if (!scene || !id)
        return NULL;
    for (unsigned i = 0; i < scene->module_count; ++i)
        if (!strcmp(scene->modules[i].id, id))
            return &scene->modules[i];
    return NULL;
}
