#ifndef PROTEUS_BOAT_COURSE_H
#define PROTEUS_BOAT_COURSE_H

#include "raylib.h"

#include <stdint.h>

#define BOAT_COURSE_GATES 4
// Boat position at the start of each attempt, facing the first gate.
#define BOAT_COURSE_START ((Vector2) { 0, 50 })

typedef struct {
    Vector2 centre;
    float half_width;
} BoatCourseGate;

// PD1: the time trial's start and gates are values bound into the session. The compiled
// definition below remains the default and the reference for saved-run compatibility.
typedef struct {
    Vector2 start;
    float heading;
    BoatCourseGate gates[BOAT_COURSE_GATES];
} BoatCourseDefinition;

typedef struct {
    bool enabled;
    int next_gate; // 0: waiting at start, 1..3: running, 4: finished.
    double elapsed;
    double splits[BOAT_COURSE_GATES];
    // Crossings on the most recent simulation tick, -1 when absent.
    double crossing[BOAT_COURSE_GATES];
    // Borrowed start/gates; NULL selects the compiled definition above.
    const BoatCourseDefinition *definition;
} BoatCourse;

extern const BoatCourseGate boat_course_gates[BOAT_COURSE_GATES];
extern const BoatCourseDefinition boat_course_default;
// FNV-1a over the start, heading and every gate, with float bits least significant first.
uint64_t boat_course_digest(const BoatCourseDefinition *definition);
// The compiled definition's digest, for saved-run path compatibility.
uint64_t boat_course_compiled_digest(void);
void boat_course_step(BoatCourse *course, Vector2 previous, Vector2 position, double dt);

#endif
