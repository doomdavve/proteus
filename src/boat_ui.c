#include "boat_ui.h"
#include "boat_assets.h"
#include <math.h>

static Font ui, small;

BoatUiPalette boat_ui_palette(void)
{
#if PROTEUS_WARM_MENU
    return (BoatUiPalette) { { 29, 58, 83, 245 }, { 89, 153, 207, 255 }, { 255, 224, 112, 255 },
        { 255, 249, 218, 255 }, { 255, 218, 83, 255 }, { 24, 46, 68, 255 }, { 225, 72, 62, 255 },
        { 255, 238, 171, 255 }, { 20, 42, 58, 230 } };
#else
    return (BoatUiPalette) { { 8, 32, 54, 228 }, { 61, 133, 157, 255 }, { 142, 224, 226, 255 },
        { 190, 242, 235, 255 }, { 156, 241, 231, 255 }, { 11, 40, 62, 255 }, { 231, 128, 105, 255 },
        { 255, 196, 150, 255 }, { 10, 38, 57, 200 } };
#endif
}

bool boat_ui_load(void)
{
    if (ui.glyphs && small.glyphs)
        return true;
    boat_ui_unload();
    char path[4096];
    if (!boat_asset_path(path, sizeof(path), "fonts/m5x7.fnt"))
        return false;
    ui = LoadFont(path);
    if (ui.texture.id == GetFontDefault().texture.id) {
        ui = (Font) { 0 };
        return false;
    }
    SetTextureFilter(ui.texture, TEXTURE_FILTER_POINT);
    if (!boat_asset_path(path, sizeof(path), "fonts/m3x6.fnt"))
        return false;
    small = LoadFont(path);
    if (small.texture.id == GetFontDefault().texture.id) {
        small = (Font) { 0 };
        return false;
    }
    SetTextureFilter(small.texture, TEXTURE_FILTER_POINT);
    return IsFontValid(ui) && IsFontValid(small);
}

void boat_ui_unload(void)
{
    if (ui.glyphs)
        UnloadFont(ui);
    ui = (Font) { 0 };
    if (small.glyphs)
        UnloadFont(small);
    small = (Font) { 0 };
}

// Keep all glyphs on integer pixels at any requested overlay size.
static int text_scale(int size) { return size >= 16 ? size / 8 : 1; }

int boat_ui_width(const char *text, int size)
{
    if (!ui.glyphs)
        return MeasureText(text, size);
    return (int)roundf(MeasureTextEx(ui, text, 16 * text_scale(size), 0).x);
}

void boat_ui_text(const char *text, int x, int y, int size, Color color)
{
    if (!ui.glyphs) {
        DrawText(text, x, y, size, color);
        return;
    }
    int scale = text_scale(size);
    int top = ui.glyphs[GetGlyphIndex(ui, 'A')].offsetY;
    DrawTextEx(ui, text, (Vector2) { (float)x, (float)(y - top * scale) }, 16 * scale, 0, color);
}

// Instruments use the title's compact face. Standalone primitive review tools can
// retain their built-in lettering without loading any file-backed UI resources.
bool boat_ui_compact(const char *text, int center, int y, Color color)
{
    if (!small.glyphs)
        return false;
    int width = (int)roundf(MeasureTextEx(small, text, 16, 0).x);
    int top = small.glyphs[GetGlyphIndex(small, 'A')].offsetY;
    DrawTextEx(
        small, text, (Vector2) { (float)(center - width / 2), (float)(y - top) }, 16, 0, color);
    return true;
}
