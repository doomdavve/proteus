#include "wake_velocity/wake_velocity_app.h"

#include "raylib.h"

double wake_app_now(void) { return GetTime(); }

unsigned wake_app_window_flags(bool hidden)
{
    return hidden ? FLAG_WINDOW_HIDDEN : FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT;
}

void wake_app_title_open(void) { }

void wake_app_title_close(void) { }

int wake_app_canvas_scale(BoatResolution resolution, int width, int height)
{
    (void)width;
    (void)height;
    return boat_resolution_window_scale(resolution);
}

bool wake_app_viewport(int *width, int *height)
{
    (void)width;
    (void)height;
    return false;
}

void wake_app_canvas_fit(void) { }

void wake_app_session_canvas(BoatResolution resolution, int width, int height)
{
    (void)resolution;
    (void)width;
    (void)height;
}

void wake_app_session_open(bool vsync)
{
    if (!vsync)
        ClearWindowState(FLAG_VSYNC_HINT);
}
