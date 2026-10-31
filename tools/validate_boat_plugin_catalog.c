/* PD1: every bundled catalog entry equals evaluating its embedded source; the free, outer and
 * time-trial defaults equal the compiled definitions; and catalog decoding starts no Lua VM. */
#include "boat_plugin_catalog.h"
#include "boat_plugin_lua.h"
#include "boat_plugin_wire.h"
#include "boat_test_cases.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static BoatPluginStatus load_source(
    const BoatPluginDecoded *decoded, BoatPluginRevision *revision, BoatPluginDiagnostic *d)
{
    BoatPluginBundle bundle
        = { decoded->manifest, decoded->manifest_size, decoded->source, decoded->source_size };
    BoatPluginStatus status;
    switch (decoded->kind) {
    case BOAT_PLUGIN_KIND_VOYAGE:
        status = boat_plugin_load_voyage(&bundle, NULL, &revision->voyage, d);
        if (status == BOAT_PLUGIN_OK)
            revision->manifest = revision->voyage.manifest;
        return status;
    case BOAT_PLUGIN_KIND_RACE:
        status = boat_plugin_load_race(&bundle, NULL, &revision->race, d);
        if (status == BOAT_PLUGIN_OK)
            revision->manifest = revision->race.manifest;
        return status;
    case BOAT_PLUGIN_KIND_FREE:
        status = boat_plugin_load_free(&bundle, NULL, &revision->free, d);
        if (status == BOAT_PLUGIN_OK)
            revision->manifest = revision->free.manifest;
        return status;
    case BOAT_PLUGIN_KIND_COURSE:
        status = boat_plugin_load_course(&bundle, NULL, &revision->course, d);
        if (status == BOAT_PLUGIN_OK)
            revision->manifest = revision->course.manifest;
        return status;
    case BOAT_PLUGIN_KIND_MATCH:
        status = boat_plugin_load_match(&bundle, NULL, &revision->match, d);
        if (status == BOAT_PLUGIN_OK)
            revision->manifest = revision->match.manifest;
        return status;
    default:
        return BOAT_PLUGIN_MANIFEST;
    }
}

static uint64_t revision_digest(const BoatPluginRevision *r, BoatPluginKind kind)
{
    switch (kind) {
    case BOAT_PLUGIN_KIND_VOYAGE:
        return r->voyage.digest;
    case BOAT_PLUGIN_KIND_RACE:
        return r->race.digest;
    case BOAT_PLUGIN_KIND_COURSE:
        return r->course.digest;
    case BOAT_PLUGIN_KIND_FREE:
        return r->free.digest;
    case BOAT_PLUGIN_KIND_MATCH:
        return r->match.digest;
    default:
        return 0;
    }
}

static int entries_equal_source(void)
{
    CHECK(boat_plugin_catalog_count >= 6);
    for (unsigned i = 0; i < boat_plugin_catalog_count; ++i) {
        const BoatPluginCatalogEntry *entry = boat_plugin_catalog_at(i);
        CHECK(entry);
        BoatPluginDecoded *decoded = malloc(sizeof(*decoded));
        BoatPluginRevision decoded_revision;
        BoatPluginDiagnostic d = { 0 };
        CHECK(decoded);
        CHECK(boat_plugin_wire_decode(entry->wire, entry->wire_size, decoded, &d));
        CHECK(boat_plugin_decoded_revision(decoded, &decoded_revision, &d));
        unsigned before = boat_plugin_lua_states();
        BoatPluginRevision catalog_revision;
        CHECK(boat_plugin_catalog_decode(entry, &catalog_revision, &d));
        CHECK(boat_plugin_lua_states() == before); // The runtime never starts a Lua VM.
        BoatPluginRevision evaluated_revision;
        CHECK(load_source(decoded, &evaluated_revision, &d) == BOAT_PLUGIN_OK);
        CHECK(evaluated_revision.manifest.api == catalog_revision.manifest.api);
        CHECK(!strcmp(evaluated_revision.manifest.id, entry->id));
        CHECK(!strcmp(evaluated_revision.manifest.kind, entry->kind));
        CHECK(!strcmp(evaluated_revision.manifest.location, entry->location));
        CHECK(evaluated_revision.manifest.scene == catalog_revision.manifest.scene);
        CHECK(evaluated_revision.manifest.objective == catalog_revision.manifest.objective);
        CHECK(evaluated_revision.manifest.timers == catalog_revision.manifest.timers);
        CHECK(evaluated_revision.manifest.state_version == catalog_revision.manifest.state_version);
        CHECK(decoded->kind == boat_plugin_kind_parse(entry->kind));
        CHECK(revision_digest(&catalog_revision, decoded->kind) == entry->digest);
        CHECK(revision_digest(&evaluated_revision, decoded->kind) == entry->digest);
        if (decoded->kind == BOAT_PLUGIN_KIND_VOYAGE)
            CHECK(boat_plugin_route_digest(&evaluated_revision.voyage.route)
                == boat_plugin_route_digest(&catalog_revision.voyage.route));
        if (decoded->kind == BOAT_PLUGIN_KIND_COURSE)
            CHECK(!memcmp(&evaluated_revision.course.course, &catalog_revision.course.course,
                sizeof(BoatCourseDefinition)));
        free(decoded);
    }
    return 0;
}

