#include "core/boat_scene_definition.h"

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

// Build "PREFIX.KEY" without stdio or unbounded formats.
static void dotted(char *out, size_t capacity, const char *prefix, const char *key)
{
    size_t n = 0;
    while (*prefix && n + 1 < capacity)
        out[n++] = *prefix++;
    if (n + 1 < capacity)
        out[n++] = '.';
    while (*key && n + 1 < capacity)
        out[n++] = *key++;
    out[n] = 0;
}

static bool unique(const BoatSceneDefinition *scene, unsigned berth, unsigned module,
    const char *id, BoatSceneDiagnostic *d, const char *field)
{
    for (unsigned i = 0; i < berth; ++i)
        if (!strcmp(scene->berths[i].id, id))
            return fail(d, field, "repeats an existing scene ID");
    for (unsigned i = 0; i < module; ++i)
        if (!strncmp(scene->modules[i].id, id, BOAT_SCENE_TEXT))
            return fail(d, field, "repeats an existing scene ID");
    return true;
}

static bool joined_edges(const BoatSceneModule *parent, const BoatSceneModule *child)
{
    float angle = child->heading - parent->heading;
    float c = fabsf(cosf(angle)), s = fabsf(sinf(angle));
    if (fminf(c, s) > .001f)
        return false;
    float dx = child->x - parent->x, dz = child->z - parent->z;
    float x = fabsf(dx * cosf(parent->heading) + dz * sinf(parent->heading));
    float z = fabsf(-dx * sinf(parent->heading) + dz * cosf(parent->heading));
    float w = parent->width * .5f + (child->width * c + child->length * s) * .5f;
    float l = parent->length * .5f + (child->length * c + child->width * s) * .5f;
    return (fabsf(x - w) <= .01f && l - z >= .25f) || (fabsf(z - l) <= .01f && w - x >= .25f);
}

