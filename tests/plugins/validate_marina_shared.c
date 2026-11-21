// M5 W5: harbor-life and harbor-room derive the Eriksberg marina from one shared module. The
// decoded definitions must equal what the single-file packages produced before the move: the
// constants below were recorded from those sources (commit f402c87) before they were edited.
// The directories, the catalog and the room loader must all agree on the same scene.
#include "core/boat_scene_definition.h"
#include "core/boat_world.h"
#include "plugins/boat_plugin_catalog.h"
#include "plugins/boat_plugin_io.h"
#include "plugins/boat_plugin_load.h"
#include "room/boat_room_package.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// Recorded from the pre-W5 single-file packages (see docs/MARINA_AUTHORING_PLAN.md, M5 W5).
#define OLD_SCENE_DIGEST 0x0692ebb6e02eca36ULL
#define OLD_VOYAGE_DIGEST 0xe8509272172cb7c4ULL
#define OLD_LIFE_ENTRY_DIGEST 0xe8509272172cb7c4ULL
#define OLD_ROOM_ENTRY_DIGEST 0x0692ebb6e02eca36ULL
#define OLD_ROOM_DIGEST 0x914a7a169b542df6ULL

static BoatWorldResources outer;
static BoatPluginRevision revision;

int main(void)
{
    CHECK(boat_world_resources_init(&outer, boat_location(BOAT_LOCATION_OUTER)));
    BoatPluginDiagnostic d;

    // harbor-life from its directory (entry plus module image) through the real loader.
    static BoatPluginFiles files;
    CHECK(boat_plugin_read("plugins/harbor-life", NULL, &files, &d) == BOAT_PLUGIN_OK);
    BoatPluginBundle bundle = boat_plugin_bundle(&files);
    static BoatPluginVoyage voyage;
    CHECK(boat_plugin_load_voyage(&bundle, NULL, &voyage, &d) == BOAT_PLUGIN_OK);
    CHECK(voyage.has_scene);
    uint64_t life_scene = boat_scene_digest(&voyage.scene);

    // ... and from the build-time catalog.
    const BoatPluginCatalogEntry *life = boat_plugin_catalog_find("proteus.harbor-life");
    CHECK(life && boat_plugin_catalog_decode(life, &revision, &d));
    CHECK(revision.voyage.has_scene);
    uint64_t catalog_life_scene = boat_scene_digest(&revision.voyage.scene);
    CHECK(revision.voyage.digest == voyage.digest);

    // harbor-room through the room loader, by directory and by catalog id.
    static BoatRoomDefinition room_dir, room_catalog;
    char error[220];
    CHECK(boat_room_package_load("plugins/harbor-room", false, false, BOAT_LOCATION_OUTER, &outer,
        &room_dir, error, sizeof(error)));
    CHECK(boat_room_package_load("proteus.harbor-room", false, false, BOAT_LOCATION_OUTER, &outer,
        &room_catalog, error, sizeof(error)));
    uint64_t room_scene = boat_scene_digest(&room_dir.scene);
    const BoatPluginCatalogEntry *room = boat_plugin_catalog_find("proteus.harbor-room");
    CHECK(room && boat_plugin_catalog_decode(room, &revision, &d));
    CHECK(revision.free.has_scene);
    uint64_t catalog_room_scene = boat_scene_digest(&revision.free.scene);

    printf("marina_shared scene=%016" PRIx64 " voyage=%016" PRIx64 " life_entry=%016" PRIx64
           " room_entry=%016" PRIx64 " room=%016" PRIx64 " berths=%u modules=%u vessels=%u"
           " flocks=%u\n",
        life_scene, voyage.digest, life->digest, room->digest,
        boat_room_definition_digest(&room_dir), voyage.scene.berth_count, voyage.scene.module_count,
        voyage.scene.traffic.actor_count, voyage.scene.gulls.flock_count);

    // One marina: all four routes to it agree.
    CHECK(life_scene == catalog_life_scene && life_scene == room_scene
        && life_scene == catalog_room_scene);
    CHECK(boat_room_definition_digest(&room_dir) == boat_room_definition_digest(&room_catalog));
    CHECK(voyage.scene.berth_count == 12 && voyage.scene.module_count == 13);
    CHECK(voyage.scene.traffic.actor_count == 4 && voyage.scene.gulls.flock_count == 1);
    // Unchanged from the single-file packages.
    CHECK(life_scene == OLD_SCENE_DIGEST);
    CHECK(voyage.digest == OLD_VOYAGE_DIGEST);
    CHECK(life->digest == OLD_LIFE_ENTRY_DIGEST);
    CHECK(room->digest == OLD_ROOM_ENTRY_DIGEST);
    CHECK(boat_room_definition_digest(&room_dir) == OLD_ROOM_DIGEST);
    printf("marina_shared=ok\n");
    return 0;
}
