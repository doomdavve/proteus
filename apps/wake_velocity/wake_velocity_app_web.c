#include "wake_velocity/wake_velocity_app.h"

#include "raylib.h"
#include <emscripten/emscripten.h>

EM_JS(void, wake_js_fit_canvas, (void), { Module['boatFitCanvas'](); })
EM_JS(int, wake_js_viewport_size, (int axis), {
    const size = Module['boatViewportSize']();
    return axis ? size.height : size.width;
})

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

int wake_app_canvas_scale(BoatResolution resolution, int width, int height)
{
    int scale = boat_resolution_window_scale(resolution);
    return resolution == BOAT_RESOLUTION_LOW
        ? scale
        : boat_screen_layout(resolution, width, height, width * scale, height * scale).scene_scale;
}

bool wake_app_viewport(int *width, int *height)
{
    *width = wake_js_viewport_size(0);
    *height = wake_js_viewport_size(1);
    return true;
}

void wake_app_canvas_fit(void) { wake_js_fit_canvas(); }

void wake_app_session_canvas(BoatResolution resolution, int width, int height)
{
    int window_scale = wake_app_canvas_scale(resolution, width, height);
    if (GetScreenWidth() < width * window_scale) {
        SetWindowSize(width * window_scale, height * window_scale);
        wake_app_canvas_fit();
    }
}

void wake_app_session_open(bool vsync)
{
    (void)vsync;
    SetTargetFPS(60);
}
