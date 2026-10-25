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
    unsigned host_mode; // HOST A ROOM: a BoatTitleHostMode, a boat_location id and the water.
    unsigned host_location;
    bool host_waves;
    char message[160]; // Shown under the menu, e.g. why the last connection ended.
} BoatTitleSettings;

typedef enum {
    BOAT_TITLE_EXIT,
    BOAT_TITLE_SINGLE,
    BOAT_TITLE_JOIN,
    BOAT_TITLE_HOST
} BoatTitleChoice;
typedef enum {
    BOAT_TITLE_HOST_FREE,
    BOAT_TITLE_HOST_DEATHMATCH,
    BOAT_TITLE_HOST_RACE,
    BOAT_TITLE_HOST_MODES
} BoatTitleHostMode;

// The command-line spelling of a host mode: free, deathmatch or race.
const char *boat_title_host_mode(unsigned mode);

// Remembered between launches in the per-user data directory; defaults when absent.
void boat_title_load(BoatTitleSettings *settings);
void boat_title_save(const BoatTitleSettings *settings);
// Run the menu until the player chooses. JOIN and HOST have already shown a status frame.
BoatTitleChoice boat_title_run(BoatTitleSettings *settings);
// Deterministic 320x200 capture of the main menu at TIME seconds.
bool boat_title_screenshot(const BoatTitleSettings *settings, double time, const char *path);
#endif
