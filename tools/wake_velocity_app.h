#ifndef WAKE_VELOCITY_APP_H
#define WAKE_VELOCITY_APP_H
// AR3 application lifecycle for wake_velocity: the process clock, the windows the game opens
// itself and the title's start and stop. CMake selects wake_velocity_app.c (native) or
// wake_velocity_app_web.c (browser). Sessions, frame pacing and audio streams keep their own
// owners; this holds only what differs between the native loop and the page.
#include "boat_screen.h"
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

// The drawing buffer's multiple of a WIDTH x HEIGHT logical frame. Native: the resolution's
// window scale. Browser: 3, or the high scene's own scale, which shrinks for tall mobile frames
// so the buffer holds no more pixels than the scene.
int wake_app_canvas_scale(BoatResolution resolution, int width, int height);
// After resizing the window: the page refits the canvas's CSS (browser; native: nothing).
void wake_app_canvas_fit(void);
// Once a session's window is ready: the browser grows a canvas smaller than the session's
// buffer and refits it, and otherwise keeps the size the title or a previous session left
// (high resolution draws into a buffer of its own scale; CSS still fits the page). Native:
// nothing, since the session sized its own window.
void wake_app_session_canvas(BoatResolution resolution, int width, int height);
// After the session's UI loads, before its first frame: the browser targets 60 fps (each frame
// yields to the page); native drops vsync when VSYNC is false.
void wake_app_session_open(bool vsync);

#endif
