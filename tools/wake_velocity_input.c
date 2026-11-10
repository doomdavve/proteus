#include "wake_velocity_input.h"
#include "wake_velocity_controls.h"

BoatAppInput boat_app_input_sample(void)
{
    BoatAppInput input = { 0 };
    input.up = IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W);
    input.down = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
    input.left = IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A);
    input.right = IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D);
    input.confirm = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    input.activate = input.confirm || IsKeyPressed(KEY_SPACE);
    input.back = IsKeyPressed(KEY_ESCAPE);
    input.pause = IsKeyPressed(KEY_P);
    input.bracket_left = IsKeyPressed(KEY_LEFT_BRACKET);
    input.bracket_right = IsKeyPressed(KEY_RIGHT_BRACKET);
    input.backslash = IsKeyPressed(KEY_BACKSLASH);
    input.backspace_down = IsKeyDown(KEY_BACKSPACE);
    input.pointer = GetMousePosition();
    input.pointer_delta = GetMouseDelta();
    input.pointer_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    input.pointer_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    input.helm_left = IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A);
    input.helm_right = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
    input.throttle_up = IsKeyDown(KEY_UP) || IsKeyDown(KEY_W);
    input.throttle_down = IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S);
    input.precision = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    return input;
}

void boat_app_input_poll_touch(BoatAppInput *input)
{
    int touch_click = wake_touch_tap(&input->pointer.x);
    if (touch_click)
        input->pointer_pressed = touch_click > 0;
}
