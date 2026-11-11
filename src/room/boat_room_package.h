#ifndef PROTEUS_BOAT_ROOM_PACKAGE_H
#define PROTEUS_BOAT_ROOM_PACKAGE_H

// PD2/PD3: the authority's one package loader. A catalog ID decodes the build-time entry with no
// Lua; a directory is evaluated once at start-up with full validation. Both become the room
// definition the authority sends to every client.
#include "core/boat_room_definition.h"
#include "core/boat_world.h"
#include "plugins/boat_plugin_catalog.h"

// The package ID a room mode uses when --package is absent. Deathmatch uses `proteus.deathmatch`
// at every location; race uses `proteus.river-sprint` on the outer map; free uses the location's
// free package.
const char *boat_room_package_default(bool deathmatch, bool race, BoatLocationId location);

// Resolve SPEC (a catalog ID, or a directory path) into DEFINITION. SPEC NULL selects the
// default. LOCATION is the authority's --location; a package with a scene fixes its own and a
// conflicting flag refuses to start. WORLD is LOCATION's ready resources. On failure ERROR
// receives a one-line reason and the room must not start.
bool boat_room_package_load(const char *spec, bool deathmatch, bool race, BoatLocationId location,
    const BoatWorldResources *world, BoatRoomDefinition *definition, char *error, size_t size);

// P5b: a free package's room objective. Only the authority evaluates it; clients receive its HUD.
typedef struct {
    bool present;
    BoatMarkSet marks;
    unsigned state_version;
    bool migrates; // The source has a migrate callback (a reload then keeps the state).
    size_t source_size;
    char source[BOAT_PLUGIN_SOURCE_MAX];
} BoatRoomObjectiveSource;

// boat_room_package_load that also returns the package's room objective in OBJECTIVE (present is
// false for packages without one). The plain loader refuses packages with an objective, because
// its caller cannot run one.
bool boat_room_package_load_objective(const char *spec, bool deathmatch, bool race,
    BoatLocationId location, const BoatWorldResources *world, BoatRoomDefinition *definition,
    BoatRoomObjectiveSource *objective, char *error, size_t size);

#endif
