#ifndef PROTEUS_BOAT_VIEW_STATE_H
#define PROTEUS_BOAT_VIEW_STATE_H

#include "boat.h"
#include "boat_course.h"
#include "boat_scene_definition.h"

struct BoatRaceCourse;

// What a view shows: the live session boat, a replay sample or a ghost. Built per displayed
// frame and never stepped; replay values are continuous-time interpolations of a recording.
typedef struct {
    unsigned int skin; // Local cosmetic choice; zero is Sweden. Never serialized or simulated.
    Boat boat; // Pose, motion and engine values for drawing and audio. Not a live boat.
    double time; // World time shown: the session tick's seconds, or recorded continuous time.
    BoatCourse course; // Gate progress for course drawing and the HUD; disabled when free.
    // L1.3: frame-borrowed accepted scene; NULL draws and collides with the compiled marina.
    const BoatSceneDefinition *scene;
    // Race rooms: draw the river sprint's start, gates and finish; NEXT_GATE is the viewer's
    // next uncleared gate (the gate count once finished).
    struct {
        bool enabled;
        const struct BoatRaceCourse *course; // Frame-borrowed room course; NULL draws no race.
        unsigned char next_gate;
    } race;
} BoatViewState;

#endif
