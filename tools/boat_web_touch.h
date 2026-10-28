#ifndef PROTEUS_BOAT_WEB_TOUCH_H
#define PROTEUS_BOAT_WEB_TOUCH_H
#if defined(__EMSCRIPTEN__)
// 1: completed touch tap, -1: touch owns the pointer, 0: use raylib mouse input.
int boat_web_touch_click(float *position);
#endif
#endif
