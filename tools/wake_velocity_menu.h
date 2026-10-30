#ifndef WAKE_VELOCITY_MENU_H
#define WAKE_VELOCITY_MENU_H
#include "raylib.h"

#include <stdbool.h>

// The in-game P/Escape menu. It is drawn in the scene's 320x200 coordinates (through a Camera2D
// matching the integer-scaled destination) and never changes a session itself: the study acts on
// the returned choice. Solo play pauses while it is open; rooms keep running.
typedef enum {
    BOAT_MENU_NONE,
    BOAT_MENU_RESUME,
    BOAT_MENU_STEERING, // Toggle mouse / keyboard steering.
    BOAT_MENU_CONTROLS, // Open the keyboard guide over the menu.
    BOAT_MENU_FULLSCREEN, // Toggle borderless fullscreen (not offered on the web).
    BOAT_MENU_RESET, // Explicit reset/restart/request, using the session's existing action.
    BOAT_MENU_LEAVE,
} BoatMenuChoice;

typedef struct {
    bool open;
    int selected;
    double time;
    bool can_leave; // A shared title window lets a browser session return too.
    const char *reset_label; // RESET BOAT, RESTART RUN or REQUEST RESET.
    const char *leave_label; // QUIT, BACK TO TITLE, LEAVE ROOM or CLOSE ROOM.
    const char *room_hint; // Replaces the networked hint (a hosted room), if set.
} BoatMenu;

void boat_menu_open(BoatMenu *menu);
// Keyboard and pointer input for one frame; P/Escape stay with the caller.
BoatMenuChoice boat_menu_update(BoatMenu *menu, Camera2D camera, float dt);
void boat_menu_draw(const BoatMenu *menu, Camera2D camera, bool mouse_steering, bool networked);
#endif
