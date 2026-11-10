#ifndef WAKE_VELOCITY_APP_H
#define WAKE_VELOCITY_APP_H
// AR3 application lifecycle for wake_velocity: the process clock, the windows the game opens
// itself and the title's start and stop. CMake selects wake_velocity_app.c (native) or
// wake_velocity_app_web.c (browser). Sessions, frame pacing and audio streams keep their own
// owners; this holds only what differs between the native loop and the page.
#include <stdbool.h>

// Monotonic seconds on one origin for the whole process (plugin request and acceptance
// timestamps). The browser's starts before InitWindow resets GLFW's clock; native uses GetTime.
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
