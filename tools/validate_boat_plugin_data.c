// AR5 data-only consumer: links proteus_plugin_catalog and proteus_plugin_data only, with no Lua,
// package I/O or worker, and opens every bundled package from its pre-evaluated wire bytes. A
// link of this program is the evidence that the data contract needs no evaluation library.
#include "boat_plugin_catalog.h"
#include "boat_plugin_objective.h"

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

int main(void)
{
    static BoatPluginRevision revision;
    CHECK(boat_plugin_catalog_count > 0);
    for (unsigned i = 0; i < boat_plugin_catalog_count; ++i) {
        const BoatPluginCatalogEntry *entry = boat_plugin_catalog_at(i);
        BoatPluginDiagnostic d = { 0 };
        CHECK(entry && boat_plugin_catalog_decode(entry, &revision, &d));
        CHECK(!strcmp(revision.manifest.id, entry->id));
        CHECK(boat_plugin_kind_parse(revision.manifest.kind) != BOAT_PLUGIN_KIND_NONE);
        CHECK(!strcmp(revision.manifest.kind, entry->kind));
    }
    // The objective checkpoint codec round-trips without the evaluator.
    static BoatPluginObjective objective, decoded;
    objective.generation = 1;
    objective.rng = 12345;
    objective.state_version = 1;
    static unsigned char bytes[sizeof(BoatPluginObjective)];
    size_t size = 0;
    CHECK(boat_plugin_objective_encode(&objective, bytes, sizeof(bytes), &size) && size);
    CHECK(boat_plugin_objective_decode(bytes, size, &decoded));
    CHECK(boat_plugin_objective_digest(&decoded) == boat_plugin_objective_digest(&objective));
    printf("plugin data: %u catalog entries decoded and an objective round trip, with no Lua\n",
        boat_plugin_catalog_count);
    return 0;
}
