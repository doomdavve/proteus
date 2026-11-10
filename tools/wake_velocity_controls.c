#include "wake_velocity_controls.h"

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