bool boat_scene_validate(const BoatSceneDefinition *scene, BoatSceneDiagnostic *diagnostic)
{
    char path[64];
    if (!scene)
        return fail(diagnostic, "", "is missing");
    if (scene->version != BOAT_SCENE_VERSION)
        return fail(diagnostic, "scene.version", "must be 3");
    if (scene->berth_count > BOAT_SCENE_BERTHS)
        return fail(diagnostic, "scene.marina.berths", "exceeds the 32-berth capacity");
    if (scene->module_count > BOAT_SCENE_MODULES)
        return fail(diagnostic, "scene.marina.modules", "exceeds the 64-module capacity");
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
        BoatModelSpec base = boat_scene_berth_spec((BoatSceneBerthRecipe)b->recipe);
        const BoatSceneAppearance *a = &b->appearance;
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".appearance.length");
        if (a->length != 0 && !finite_in(a->length, base.length * .75f, base.length * 1.25f))
            return fail(diagnostic, at, "must be finite, 0.75..1.25 times recipe length in metres");
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".appearance.beam");
        if (a->beam != 0 && !finite_in(a->beam, base.beam * .75f, base.beam * 1.25f))
            return fail(diagnostic, at, "must be finite, 0.75..1.25 times recipe beam in metres");
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".appearance.mast_height");
        if (a->mast_height != 0
            && (base.mast_height == 0 || !finite_in(a->mast_height, base.freeboard + 2, 12)))
            return fail(
                diagnostic, at, "requires a sailboat mast, freeboard + 2 through 12 metres");
        const Color colors[] = { a->hull, a->deck, a->trim };
        const char *const suffix[] = { ".appearance.hull", ".appearance.deck", ".appearance.trim" };
        for (unsigned c = 0; c < 3; ++c) {
            indexed(at, sizeof(at), "scene.marina.berths", i + 1, suffix[c]);
            if (colors[c].a != 255
                && (colors[c].a != 0 || colors[c].r || colors[c].g || colors[c].b))
                return fail(diagnostic, at, "must be opaque RGB or the all-zero inherited color");
        }
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".appearance.name");
        if (!boat_model_name_valid(a->name))
            return fail(diagnostic, at,
                "must be 1..16 A-Z, 0-9, space, period, hyphen or apostrophe; include a "
                "letter/digit");
        indexed(at, sizeof(at), "scene.marina.berths", i + 1, ".appearance");
        if (!boat_model_spec_valid(boat_scene_berth_resolve(b)))
            return fail(diagnostic, at, "resolved recipe violates native geometry limits");
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
            if (scene->traffic.actors[i].retain == 0xff && !(scene->traffic.actors[i].speed > 0))
                return fail(diagnostic, "scene.traffic.vessels",
                    "an authored craft needs a positive speed");
    }
    if (!scene->gulls_present && scene->gulls.flock_count)
        return fail(diagnostic, "scene.gulls.flocks", "is absent but has entries");
    if (scene->gulls_present) {
        WaterfrontFlockDiagnostic flock;
        if (!waterfront_flock_validate(&scene->gulls, &flock))
            return fail(diagnostic, flock.field, flock.message);
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
        indexed(at, sizeof(at), "scene.marina.modules", i + 1, ".x");
        if (!finite_in(m->x, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT))
            return fail(diagnostic, at, "must be finite within 20000 m");
        indexed(at, sizeof(at), "scene.marina.modules", i + 1, ".z");
        if (!finite_in(m->z, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT))
            return fail(diagnostic, at, "must be finite within 20000 m");
        indexed(at, sizeof(at), "scene.marina.modules", i + 1, ".heading");
        if (!finite_in(m->heading, -6.28318531f, 6.28318531f))
            return fail(diagnostic, at, "must be finite within one turn");
        if (!finite_in(m->length, .5f, 60) || !finite_in(m->width, .5f, 20)
            || !finite_in(m->deck_height, m->kind == BOAT_SCENE_MODULE_QUAY ? 3.8f : .25f,
                m->kind == BOAT_SCENE_MODULE_QUAY ? 8 : 2))
            return fail(diagnostic, path,
                "length .5..60 m, width .5..20 m, deck_height .25..2 m (quay: 3.8..8 m)");
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
            if (!target || fabsf(target->deck_height - m->deck_height) > .001f
                || target->kind == BOAT_SCENE_MODULE_GANGWAY
                || target->kind == BOAT_SCENE_MODULE_QUAY)
                return fail(diagnostic, at, "must name a floating module");
        } else if (m->attaches[0]) {
            indexed(at, sizeof(at), "scene.marina.modules", i + 1, ".attaches");
            if (!bounded_text(m->attaches, sizeof(m->attaches))
                || !boat_scene_identifier(m->attaches))
                return fail(diagnostic, at, "must name an earlier floating module");
            const BoatSceneModule *parent = boat_scene_find_module(scene, m->attaches);
            if (!parent || parent >= m || parent->kind == BOAT_SCENE_MODULE_QUAY
                || parent->kind == BOAT_SCENE_MODULE_GANGWAY || m->kind == BOAT_SCENE_MODULE_QUAY
                || fabsf(parent->deck_height - m->deck_height) > .001f)
                return fail(
                    diagnostic, at, "requires an earlier floating parent at the same deck height");
            if (!joined_edges(parent, m))
                return fail(diagnostic, at,
                    "must share at least .25 m of boundary with its parent; joins are orthogonal");
        }
    }
    if (scene->work_count > BOAT_SCENE_WORK_SITES)
        return fail(diagnostic, "scene.work.sites", "exceeds the 4-site capacity");
    if (scene->work_present && !scene->work_count)
        return fail(diagnostic, "scene.work.sites", "must list at least one site");
    if (!scene->work_present && scene->work_count)
        return fail(diagnostic, "scene.work.sites", "is absent but has entries");
    if (scene->work_boat >= BOAT_SCENE_WORKBOATS)
        return fail(diagnostic, "scene.work.boat", "is not a registered workboat preset");
    bool notices = false;
    for (unsigned i = 0; i < BOAT_SCENE_NOTICES; ++i)
        notices |= scene->work_notice[i][0] != 0;
    if (!scene->work_present && notices)
        return fail(diagnostic, "scene.work.notices", "is absent but has entries");
    for (unsigned i = 0; i < BOAT_SCENE_NOTICES; ++i) {
        if (!scene->work_notice[i][0])
            continue;
        char at[64];
        dotted(at, sizeof(at), "scene.work.notices", boat_scene_notice_key((BoatSceneNotice)i));
        if (!boat_scene_notice_valid(scene->work_notice[i]))
            return fail(diagnostic, at,
                "must be 1-63 printable characters with at most the {site} token and no %");
    }
    for (unsigned i = 0; i < scene->work_count; ++i) {
        const BoatSceneWorkSite *w = &scene->work[i];
        indexed(path, sizeof(path), "scene.work.sites", i + 1, ".id");
        if (!bounded_text(w->id, sizeof(w->id)) || !boat_scene_identifier(w->id))
            return fail(diagnostic, path, "must be 1-23 characters of a-z, 0-9 and _");
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(scene->work[j].id, w->id))
                return fail(diagnostic, path, "repeats an earlier site id");
        indexed(path, sizeof(path), "scene.work.sites", i + 1, ".preset");
        if (w->preset >= BOAT_SCENE_WORK_PRESETS)
            return fail(diagnostic, path, "is not a registered work-site preset");
        indexed(path, sizeof(path), "scene.work.sites", i + 1, ".cargo");
        if (w->cargo >= BOAT_SCENE_CARGO_PRESETS)
            return fail(diagnostic, path, "is not a registered cargo preset");
        indexed(path, sizeof(path), "scene.work.sites", i + 1, ".x");
        if (!finite_in(w->x, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT))
            return fail(diagnostic, path, "must be finite within 20000 m");
        indexed(path, sizeof(path), "scene.work.sites", i + 1, ".z");
        if (!finite_in(w->z, -BOAT_SCENE_HALF_EXTENT, BOAT_SCENE_HALF_EXTENT))
            return fail(diagnostic, path, "must be finite within 20000 m");
        indexed(path, sizeof(path), "scene.work.sites", i + 1, ".heading");
        if (!finite_in(w->heading, -6.28318531f, 6.28318531f))
            return fail(diagnostic, path, "must be finite with magnitude at most 2 pi");
    }
    bool source = false;
    for (unsigned i = 0; i < scene->work_count; ++i)
        source
            |= scene->work[i].preset == BOAT_SCENE_WORK_SERVICE_PLATFORM && !scene->work[i].hidden;
    if (scene->work_count && !source)
        return fail(
            diagnostic, "scene.work.sites", "needs a visible service_platform site to start from");
    return true;
}

