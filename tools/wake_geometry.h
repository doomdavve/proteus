#ifndef PROTEUS_WAKE_GEOMETRY_H
#define PROTEUS_WAKE_GEOMETRY_H

// R2a private CPU experiment. No game, BoatWater, renderer or network target links this.
#include "core/boat_wake_objects.h"

#define R2_SLOTS 64
#define R2_CAPACITY (BOAT_OBJECT_BOATS * R2_SLOTS)
typedef enum { R2_BASELINE, R2_SHORT, R2_OPEN, R2_ANGLE, R2_MATCHED, R2_MODES } R2Mode;
typedef struct {
    BoatWakeObject records[R2_CAPACITY];
    BoatWakeObjectEmitter emitters[BOAT_OBJECT_BOATS];
    uint64_t tick, births, expired;
    unsigned live, peak;
    R2Mode mode;
} R2Field;
typedef struct {
    Vector2 position;
    float speed, wet;
} R2Source;
const char *r2_name(R2Mode mode);
bool r2_short(R2Mode mode);
void r2_init(R2Field *field, R2Mode mode);
void r2_advance(R2Field *field);
bool r2_track(R2Field *field, unsigned emitter, R2Source source);
R2Source r2_source(const char *path, unsigned tick, unsigned emitter, bool dispersed);
BoatWakeObjectSample r2_query(const R2Field *field, Vector2 point, double seconds);
BoatWakeObjectSample r2_piece(
    const BoatWakeObject *record, R2Mode mode, Vector2 point, double seconds);
// Private local-coordinate entry point; gradient is still expressed in world coordinates.
BoatWakeObjectSample r2_piece_coordinates(
    const BoatWakeObject *record, R2Mode mode, float u, float v, double seconds);

#endif
