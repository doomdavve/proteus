#include "wake_velocity/wake_velocity_work.h"

#include "wake_velocity/boat_title.h"

#include <stdio.h>

// The browser build of wake_velocity_work.h: WORKING HARBOUR is native-only. Its sessions step
// the work package's Lua, which the browser game must not link (tools/check_web_wasm.py), and the
// browser menu stops at CANAL TOUR, so none of this runs. Opening a work session simply fails.

bool wake_work_open(WakeWork *work, BoatSession *game, const BoatWorldResources *resources,
    const BoatWorldDefinition *location, char *diagnostic, size_t capacity)
{
    (void)game;
    (void)resources;
    (void)location;
    *work = (WakeWork) { 0 };
    if (diagnostic && capacity)
        snprintf(diagnostic, capacity, "the working harbour is not available in the browser");
    return false;
}

int wake_work_save_open(WakeWork *work, BoatSession *game, const char *explicit_path,
    const char *data_dir, bool default_dir, bool fresh)
{
    (void)work;
    (void)game;
    (void)explicit_path;
    (void)data_dir;
    (void)default_dir;
    (void)fresh;
    return 0;
}

void wake_work_autosave(WakeWork *work, bool on)
{
    (void)work;
    (void)on;
}

void wake_work_save_frame(WakeWork *work, bool paused, bool focused)
{
    (void)work;
    (void)paused;
    (void)focused;
}

bool wake_work_save_now(WakeWork *work)
{
    (void)work;
    return false;
}

bool wake_work_save_leave(WakeWork *work)
{
    (void)work;
    return false;
}

BoatWorkFrame wake_work_frame_input(WakeWork *work, bool active, bool demo, float steering_target,
    float steering_rate, float thrust_target)
{
    (void)work;
    (void)active;
    (void)demo;
    (void)steering_target;
    (void)steering_rate;
    (void)thrust_target;
    return (BoatWorkFrame) { 0 };
}

bool wake_work_start(WakeWork *work, BoatSession *game, double now)
{
    (void)work;
    (void)game;
    (void)now;
    return false;
}

void wake_work_stop(WakeWork *work) { (void)work; }

const BoatWorkSession *wake_work_view(const WakeWork *work) { return &work->session; }

bool wake_work_frame(
    WakeWork *work, BoatSession *game, double *accumulator, const BoatWorkFrame *frame, double now)
{
    (void)work;
    (void)game;
    (void)accumulator;
    (void)frame;
    (void)now;
    return false;
}

void wake_work_return_crate(WakeWork *work) { (void)work; }

bool wake_work_reset(WakeWork *work, BoatSession *game, double now)
{
    (void)work;
    (void)game;
    (void)now;
    return false;
}

bool wake_work_review_pose(WakeWork *work, BoatSession *game, const char *pose)
{
    (void)work;
    (void)game;
    (void)pose;
    return false;
}

bool wake_work_script(WakeWork *work, BoatSession *game, uint64_t ticks)
{
    (void)work;
    (void)game;
    (void)ticks;
    return false;
}

void wake_work_sync(WakeWork *work, BoatSession *game)
{
    (void)work;
    (void)game;
}

const BoatViewProps *wake_work_props(WakeWork *work) { return &work->props; }

void wake_work_hud(WakeWork *work, const WakeWorkProjection *projection, int width, int height)
{
    (void)work;
    (void)projection;
    (void)width;
    (void)height;
}

void wake_work_title_scan(const char *data_dir, BoatTitleSave *save)
{
    (void)data_dir;
    *save = (BoatTitleSave) { 0 };
}

bool wake_work_title_delete(const BoatTitleSave *save)
{
    (void)save;
    return false;
}

bool wake_work_title_set_aside(const BoatTitleSave *save)
{
    (void)save;
    return false;
}
