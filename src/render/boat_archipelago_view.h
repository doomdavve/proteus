#ifndef PROTEUS_BOAT_ARCHIPELAGO_VIEW_H
#define PROTEUS_BOAT_ARCHIPELAGO_VIEW_H

#include "core/boat_view_state.h"
#include "render/waterfront_scene.h"

// Noon at session time zero; one artistic day per 900 simulated seconds.
// No retained state: pause and direct/backward seeks follow the displayed simulation time.
BoatViewProps boat_archipelago_lighting(double time);

// Append residential panes with the same glass, occupancy and clock blend as Outer.
void boat_archipelago_windows_evaluate(WaterfrontSceneFrame *frame);

// Append dusk-to-dawn lantern panes after the caller selects the frame lighting.
// A slow rotating brightness pattern suggests a turning lens, without volumetric beams.
void boat_archipelago_lanterns_evaluate(double time, WaterfrontSceneFrame *frame);

// Append native local boats, birds and shore residents to the caller's scene snapshot.
// Pure simulation-time evaluation; CPU, GPU, reflections and shadows share these poses.
void boat_archipelago_evaluate(
    BoatWater water, double time, Vector3 player, Vector3 velocity, WaterfrontSceneFrame *frame);

#endif
