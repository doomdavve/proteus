#ifndef WAKE_VELOCITY_APP_H
#define WAKE_VELOCITY_APP_H
// AR3 application lifecycle for wake_velocity: the process clock, the windows the game opens
// itself and the title's start and stop. CMake selects wake_velocity_app.c (native) or
// wake_velocity_app_web.c (browser). Sessions, frame pacing and audio streams keep their own
// owners; this holds only what differs between the native loop and the page.
#include <stdbool.h>

// Monotonic seconds for plugin request and acceptance timestamps. The browser's clock
// (performance.now) keeps one origin for the page, across InitWindow resetting GLFW's clock.
// Native uses GetTime, whose origin InitWindow sets: a direct --plugin launch opens its package
// in study() before the window exists, so that first timestamp is on an earlier origin than the
// session's frames (as before AR3).
double wake_app_now(void);
// raylib config flags for a window the game opens: native opens HIDDEN windows for captures and
// reports, otherwise resizable with vsync; the page owns the canvas's CSS size (no flags).
unsigned wake_app_window_flags(bool hidden);
// After the title's window opens, before its first frame: the browser targets 60 fps (each
// frame yields to the page) and opens audio inside the page's Play gesture. Native: nothing,
// since its title paces itself and sessions open audio (boat_audio).
void wake_app_title_open(void);
// After the title's last frame, before its window closes: the browser closes audio.
void wake_app_title_close(void);

#endif