bool boat_scene_has_content(const BoatSceneDefinition *scene)
{
    return scene
        && (scene->berths_present || scene->modules_present || scene->entrance_present
            || scene->traffic_present || scene->gulls_present || scene->berth_count
            || scene->module_count || scene->traffic.actor_count || scene->gulls.flock_count
            || scene->work_present || scene->work_count);
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
        BoatModelSpec spec = boat_scene_berth_resolve(b);
        value(&hash, "berth.length", spec.length);
        value(&hash, "berth.beam", spec.beam);
        value(&hash, "berth.mast_height", spec.mast_height);
        const unsigned char colors[] = { spec.hull.r, spec.hull.g, spec.hull.b, spec.deck.r,
            spec.deck.g, spec.deck.b, spec.trim.r, spec.trim.g, spec.trim.b };
        fold(&hash, "berth.colors", 13);
        fold(&hash, colors, sizeof(colors));
        fold(&hash, "berth.name", 11);
        fold(&hash, spec.name, strlen(spec.name) + 1);
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
        value(&hash, "module.length", m->length);
        value(&hash, "module.width", m->width);
        value(&hash, "module.deck_height", m->deck_height);
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
    if (scene->gulls_present) {
        uint64_t gulls = waterfront_flock_digest(&scene->gulls);
        fold(&hash, "gulls", 6);
        count(&hash, "gulls.flocks", scene->gulls.flock_count);
        fold(&hash, &gulls, sizeof(gulls));
    }
    // Absent work keeps every earlier digest unchanged.
    if (scene->work_present) {
        fold(&hash, "work", 5);
        count(&hash, "work.sites", scene->work_count);
        for (unsigned i = 0; i < scene->work_count; ++i) {
            const BoatSceneWorkSite *w = &scene->work[i];
            fold(&hash, "work.id", 8);
            fold(&hash, w->id, strlen(w->id) + 1);
            count(&hash, "work.preset", w->preset);
            count(&hash, "work.cargo", w->cargo);
            value(&hash, "work.x", w->x);
            value(&hash, "work.z", w->z);
            value(&hash, "work.heading", w->heading);
            if (w->hidden) // Visible sites keep their S1b digests.
                fold(&hash, "work.hidden", 12);
        }
        // The default workboat keeps every S1a/S1b/S1c digest unchanged.
        if (scene->work_boat) {
            fold(&hash, "work.boat", 10);
            count(&hash, "work.boat", scene->work_boat);
        }
        for (unsigned i = 0; i < BOAT_SCENE_NOTICES; ++i) {
            if (!scene->work_notice[i][0])
                continue;
            fold(&hash, "work.notice", 12);
            count(&hash, "work.notice", i);
            fold(&hash, scene->work_notice[i], strlen(scene->work_notice[i]) + 1);
        }
    }
    return hash;
}

