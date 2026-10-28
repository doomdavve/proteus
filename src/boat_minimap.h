#ifndef PROTEUS_BOAT_MINIMAP_H
#define PROTEUS_BOAT_MINIMAP_H

#include "boat_location.h"
#include "boat_voyage.h"

// North-up chart of a mapped location for solo play: the whole map baked once from its water
// triangles, then composed with the boat, landmarks and any voyage's shelters, destination and
// exposed water. Everything is drawn on the CPU into scene-pixel images, which callers upload and
// composite at scene resolution before integer scaling.
typedef enum {
    BOAT_MINIMAP_HIDDEN,
    BOAT_MINIMAP_CORNER, // Small overview in the scene's top-right corner, without labels.
    BOAT_MINIMAP_CHART, // Larger labelled chart above the instruments, with a scale bar.
    BOAT_MINIMAP_MODES
} BoatMinimapMode;

typedef struct {
    Image base; // Baked land and water inside the trim; markers are composed onto FRAME.
    Image frame; // The latest composition, trim included.
    float scale; // Pixels per metre.
    Vector2 north_west; // Map east/north at the base image's top-left corner.
} BoatMinimapLayer;

// Caller-owned; zero-initialized is an empty minimap that composes nothing.
typedef struct {
    const BoatWorldDefinition *location;
    BoatMinimapLayer layers[BOAT_MINIMAP_MODES]; // Index BOAT_MINIMAP_HIDDEN is unused.
} BoatMinimap;

// Fails for an unmapped location or on allocation failure, leaving the minimap zeroed.
bool boat_minimap_init(BoatMinimap *minimap, const BoatWorldDefinition *location);
// Safe on a zeroed, failed or already destroyed minimap.
void boat_minimap_destroy(BoatMinimap *minimap);
bool boat_minimap_ready(const BoatMinimap *minimap);
// The mode after MODE in the N-key cycle: corner, chart, hidden.
BoatMinimapMode boat_minimap_next(BoatMinimapMode mode);
// Scene-pixel rectangle of MODE's frame in a WIDTH x HEIGHT scene; empty when hidden or unready.
Rectangle boat_minimap_bounds(
    const BoatMinimap *minimap, BoatMinimapMode mode, int width, int height);
// Frame pixel of simulation X/Z POSITION in MODE's frame, trim included.
Vector2 boat_minimap_project(const BoatMinimap *minimap, BoatMinimapMode mode, Vector2 position);
// Compose MODE's frame for a boat at simulation X/Z POSITION with HEADING (forward is
// (sin, -cos)); VOYAGE may be NULL or routeless. Returns NULL when hidden or unready. The
// image stays owned by the minimap and valid until the next composition or destruction.
const Image *boat_minimap_compose(BoatMinimap *minimap, BoatMinimapMode mode,
    const BoatVoyage *voyage, Vector2 position, float heading);

#endif
