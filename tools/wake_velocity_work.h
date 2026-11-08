#ifndef WAKE_VELOCITY_WORK_H
#define WAKE_VELOCITY_WORK_H

// The game's side of the working-harbour session (SV1f): it runs the location's bundled work
// package in place of the free-ride boat step, copies its boat, clock and world into the game's
// session each tick, and hands the renderers the sites, crate, crane and workboat as extra
// geometry. The simulation is the shared BoatWorkSession; this file owns no rules.

#include "boat_model.h"
#include "boat_session.h"
#include "boat_view.h"
#include "boat_view_state.h"
#include "boat_work_save.h"
#include "boat_work_session.h"

typedef struct {
    BoatWorkSession session;
    BoatModel solids, decal;
    BoatViewProps props;
    BoatWorkSave save;
    BoatWorkFrameWatch watch;
    bool open;
    // The work session's daylight clock drives the scene unless an explicit fixed lighting option
    // took over presentation; the clock itself still advances and autosaves.
    bool lighting_cycle;
} WakeWork;

// What the HUD needs to put a marker over the target berth.
typedef struct {
    const BoatViewState *view;
    const BoatPresentation *presentation;
    const BoatWorld *world;
    BoatCamera camera;
} WakeWorkProjection;

// What this frame's keys ask of the crane.
typedef struct {
    float slew, reach, hoist;
} WakeWorkDrive;

// Start the location's bundled work package over GAME, which borrows RESOURCES. False (with
// DIAGNOSTIC filled) when the location has no work package.
bool wake_work_open(WakeWork *work, BoatSession *game, const BoatWorldResources *resources,
    const BoatWorldDefinition *location, char *diagnostic, size_t capacity);
// Choose and read the save: EXPLICIT_PATH, else DATA_DIR, else (when DEFAULT_DIR) the game's data
// directory. A usable save replaces the fresh session; one that cannot be used is set aside as
// NAME.rejected. FRESH ignores any save. Returns 0 (none), 1 (resumed) or 2 (set aside). Call
// before the loop; autosaving starts with wake_work_autosave.
int wake_work_save_open(WakeWork *work, BoatSession *game, const char *explicit_path,
    const char *data_dir, bool default_dir, bool fresh);
// Start (or stop) the window loop's automatic saving. Needs an enabled save.
void wake_work_autosave(WakeWork *work, bool on);
// Once per frame, after the tick loop: milestone, periodic, pausing and focus-loss writes.
void wake_work_save_frame(WakeWork *work, bool paused, bool focused);
// The script's end, or leaving play (title, window close, quit): write when dirty.
bool wake_work_save_now(WakeWork *work);
bool wake_work_save_leave(WakeWork *work);
// Key actions for this frame (F hook, L lash, G moor, TAB crane or helm, 1/2/3 aim, J target),
// the brake, and the crane axes (A/D slew, W/S reach, Q/E hoist, only in crane view). ACTIVE is
// false while a menu, help or another window has the keys. Never reads the save keys: there are
// none.
WakeWorkDrive wake_work_frame_input(WakeWork *work, bool active);
// The menu's RETURN CRATE.
void wake_work_return_crate(WakeWork *work);
// A fresh session and job (the menu's reset).
void wake_work_reset(WakeWork *work, BoatSession *game);
// Developer-only bounded review fixture: "swing" hooks the real crate and runs the crane,
// "deck" loads and lashes it, "approach" uses the existing surveyed approach fixture and moor
// query at the landing. GAME is synced after success. False for an unknown name or
// when the real actions cannot reach the requested state.
bool wake_work_review_pose(WakeWork *work, BoatSession *game, const char *pose);
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
// The job's rows, the work status and guidance, the target marker, the notice and the save
// marks, drawn over the game's HUD. Counts down the SAVED mark one presentation frame.
void wake_work_hud(WakeWork *work, const WakeWorkProjection *projection, int width, int height);

#endif
