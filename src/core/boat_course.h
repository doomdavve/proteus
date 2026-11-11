#ifndef PROTEUS_BOAT_COURSE_H
#define PROTEUS_BOAT_COURSE_H

#include "raylib.h"

#include <stdint.h>

#define BOAT_COURSE_GATES 4
// The shipped time trial's digest. Saved bests keep the plain file name only for this course.
#define BOAT_COURSE_STANDARD_DIGEST UINT64_C(0xa27ccadf8bd7a754)

typedef struct {
    Vector2 centre;
    float half_width;
} BoatCourseGate;

// PD1: the time trial's start and gates are values bound into the session. The definition is
// borrowed by BoatCourse; an enabled course requires one.
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
    // Borrowed start/gates; required whenever the course is enabled.
    const BoatCourseDefinition *definition;
} BoatCourse;

// FNV-1a over the start, heading and every gate, with float bits least significant first.
uint64_t boat_course_digest(const BoatCourseDefinition *definition);
void boat_course_step(BoatCourse *course, Vector2 previous, Vector2 position, double dt);

#endif
