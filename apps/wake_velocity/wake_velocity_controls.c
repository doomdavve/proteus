#include "wake_velocity/wake_velocity_controls.h"

#include "raylib.h"

#include <math.h>

void wake_touch_session(bool enabled) { (void)enabled; }

bool wake_touch_menu_pressed(void) { return false; }

int wake_touch_buttons(bool active)
{
    (void)active;
    return 0;
}

float wake_touch_target(void) { return NAN; }

int wake_touch_button_height(int width)
{
    (void)width;
    return 26;
}

int wake_touch_state(void) { return 0; }

void wake_touch_layout(const float *rects) { (void)rects; }

void wake_touch_status(char *text, int capacity)
{
    if (capacity > 0)
        text[0] = '\0';
}

int wake_touch_tap(float *position)
{
    (void)position;
    return 0;
}

bool wake_mouse_toggle_pressed(void) { return IsKeyPressed(KEY_M); }

int wake_mouse_capture(bool wanted, bool available, bool captured)
{
    (void)available;
    if (wanted != captured) {
        if (wanted)
            DisableCursor();
        else
            EnableCursor();
    }
    return wanted ? 1 : 0;
}

float wake_mouse_movement(float native_delta) { return native_delta; }

bool wake_fullscreen_key_pressed(void) { return IsKeyPressed(KEY_F11); }
