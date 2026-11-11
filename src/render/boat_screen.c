#include "boat_screen.h"
#include "rlgl.h"

#include <string.h>

static const char *const names[BOAT_RESOLUTIONS] = { "low", "high" };

bool boat_resolution_parse(const char *text, BoatResolution *resolution)
{
    for (int i = 0; i < BOAT_RESOLUTIONS; ++i)
        if (!strcmp(text, names[i])) {
            *resolution = (BoatResolution)i;
            return true;
        }
    return false;
}

const char *boat_resolution_name(BoatResolution resolution)
{
    return resolution < BOAT_RESOLUTIONS ? names[resolution] : names[0];
}

int boat_resolution_window_scale(BoatResolution resolution)
{
    return resolution == BOAT_RESOLUTION_HIGH ? BOAT_SCREEN_HIGH_SCALE : 3;
}

BoatScreenLayout boat_screen_layout(BoatResolution resolution, int logical_width,
    int logical_height, int screen_width, int screen_height)
{
    BoatScreenLayout layout = { 0 };
    int across = screen_width / logical_width, down = screen_height / logical_height;
    layout.scale = across < down ? across : down;
    if (layout.scale < 1)
        layout.scale = 1;
    layout.destination = (Rectangle) { (float)((screen_width - logical_width * layout.scale) / 2),
        (float)((screen_height - logical_height * layout.scale) / 2),
        (float)(logical_width * layout.scale), (float)(logical_height * layout.scale) };
    layout.scene_scale = 1;
    if (resolution == BOAT_RESOLUTION_HIGH) {
        int longest = logical_width > logical_height ? logical_width : logical_height;
        int scale = layout.scale < BOAT_SCREEN_HIGH_SCALE ? layout.scale : BOAT_SCREEN_HIGH_SCALE;
        while (scale > 1 && scale * longest > BOAT_SCREEN_MAX_TARGET)
            --scale;
        layout.scene_scale = scale;
        layout.scene_filtered = layout.scale % scale != 0;
    }
    layout.scene_width = logical_width * layout.scene_scale;
    layout.scene_height = logical_height * layout.scene_scale;
    layout.reflection_width = BOAT_SCREEN_REFLECTION_WIDTH * layout.scene_scale;
    layout.reflection_height = BOAT_SCREEN_REFLECTION_HEIGHT * layout.scene_scale;
    return layout;
}

void boat_screen_begin_overlay(void)
{
    ClearBackground(BLANK);
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA,
        RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
}

void boat_screen_end_overlay(void) { EndBlendMode(); }

void boat_screen_present_overlay(const BoatScreenLayout *layout, Texture2D scene, Texture2D overlay)
{
    SetTextureFilter(
        scene, layout->scene_filtered ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    DrawTexturePro(scene, (Rectangle) { 0, 0, -(float)scene.width, -(float)scene.height },
        layout->destination, (Vector2) { 0 }, 0, WHITE);
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    DrawTexturePro(overlay, (Rectangle) { 0, 0, (float)overlay.width, -(float)overlay.height },
        layout->destination, (Vector2) { 0 }, 0, WHITE);
    EndBlendMode();
}