static int defaults_match_compiled(void)
{
    const BoatPluginCatalogEntry *basin
        = boat_plugin_catalog_default(BOAT_PLUGIN_KIND_FREE, "basin");
    const BoatPluginCatalogEntry *river
        = boat_plugin_catalog_default(BOAT_PLUGIN_KIND_FREE, "river");
    const BoatPluginCatalogEntry *outer
        = boat_plugin_catalog_default(BOAT_PLUGIN_KIND_VOYAGE, "outer");
    const BoatPluginCatalogEntry *course
        = boat_plugin_catalog_default(BOAT_PLUGIN_KIND_COURSE, "basin");
    CHECK(basin && river && outer && course);
    BoatPluginRevision r;
    BoatPluginDiagnostic d = { 0 };
    CHECK(boat_plugin_catalog_decode(outer, &r, &d));
    CHECK(r.voyage.digest == boat_plugin_route_digest(&boat_voyage_outer));
    CHECK(r.voyage.route.shelter_count == boat_voyage_outer.shelter_count);
    CHECK(boat_plugin_catalog_decode(course, &r, &d));
    CHECK(r.course.digest == boat_course_compiled_digest());
    CHECK(!memcmp(&r.course.course, &boat_course_default, sizeof(BoatCourseDefinition)));
    // Unknown kinds and locations have no default.
    const BoatPluginCatalogEntry *outer_free
        = boat_plugin_catalog_default(BOAT_PLUGIN_KIND_FREE, "outer");
    CHECK(outer_free);
    CHECK(boat_plugin_catalog_decode(outer_free, &r, &d) && r.free.digest == 0);
    // The deathmatch package equals the native defaults at every location.
    for (int id = 0; id < BOAT_LOCATION_COUNT; ++id) {
        const char *name = boat_location((BoatLocationId)id)->name;
        const BoatPluginCatalogEntry *match
            = boat_plugin_catalog_default(BOAT_PLUGIN_KIND_MATCH, name);
        CHECK(match && !strcmp(match->id, "proteus.deathmatch"));
        CHECK(boat_plugin_catalog_decode(match, &r, &d));
        BoatMatchRules rules = boat_match_default_rules();
        BoatCombatRules combat = boat_combat_default_rules();
        CHECK(boat_plugin_match_digest(&r.match.rules, &r.match.combat)
            == boat_plugin_match_digest(&rules, &combat));
    }
    CHECK(!boat_plugin_catalog_default(BOAT_PLUGIN_KIND_COURSE, "river"));
    return 0;
}

static int reachable_by_id(void)
{
    for (unsigned i = 0; i < boat_plugin_catalog_count; ++i) {
        const BoatPluginCatalogEntry *entry = boat_plugin_catalog_at(i);
        CHECK(boat_plugin_catalog_find(entry->id) == entry);
    }
    CHECK(!boat_plugin_catalog_find("proteus.missing"));
    return 0;
}

static const TestCase cases[] = {
    TEST_CASE(entries_equal_source),
    TEST_CASE(defaults_match_compiled),
    TEST_CASE(reachable_by_id),
};

int main(int argc, char **argv)
{
    return test_cases_main(cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
}