uint64_t boat_scene_work_digest(const BoatSceneDefinition *scene)
{
    uint64_t hash = 14695981039346656037ull;
    if (!scene)
        return hash;
    count(&hash, "work.present", (unsigned)scene->work_present);
    count(&hash, "work.sites", scene->work_count);
    for (unsigned i = 0; i < scene->work_count; ++i) {
        const BoatSceneWorkSite *w = &scene->work[i];
        fold(&hash, "work.id", 8);
        fold(&hash, w->id, strlen(w->id) + 1);
        count(&hash, "work.preset", w->preset);
        count(&hash, "work.cargo", w->cargo);
        value(&hash, "work.x", w->x);
        value(&hash, "work.z", w->z);
        value(&hash, "work.heading", w->heading);
        count(&hash, "work.hidden", (unsigned)w->hidden);
    }
    count(&hash, "work.boat", scene->work_boat);
    return hash;
}

bool boat_scene_same_except_cosmetics(
    const BoatSceneDefinition *a, const BoatSceneDefinition *b, bool berth_appearance)
{
    if (!a || !b || a->gulls_present != b->gulls_present || a->work_present != b->work_present)
        return false;
    BoatSceneDefinition ca = *a, cb = *b;
    for (unsigned i = 0; berth_appearance && i < BOAT_SCENE_BERTHS; ++i) {
        ca.berths[i].appearance.hull = cb.berths[i].appearance.hull = (Color) { 0 };
        ca.berths[i].appearance.deck = cb.berths[i].appearance.deck = (Color) { 0 };
        ca.berths[i].appearance.trim = cb.berths[i].appearance.trim = (Color) { 0 };
        memset(ca.berths[i].appearance.name, 0, BOAT_MODEL_NAME_BYTES);
        memset(cb.berths[i].appearance.name, 0, BOAT_MODEL_NAME_BYTES);
    }
    for (unsigned i = 0; i < WATERFRONT_FLOCKS; ++i) {
        ca.gulls.flocks[i].cue_count = 0;
        cb.gulls.flocks[i].cue_count = 0;
    }
    return boat_scene_digest(&ca) == boat_scene_digest(&cb);
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
        = { "walkway", "platform", "gangway", "quay_deck", "finger_pier" };
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

BoatModelSpec boat_scene_berth_resolve(const BoatSceneBerth *b)
{
    BoatModelSpec s = boat_scene_berth_spec((BoatSceneBerthRecipe)b->recipe);
    const BoatSceneAppearance *a = &b->appearance;
    if (a->length != 0) {
        float ratio = a->length / s.length;
        s.cabin_length *= ratio;
        s.cabin_offset *= ratio;
        s.length = a->length;
    }
    if (a->beam != 0)
        s.beam = a->beam;
    if (a->mast_height != 0)
        s.mast_height = a->mast_height;
    if (a->hull.a)
        s.hull = a->hull;
    if (a->deck.a)
        s.deck = a->deck;
    if (a->trim.a)
        s.trim = a->trim;
    memcpy(s.name, a->name, sizeof(s.name));
    return s;
}

void boat_scene_berth_canonicalize(BoatSceneBerth *b)
{
    BoatModelSpec base = boat_scene_berth_spec((BoatSceneBerthRecipe)b->recipe);
    BoatSceneAppearance *a = &b->appearance;
    if (a->length == base.length)
        a->length = 0;
    if (a->beam == base.beam)
        a->beam = 0;
    if (a->mast_height == base.mast_height)
        a->mast_height = 0;
    if (!memcmp(&a->hull, &base.hull, sizeof(Color)))
        a->hull = (Color) { 0 };
    if (!memcmp(&a->deck, &base.deck, sizeof(Color)))
        a->deck = (Color) { 0 };
    if (!memcmp(&a->trim, &base.trim, sizeof(Color)))
        a->trim = (Color) { 0 };
}

float boat_scene_module_distance(const BoatSceneModule *m, float x, float z)
{
    float dx = x - m->x, dz = z - m->z;
    float c = cosf(m->heading), s = sinf(m->heading);
    return hypotf(fmaxf(0, fabsf(dx * c + dz * s) - m->width * .5f),
        fmaxf(0, fabsf(-dx * s + dz * c) - m->length * .5f));
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
        if (!strncmp(scene->modules[i].id, id, BOAT_SCENE_TEXT))
            return &scene->modules[i];
    return NULL;
}

static const char *const work_presets[] = { "service_platform", "receiving_platform" };

const char *boat_scene_work_preset_name(BoatSceneWorkPreset preset)
{
    return (unsigned)preset < BOAT_SCENE_WORK_PRESETS ? work_presets[preset] : "unknown";
}

bool boat_scene_work_preset_from_name(const char *name, BoatSceneWorkPreset *preset)
{
    for (unsigned i = 0; i < BOAT_SCENE_WORK_PRESETS; ++i)
        if (!strcmp(name, work_presets[i])) {
            *preset = (BoatSceneWorkPreset)i;
            return true;
        }
    return false;
}

static const char *const workboats[] = { "flat_deck" };

const char *boat_scene_workboat_name(BoatSceneWorkboat b)
{
    return (unsigned)b < BOAT_SCENE_WORKBOATS ? workboats[b] : "unknown";
}

bool boat_scene_workboat_from_name(const char *name, BoatSceneWorkboat *b)
{
    for (unsigned i = 0; i < BOAT_SCENE_WORKBOATS; ++i)
        if (!strcmp(name, workboats[i])) {
            *b = (BoatSceneWorkboat)i;
            return true;
        }
    return false;
}

const char *boat_scene_cargo_preset_name(BoatSceneCargoPreset preset)
{
    return preset == BOAT_SCENE_CARGO_CRATE ? "crate" : "unknown";
}

bool boat_scene_cargo_preset_from_name(const char *name, BoatSceneCargoPreset *preset)
{
    if (strcmp(name, "crate"))
        return false;
    *preset = BOAT_SCENE_CARGO_CRATE;
    return true;
}

static const char *const notice_keys[BOAT_SCENE_NOTICES]
    = { "delivered", "built", "supplied", "returned" };

const char *boat_scene_notice_key(BoatSceneNotice n)
{
    return (unsigned)n < BOAT_SCENE_NOTICES ? notice_keys[n] : "unknown";
}

bool boat_scene_notice_valid(const char *text)
{
    if (!text)
        return false;
    size_t length = strlen(text);
    if (length < 1 || length > 63)
        return false;
    for (const char *p = text; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 126 || c == '%')
            return false;
        if (c == '{' || c == '}') {
            if (strncmp(p, "{site}", 6))
                return false;
            p += 5;
        }
    }
    return true;
}
