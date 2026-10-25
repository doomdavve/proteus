#ifndef BOAT_TITLE_H
#define BOAT_TITLE_H
#include <stdbool.h>

// Wake Velocity title screen for boat_study's no-argument launch. It draws into the already open
// window with its own presentation clock and never touches a boat session.
typedef struct {
    char name[17]; // The multiplayer nick: BOAT_NET_NAME_BYTES.
    char address[64]; // HOST:PORT for MULTIPLAYER.
    unsigned skin; // Single-player livery; rooms colour boats by slot.
    bool mouse_steering; // Sessions start with captured mouse steering; in-game changes return.
    unsigned soundtrack; // Local music choice, including off; shared by single player and rooms.
    char message[160]; // Shown under the menu, e.g. why the last connection ended.
} BoatTitleSettings;

typedef enum { BOAT_TITLE_EXIT, BOAT_TITLE_SINGLE, BOAT_TITLE_JOIN } BoatTitleChoice;

// Remembered between launches in the per-user data directory; defaults when absent.
void boat_title_load(BoatTitleSettings *settings);
void boat_title_save(const BoatTitleSettings *settings);
// Run the menu until the player chooses. JOIN has already shown a "connecting" frame.
BoatTitleChoice boat_title_run(BoatTitleSettings *settings);
// Deterministic 320x200 capture of the main menu at TIME seconds.
bool boat_title_screenshot(const BoatTitleSettings *settings, double time, const char *path);
#endif
