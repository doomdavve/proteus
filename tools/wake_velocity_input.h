#ifndef WAKE_VELOCITY_INPUT_H
#define WAKE_VELOCITY_INPUT_H
#include "raylib.h"

#include <stdbool.h>

// AR2a: one normalized application input value per frame. raylib reads belong to the
// application layer; this carries no gameplay state (the client later turns it into commands).
typedef struct BoatAppInput {
    // Menu/UI navigation, true only on the frame a key was pressed.
    bool up; // KEY_UP or KEY_W pressed
    bool down; // KEY_DOWN or KEY_S pressed
    bool left; // KEY_LEFT or KEY_A pressed
    bool right; // KEY_RIGHT or KEY_D pressed
    bool confirm; // KEY_ENTER or KEY_KP_ENTER pressed
    bool activate; // confirm, or KEY_SPACE pressed
    bool back; // KEY_ESCAPE pressed
    bool pause; // KEY_P pressed
    // Pointer, window pixel coordinates (not scene coordinates).
    Vector2 pointer; // GetMousePosition(), or the touch tap position on the web
    Vector2 pointer_delta; // GetMouseDelta()
    bool pointer_pressed; // left mouse pressed this frame, or a completed touch tap (web)
    bool pointer_down; // left mouse held
    // Helm keys, held.
    bool helm_left; // KEY_LEFT or KEY_A down
    bool helm_right; // KEY_RIGHT or KEY_D down
    bool throttle_up; // KEY_UP or KEY_W down
    bool throttle_down; // KEY_DOWN or KEY_S down
    bool precision; // KEY_LEFT_SHIFT or KEY_RIGHT_SHIFT down
} BoatAppInput;

// Reads raylib input once. poll_touch: on the web, also consume a pending touch tap
// (boat_web_touch_click from "boat_web_touch.h") exactly as the existing menu code does. That
// call consumes the tap, so callers pass true only on frames where a UI consumer would have
// called it before. Native builds ignore poll_touch.
BoatAppInput boat_app_input_sample(bool poll_touch);
#endif
