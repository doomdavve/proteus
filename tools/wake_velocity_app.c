#include "wake_velocity_app.h"

#include "raylib.h"

double wake_app_now(void) { return GetTime(); }

unsigned wake_app_window_flags(bool hidden)
{
    return hidden ? FLAG_WINDOW_HIDDEN : FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT;
}

void wake_app_title_open(void) { }

void wake_app_title_close(void) { }
