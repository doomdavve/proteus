#ifndef PROTEUS_BOAT_WAKE_OBJECTS_H
#define PROTEUS_BOAT_WAKE_OBJECTS_H

#include "raylib.h"
#include <stdint.h>

#define BOAT_OBJECT_BOATS 4
#define BOAT_OBJECT_PER_BOAT 32
#define BOAT_OBJECT_CAPACITY (BOAT_OBJECT_BOATS * BOAT_OBJECT_PER_BOAT)
#define BOAT_OBJECT_EMIT_TICKS 60
#define BOAT_OBJECT_LIFE_TICKS 1920
#define BOAT_OBJECT_DEFAULT_HEIGHT 1.2f

typedef struct {
    Vector2 position, direction;
    float length, amplitude, inverse_speed;
    uint64_t born;
} BoatWakeObject;
typedef struct {
    Vector2 start;
    uint64_t started, tracked;
    unsigned next;
    float strength, strength_sum;
    bool initialized;
} BoatWakeObjectEmitter;
/* Caller-owned shared world. BoatWater borrows it; never allocate a map-sized grid.
 * One advance per 120 Hz world tick, then track each boat's actual wet stern once.
 * Each emitter owns thirty-two slots. Configured lifetime cannot exceed their capacity. */
typedef struct {
    BoatWakeObject objects[BOAT_OBJECT_CAPACITY];
    BoatWakeObjectEmitter emitters[BOAT_OBJECT_BOATS];
    uint64_t tick, emitted, expired;
    unsigned life_ticks, live, peak_live;
    float crest_height;
} BoatWakeObjects;
typedef struct {
    float height, vertical_velocity;
    float crest_foam; // Visual crest coordinate/age mask; never contributes to hull forces.
    Vector2 gradient;
} BoatWakeObjectSample;

bool boat_wake_objects_init(BoatWakeObjects *world, unsigned life_ticks, float crest_height);
void boat_wake_objects_advance(BoatWakeObjects *world);
bool boat_wake_objects_track(
    BoatWakeObjects *world, unsigned boat, Vector2 stern, float speed, float wet);
BoatWakeObjectSample boat_wake_objects_query(const BoatWakeObjects *world, Vector2 position);
/* The same records evaluated TICK >= world->tick, before any later births: records that would
 * expire by then have already faded to zero. Earlier ticks return a flat sample. */
BoatWakeObjectSample boat_wake_objects_query_at(
    const BoatWakeObjects *world, Vector2 position, uint64_t tick);
/* Only EMITTER's records at the world tick: attribution diagnostics, never hull forces. */
BoatWakeObjectSample boat_wake_objects_query_emitter(
    const BoatWakeObjects *world, unsigned emitter, Vector2 position);
/* Pack CAPACITY vec4 slots per output array. Age replaces absolute birth time;
 * the renderer stores these two arrays as rows of a compact object-record texture. */
int boat_wake_objects_pack(const BoatWakeObjects *world, float *origins, float *parameters);

#endif
