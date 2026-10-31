#include "boat_room_package.h"

#include "boat_plugin_io.h"
#include "waterfront_harbor.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void reason(char *error, size_t size, const char *text)
{
    if (error && size)
        snprintf(error, size, "%s", text);
}

static bool path_like(const char *spec)
{
    return spec && (strchr(spec, '/') || strchr(spec, '\\') || spec[0] == '.');
}

const char *boat_room_package_default(bool deathmatch, bool race, BoatLocationId location)
{
    if (race)
        return BOAT_PLUGIN_DEFAULT_RIVER_RACE;
    if (deathmatch)
        return BOAT_PLUGIN_DEFAULT_MATCH;
    switch (location) {
    case BOAT_LOCATION_BASIN:
        return BOAT_PLUGIN_DEFAULT_BASIN_FREE;
    case BOAT_LOCATION_RIVER:
        return BOAT_PLUGIN_DEFAULT_RIVER_FREE;
    case BOAT_LOCATION_OUTER:
        return "proteus.outer-free";
    default:
        return NULL;
    }
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

// Authored contacts must leave the room's spawn, course lines and recovery points clear.
static bool scene_contact(const BoatSceneDefinition *scene, Vector2 point, float clearance,
    const char *field, char *error, size_t size)
{
    Vector2 normal;
    if (waterfront_harbor_contact_height_scene(
            scene, point, clearance, 0, 2, (BoatWater) { 0 }, 0, &normal)) {
        char text[160];
        snprintf(text, sizeof(text), "the scene blocks %s", field);
        reason(error, size, text);
        return false;
    }
    return true;
}

static bool scene_clear(
    const BoatRoomDefinition *room, const BoatWorldDefinition *location, char *error, size_t size)
{
    if (!room->has_scene)
        return true;
    const BoatSceneDefinition *scene = &room->scene;
    if (!scene_contact(scene, location->fleet_spawn, 8, "the fleet spawn", error, size))
        return false;
    if (room->mode != BOAT_ROOM_MODE_RACE)
        return true;
    const BoatRaceCourse *c = &room->course;
    for (unsigned i = 0; i <= c->gate_count; ++i) {
        const BoatRaceGate *g = i ? &c->gates[i - 1] : &c->start;
        char field[64];
        snprintf(field, sizeof(field), i ? "gate %u's line" : "the start line", i);
        for (int edge = -1; edge <= 1; ++edge) {
            Vector2 p = { g->centre.x - g->direction.y * edge * g->half_width,
                g->centre.y + g->direction.x * edge * g->half_width };
            if (!scene_contact(scene, p, 4, field, error, size))
                return false;
        }
        for (unsigned k = 0; k < 14; ++k) {
            Boat boat;
            if (!boat_race_recovery_candidate(g, i == c->gate_count, k, &boat))
                continue;
            snprintf(field, sizeof(field), "gate %u's recovery point", i);
            if (!scene_contact(scene, boat.position, 6, field, error, size))
                return false;
        }
    }
    return true;
}

static bool convert(const BoatPluginRevision *revision, BoatPluginKind kind,
    BoatLocationId location, const BoatWorldResources *world, BoatRoomDefinition *definition,
    char *error, size_t size)
{
    const char *package_location = revision->manifest.location;
    bool any = !strcmp(package_location, "any");
    const BoatWorldDefinition *named
        = any ? world->definition : boat_location_find(package_location);
    if (!named || named->id != location) {
        char text[160];
        snprintf(text, sizeof(text), "the package is for %s, but this room is at %s",
            package_location, world->definition->name);
        reason(error, size, text);
        return false;
    }
    BoatRoomDefinition room = { .version = BOAT_ROOM_VERSION,
        .mode = kind == BOAT_PLUGIN_KIND_RACE ? BOAT_ROOM_MODE_RACE
            : kind == BOAT_PLUGIN_KIND_MATCH  ? BOAT_ROOM_MODE_DEATHMATCH
                                              : BOAT_ROOM_MODE_FREE,
        .location = (unsigned char)location };
    copy_text(room.id, sizeof(room.id), revision->manifest.id);
    copy_text(room.name, sizeof(room.name), revision->manifest.name);
    if (kind == BOAT_PLUGIN_KIND_RACE) {
        room.course = revision->race.course;
        room.race_rules = revision->race.rules;
        room.has_scene = revision->race.has_scene;
        room.scene = revision->race.scene;
    } else if (kind == BOAT_PLUGIN_KIND_MATCH) {
        room.match_rules = revision->match.rules;
        room.combat_rules = revision->match.combat;
        room.has_scene = revision->match.has_scene;
        room.scene = revision->match.scene;
    } else if (kind == BOAT_PLUGIN_KIND_FREE) {
        room.has_scene = revision->free.has_scene;
        room.scene = revision->free.scene;
    } else {
        reason(error, size, "the package kind cannot define a room");
        return false;
    }
    BoatRoomDiagnostic diagnostic;
    if (!boat_room_definition_validate(&room, &diagnostic)) {
        char text[200];
        snprintf(text, sizeof(text), "%s: %s", diagnostic.field, diagnostic.message);
        reason(error, size, text);
        return false;
    }
    if (room.has_scene) {
        for (unsigned i = 0; i < room.scene.berth_count; ++i) {
            const BoatSceneBerth *b = &room.scene.berths[i];
            BoatModelSpec spec = boat_scene_berth_spec((BoatSceneBerthRecipe)b->recipe);
            if (!boat_map_water(&world->map, (Vector2) { b->x, b->z }, (int)ceilf(spec.length))) {
                reason(error, size, "a scene berth is not on open water");
                return false;
            }
        }
    }
    if (!scene_clear(&room, world->definition, error, size))
        return false;
    *definition = room;
    return true;
}

static bool load_catalog(const char *id, BoatLocationId location, const BoatWorldResources *world,
    BoatRoomDefinition *definition, char *error, size_t size)
{
    const BoatPluginCatalogEntry *entry = boat_plugin_catalog_find(id);
    if (!entry) {
        char text[160];
        snprintf(text, sizeof(text), "no bundled package named %s", id);
        reason(error, size, text);
        return false;
    }
    static BoatPluginRevision revision;
    BoatPluginDiagnostic diagnostic;
    if (!boat_plugin_catalog_decode(entry, &revision, &diagnostic)) {
        char text[200];
        snprintf(text, sizeof(text), "%s: %s", diagnostic.field, diagnostic.message);
        reason(error, size, text);
        return false;
    }
    BoatPluginKind kind = boat_plugin_kind_parse(entry->kind);
    return convert(&revision, kind, location, world, definition, error, size);
}

static bool load_directory(const char *directory, BoatLocationId location,
    const BoatWorldResources *world, BoatRoomDefinition *definition, char *error, size_t size)
{
    static BoatPluginFiles files;
    BoatPluginDiagnostic diagnostic = { 0 };
    if (boat_plugin_read(directory, NULL, &files, &diagnostic) != BOAT_PLUGIN_OK) {
        char text[220];
        snprintf(text, sizeof(text), "%s%s%s: %s", diagnostic.file, diagnostic.field[0] ? " " : "",
            diagnostic.field, diagnostic.message);
        reason(error, size, text);
        return false;
    }
    BoatPluginManifest manifest;
    if (boat_plugin_parse_manifest(
            files.manifest, files.manifest_size, NULL, &manifest, &diagnostic)
        != BOAT_PLUGIN_OK) {
        reason(error, size, diagnostic.message);
        return false;
    }
    BoatPluginKind kind = boat_plugin_kind_parse(manifest.kind);
    BoatPluginBundle bundle = boat_plugin_bundle(&files);
    static BoatPluginRevision revision;
    memset(&revision, 0, sizeof(revision));
    BoatPluginStatus status;
    switch (kind) {
    case BOAT_PLUGIN_KIND_RACE:
        status = boat_plugin_load_race(&bundle, NULL, &revision.race, &diagnostic);
        if (status == BOAT_PLUGIN_OK)
            status = boat_plugin_race_check_location(
                &revision.race, world->definition, &world->map, &diagnostic);
        break;
    case BOAT_PLUGIN_KIND_MATCH:
        status = boat_plugin_load_match(&bundle, NULL, &revision.match, &diagnostic);
        if (status == BOAT_PLUGIN_OK)
            status = boat_plugin_match_check_location(
                &revision.match, world->definition, &world->map, &diagnostic);
        break;
    case BOAT_PLUGIN_KIND_FREE:
        status = boat_plugin_load_free(&bundle, NULL, &revision.free, &diagnostic);
        if (status == BOAT_PLUGIN_OK)
            status
                = boat_plugin_free_check_location(&revision.free, world->definition, &diagnostic);
        break;
    default:
        status = BOAT_PLUGIN_MANIFEST;
        snprintf(diagnostic.message, sizeof(diagnostic.message), "%s",
            "a room package must be free, match or race");
        break;
    }
    if (status != BOAT_PLUGIN_OK) {
        char text[220];
        snprintf(text, sizeof(text), "%s: %s", diagnostic.field, diagnostic.message);
        reason(error, size, text);
        return false;
    }
    revision.manifest = manifest;
    if (!convert(&revision, kind, location, world, definition, error, size))
        return false;
    copy_text(definition->id, sizeof(definition->id), manifest.id);
    copy_text(definition->name, sizeof(definition->name), manifest.name);
    return true;
}

bool boat_room_package_load(const char *spec, bool deathmatch, bool race, BoatLocationId location,
    const BoatWorldResources *world, BoatRoomDefinition *definition, char *error, size_t size)
{
    if (!world || !boat_world_resources_ready(world)) {
        reason(error, size, "the room's map is not ready");
        return false;
    }
    if (race && location != BOAT_LOCATION_RIVER) {
        reason(error, size, "a race room runs on the river");
        return false;
    }
    if (!spec)
        spec = boat_room_package_default(deathmatch, race, location);
    if (!spec)
        return false;
    bool ok = path_like(spec) ? load_directory(spec, location, world, definition, error, size)
                              : load_catalog(spec, location, world, definition, error, size);
    if (!ok)
        return false;
    bool mode_ok = race ? definition->mode == BOAT_ROOM_MODE_RACE
        : deathmatch    ? definition->mode == BOAT_ROOM_MODE_DEATHMATCH
                        : definition->mode == BOAT_ROOM_MODE_FREE;
    if (!mode_ok) {
        reason(error, size, "the package kind does not match the room mode");
        return false;
    }
    return true;
}
