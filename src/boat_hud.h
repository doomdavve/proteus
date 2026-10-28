#ifndef PROTEUS_BOAT_HUD_H
#define PROTEUS_BOAT_HUD_H

#include <stdbool.h>

// Presentation-only telemetry, independent of session, physics and multiplayer ownership.
typedef struct {
    float speed_mps; // Ground-speed magnitude, including sideways/reverse motion.
    float thrust; // Actual command: -1 reverse, 0 neutral, +1 ahead (not engine response).
    float steering; // Wheel setting: -1 port, +1 starboard (not yaw or hull heading).
    bool precision; // Shift is held in active manual play.
    float reset_progress; // 0..1; zero hides the hold-to-reset prompt.
} BoatHudTelemetry;

typedef struct {
    float hull, heat, pressure, grenade_seconds, torpedo_seconds;
    unsigned torpedoes;
    bool overheated;
} BoatHudCombat;
// Matching side instruments around the existing controls, deathmatch callers only.
void boat_hud_combat_draw(BoatHudCombat combat, int width, int height);

// Three-by-five scene-pixel lettering: five rows of three-bit masks as the digits '0'..'7', the
// highest bit leftmost. NULL for a character without a glyph, which callers leave blank.
const char *boat_hud_glyph(char character);

// Draw at scene resolution (320x200) in a render texture, then present with the scene
// destination and nearest filtering. Composite over the scene before scaling so case
// transparency blends correctly. Anchored to the bottom edge. No retained state.
void boat_hud_draw(BoatHudTelemetry telemetry, int width, int height);

#endif
