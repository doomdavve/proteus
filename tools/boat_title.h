#ifndef BOAT_TITLE_H
#define BOAT_TITLE_H
#include <stdbool.h>

// Wake Velocity title screen for wake_velocity's no-argument launch. It draws into the already open
// window with its own presentation clock and never touches a boat session.
typedef struct {
    char name[17]; // The multiplayer nick: BOAT_NET_NAME_BYTES.
    char address[64]; // HOST:PORT for MULTIPLAYER.
    unsigned location; // Single-player location; BOAT_LOCATION_COUNT selects the basin trial.
    bool waves, smooth_edges;
    bool high_resolution; // PICTURE HIGH RES: --resolution high, which replaces smooth_edges.
    unsigned skin; // Single-player livery; rooms colour boats by slot.
    bool mouse_steering; // Sessions start with captured mouse steering; in-game changes return.
    unsigned soundtrack; // Local music choice, including off; shared by single player and rooms.
    unsigned lighting; // Fixed local lighting preset, shared by single player and rooms.
    unsigned host_mode; // HOST A ROOM: a BoatTitleHostMode, a boat_location id and the water.
    unsigned host_location;
    bool host_waves;
    unsigned outer_content; // A BoatTitleOuterContent: the harbour's single-player package.
    char message[160]; // Shown under the menu, e.g. why the last connection ended.
} BoatTitleSettings;

typedef enum {
    BOAT_TITLE_EXIT,
    BOAT_TITLE_SINGLE,
    BOAT_TITLE_JOIN,
    BOAT_TITLE_HOST
} BoatTitleChoice;
// The CONTENT row on GOTHENBURG HARBOUR: the Långedrag voyage, Harbor life, the Grodan canal tour
// in the Grodan with its passengers, or the same tour in the player's own boat.
typedef enum {
    BOAT_TITLE_OUTER_STANDARD,
    BOAT_TITLE_OUTER_HARBOR_LIFE,
    BOAT_TITLE_OUTER_GRODAN,
    BOAT_TITLE_OUTER_CANAL_TOUR,
    BOAT_TITLE_OUTER_CONTENTS
} BoatTitleOuterContent;
// The saved spelling of a content, also the --title-start suffix: standard, harbor-life, grodan,
// canal-tour.
const char *boat_title_outer_content(unsigned content);
// The bundled package for a content.
const char *boat_title_outer_package(unsigned content);
typedef enum {
    BOAT_TITLE_HOST_FREE,
    BOAT_TITLE_HOST_DEATHMATCH,
    BOAT_TITLE_HOST_RACE,
    BOAT_TITLE_HOST_MODES
} BoatTitleHostMode;
typedef enum {
    BOAT_TITLE_LIGHTING_DAY,
    BOAT_TITLE_LIGHTING_SUNSET,
    BOAT_TITLE_LIGHTING_DUSK,
    BOAT_TITLE_LIGHTING_COUNT
} BoatTitleLighting;

// The command-line spelling of a host mode: free, deathmatch or race.
const char *boat_title_host_mode(unsigned mode);
// The command-line spelling of a fixed lighting preset: day, sunset or dusk.
const char *boat_title_lighting(unsigned lighting);

void boat_title_defaults(BoatTitleSettings *settings);
// Remembered between launches in the per-user data directory; defaults when absent.
void boat_title_load(BoatTitleSettings *settings);
void boat_title_save(const BoatTitleSettings *settings);
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
void boat_title_directory_start(void);
void boat_title_directory_stop(void);
#endif
// Run the menu until the player chooses. JOIN and HOST have already shown a status frame.
BoatTitleChoice boat_title_run(BoatTitleSettings *settings);
// Deterministic 320x200 capture of the main menu at TIME seconds.
bool boat_title_screenshot(const BoatTitleSettings *settings, double time, const char *path);
#endif
