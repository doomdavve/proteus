#ifndef WAKE_VELOCITY_CONTROLS_H
#define WAKE_VELOCITY_CONTROLS_H
#include <stdbool.h>

// AR2b capabilities of wake_velocity: the browser's touch controls (web/mobile_controls.js).
// CMake selects wake_velocity_controls.c (native) or wake_velocity_controls_web.c (browser). The
// game keeps every decision (when controls are active, drawing, steering); these only reach the
// platform.

// Start or stop the touch session (the page clears held controls and any pending tap).
// Native: nothing.
void wake_touch_session(bool enabled);

// The touch MENU button was pressed since the last call; consumes it. Native: false.
bool wake_touch_menu_pressed(void);

// Samples this frame's held touch controls; ACTIVE lets them drive and, when true, the page drops
// a pending tap. Bits: 1 forward, 2 reverse, 4 left, 8 right, 16 the touch presentation owns
// steering. Native: 0.
int wake_touch_buttons(bool active);

// Tilt steering target from the latest wake_touch_buttons, in [-1, 1], or NAN. Native: NAN.
float wake_touch_target(void);

// Touch button height in frame pixels for a WIDTH-pixel frame (at least 26). Native: 26.
int wake_touch_button_height(int width);

// Presentation bits: 1 controls shown, 2 tilt on, 4 tilt permission pending, 8 fullscreen
// supported, 16 fullscreen active. Native: 0.
int wake_touch_state(void);

// Publish 8 hit rectangles (x, y, width, height as fractions of the canvas; 32 floats).
// Native: nothing.
void wake_touch_layout(const float *rects);

// The page's status line, NUL-terminated. Native: empty.
void wake_touch_status(char *text, int capacity);

// Consume a pending tap: 1 with POSITION[0..1] set to canvas pixels, -1 touch owns the pointer
// (no mouse press), 0 use mouse input. Native: 0.
int wake_touch_tap(float *position);

#endif
