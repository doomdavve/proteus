#ifndef BOAT_UI_H
#define BOAT_UI_H
#include "raylib.h"

// Compile-time menu palette shared by the title and the in-game overlay.
typedef struct {
    Color panel, border, edge, text, selected, selected_text, accent, hint, shadow;
} BoatUiPalette;
BoatUiPalette boat_ui_palette(void);
// Call inside a graphics context; release before closing it. No device/file work in core.
bool boat_ui_load(void);
void boat_ui_unload(void);
void boat_ui_text(const char *text, int x, int y, int size, Color color);
bool boat_ui_compact(const char *text, int center, int y, Color color);
int boat_ui_width(const char *text, int size);
#endif
