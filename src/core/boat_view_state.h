#ifndef PROTEUS_BOAT_VIEW_STATE_H
#define PROTEUS_BOAT_VIEW_STATE_H

#include "core/boat.h"
#include "core/boat_course.h"
#include "core/boat_marks.h"
#include "core/boat_scene_definition.h"
#include "core/geometry3d.h"

struct BoatRaceCourse;

// Extra frame-borrowed geometry in renderer coordinates (the working harbour's sites, crate,
// crane and workboat). SOLIDS are lit; DECAL (may be NULL) is the landing decal on receivers.
// HIDE_PLAYER omits the skinned player boat because SOLIDS already hold the workboat.
typedef struct {
    const Mesh3D *solids;
    const Mesh3D *decal;
    bool hide_player;
    // Optional shared lighting cycle. Zero-init (flag false) keeps every legacy caller on its
    // fixed daylight phase or explicit hour. HOURS is unwrapped; the renderers normalize it.
    bool lighting_cycle;
    double lighting_hours, lighting_cycle_seconds;
} BoatViewProps;

// What a view shows: the live session boat, a replay sample or a ghost. Built per displayed
// frame and never stepped; replay values are continuous-time interpolations of a recording.
typedef struct {
    unsigned int skin; // Local cosmetic choice; zero is Sweden. Never serialized or simulated.
    Boat boat; // Pose, motion and engine values for drawing and audio. Not a live boat.
    double time; // World time shown: the session tick's seconds, or recorded continuous time.
    BoatCourse course; // Gate progress for course drawing and the HUD; disabled when free.
    // L1.3: frame-borrowed accepted scene; NULL draws and collides with the compiled marina.
    const BoatSceneDefinition *scene;
    const BoatViewProps *props; // NULL: nothing extra.
    // A room objective's marks (P5d), drawn as rings of buoys; frame-borrowed, NULL draws none.
    const BoatMarkSet *marks;
    // Race rooms: draw the river sprint's start, gates and finish; NEXT_GATE is the viewer's
    // next uncleared gate (the gate count once finished).
    struct {
        bool enabled;
        const struct BoatRaceCourse *course; // Frame-borrowed room course; NULL draws no race.
        unsigned char next_gate;
    } race;
} BoatViewState;

#endif
