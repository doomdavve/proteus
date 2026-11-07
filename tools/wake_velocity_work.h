#ifndef WAKE_VELOCITY_WORK_H
#define WAKE_VELOCITY_WORK_H

// The game's side of the working-harbour session (SV1f): it runs the location's bundled work
// package in place of the free-ride boat step, copies its boat, clock and world into the game's
// session each tick, and hands the renderers the sites, crate, crane and workboat as extra
// geometry. The simulation is the shared BoatWorkSession; this file owns no rules.

#include "boat_model.h"
#include "boat_session.h"
#include "boat_view_state.h"
#include "boat_work_session.h"

typedef struct {
    BoatWorkSession session;
    BoatModel solids, decal;
    BoatViewProps props;
    bool open;
} WakeWork;

// Start the location's bundled work package over GAME, which borrows RESOURCES. False (with
// DIAGNOSTIC filled) when the location has no work package.
bool wake_work_open(WakeWork *work, BoatSession *game, const BoatWorldResources *resources,
    const BoatWorldDefinition *location, char *diagnostic, size_t capacity);
// A fresh session and job (the menu's reset).
void wake_work_reset(WakeWork *work, BoatSession *game);
// One tick with the crane and helm inputs, then the cosmetic presentation step. Call
// wake_work_sync once per frame afterwards.
void wake_work_tick(
    WakeWork *work, BoatSession *game, float slew, float reach, float hoist, BoatInput input);
// The scripted capture drive for TICKS ticks: idle at the berth, and from tick
// WAKE_WORK_DELIVERY_TICK on the landing delivery, then idle.
enum { WAKE_WORK_DELIVERY_TICK = 1200 };
bool wake_work_script(WakeWork *work, BoatSession *game, uint64_t ticks);
// Copy the work session's boat, clock and world into GAME.
void wake_work_sync(WakeWork *work, BoatSession *game);
// Rebuild the extra geometry for the current state and return it (valid until the next call).
const BoatViewProps *wake_work_props(WakeWork *work);
// The job's HUD rows and the current notice, drawn over the game's HUD.
void wake_work_hud(const WakeWork *work, int width, int height);

#endif
