#ifndef PROTEUS_BOAT_SCREEN_H
#define PROTEUS_BOAT_SCREEN_H
#include "raylib.h"

#include <stdbool.h>

// How the 3D scene is sized against the logical frame. The HUD, menus and hit targets always use
// the logical frame (320x200, or the browser's mobile size), integer-scaled with nearest filtering.
// Low renders the scene at the logical size, exactly as before. High renders it at up to
// BOAT_SCREEN_HIGH_SCALE logical pixels per side, matched to the window, with a reflection target
// grown by the same factor. High needs the GPU renderer and replaces --gpu-aa.
typedef enum { BOAT_RESOLUTION_LOW, BOAT_RESOLUTION_HIGH, BOAT_RESOLUTIONS } BoatResolution;

#define BOAT_SCREEN_HIGH_SCALE 4
#define BOAT_SCREEN_MAX_TARGET 2048 // Longest scene target side, inside WebGL2's minimum.
#define BOAT_SCREEN_REFLECTION_WIDTH 160 // Low reflection; high multiplies it.
#define BOAT_SCREEN_REFLECTION_HEIGHT 120

typedef struct {
    int scale; // Screen pixels per logical pixel, at least 1.
    Rectangle destination; // The logical frame on screen, centred.
    int scene_scale; // Scene pixels per logical pixel.
    int scene_width, scene_height;
    int reflection_width, reflection_height;
    // Scene pixels do not land on whole screen pixels (a window wider than the high scale): the
    // presentation filters bilinearly instead of with uneven nearest pixels.
    bool scene_filtered;
} BoatScreenLayout;

bool boat_resolution_parse(const char *text, BoatResolution *resolution);
const char *boat_resolution_name(BoatResolution resolution);
// The window's initial multiple of the logical frame: 3, or the high scale.
int boat_resolution_window_scale(BoatResolution resolution);
// LOGICAL and SCREEN sizes must be positive.
BoatScreenLayout boat_screen_layout(BoatResolution resolution, int logical_width,
    int logical_height, int screen_width, int screen_height);

// High resolution draws the HUD into a transparent logical target between these calls, which
// accumulate premultiplied colour, and then presents it over the scene.
void boat_screen_begin_overlay(void);
void boat_screen_end_overlay(void);
// Draw the GPU SCENE (an OpenGL render target, mirrored as every GPU scene is) and then the
// logical OVERLAY target into LAYOUT's destination, inside BeginDrawing or another target.
void boat_screen_present_overlay(
    const BoatScreenLayout *layout, Texture2D scene, Texture2D overlay);

#endif
