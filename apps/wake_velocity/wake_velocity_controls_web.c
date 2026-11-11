#include "wake_velocity/wake_velocity_controls.h"

#include <emscripten/emscripten.h>

// Touch controls: web/mobile_controls.js.
EM_JS(int, wake_js_touch_click, (float *position), {
    const tap = Module['boatMobile']['click']();
    if (Object.is(tap, null))
        return 0;
    if (Object.is(tap, false))
        return -1;
    HEAPF32[position >> 2] = tap.x;
    HEAPF32[(position >> 2) + 1] = tap.y;
    return 1;
})
EM_JS(void, wake_js_mobile_session, (int enabled), { Module['boatMobile']['session'](enabled); })
EM_JS(int, wake_js_mobile_menu, (void), { return Module['boatMobile']['menu'](); })
EM_JS(int, wake_js_mobile_sample, (int active), { return Module['boatMobile']['sample'](active); })
EM_JS(float, wake_js_mobile_target, (void), { return Module['boatMobile']['target'](); })
EM_JS(int, wake_js_mobile_state, (void), { return Module['boatMobile']['state'](); })
EM_JS(int, wake_js_mobile_size, (int width), {
    return Math.max(26, Math.ceil(48 * width / Module['canvas'].getBoundingClientRect().width));
})
EM_JS(void, wake_js_mobile_status, (char *text, int capacity),
    { stringToUTF8(Module['boatMobile']['status'](), text, capacity); })
EM_JS(void, wake_js_mobile_layout, (const float *rects), {
    const regions = [];
    for (let i = 0; i < 8; ++i) {
        const offset = (rects >> 2) + i * 4;
        regions.push(Array.from(HEAPF32.subarray(offset, offset + 4)));
    }
    Module['boatMobile']['layout'](regions);
})

// Mouse steering capture: web/mouse_controls.js.
EM_JS(int, wake_js_mouse_state, (int wanted, int available),
    { return Module['boatMouse']['state'](wanted, available); })
EM_JS(int, wake_js_mouse_toggle, (void), { return Module['boatMouse']['toggle'](); })
EM_JS(float, wake_js_mouse_movement, (void), { return Module['boatMouse']['movement'](); })
EM_JS(float, wake_js_mouse_scroll, (void), { return Module['boatMouse']['scroll'](); })

void wake_touch_session(bool enabled) { wake_js_mobile_session(enabled); }
bool wake_touch_menu_pressed(void) { return wake_js_mobile_menu() != 0; }
int wake_touch_buttons(bool active) { return wake_js_mobile_sample(active); }
float wake_touch_target(void) { return wake_js_mobile_target(); }
int wake_touch_button_height(int width) { return wake_js_mobile_size(width); }
int wake_touch_state(void) { return wake_js_mobile_state(); }
void wake_touch_layout(const float *rects) { wake_js_mobile_layout(rects); }
void wake_touch_status(char *text, int capacity) { wake_js_mobile_status(text, capacity); }
int wake_touch_tap(float *position) { return wake_js_touch_click(position); }

bool wake_mouse_toggle_pressed(void) { return wake_js_mouse_toggle() != 0; }
int wake_mouse_capture(bool wanted, bool available, bool captured)
{
    (void)captured; // The page tracks its own capture.
    return wake_js_mouse_state(wanted, available);
}
float wake_mouse_movement(float native_delta)
{
    (void)native_delta;
    float movement = wake_js_mouse_movement();
    (void)wake_js_mouse_scroll(); // Drain browser input; scrolling no longer applies thrust.
    return movement;
}
bool wake_fullscreen_key_pressed(void) { return false; }
