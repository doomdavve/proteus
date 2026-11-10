#include "wake_velocity_app.h"

#include "raylib.h"
#include <emscripten/emscripten.h>

double wake_app_now(void) { return emscripten_get_now() / 1000.0; }

unsigned wake_app_window_flags(bool hidden)
{
    (void)hidden; // CSS sizing belongs to the page; mobile gameplay adapts the drawing buffer.
    return 0;
}

void wake_app_title_open(void)
{
    SetTargetFPS(60);
    InitAudioDevice(); // Start inside the page's Play gesture, before the title yields.
}

void wake_app_title_close(void) { CloseAudioDevice(); }
