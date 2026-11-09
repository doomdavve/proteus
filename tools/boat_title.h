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
    unsigned lighting; // Fixed local lighting choice, shared by single player and rooms.
    unsigned host_mode; // HOST A ROOM: a BoatTitleHostMode, a boat_location id and the water.
    unsigned host_location;
    bool host_waves;
    unsigned outer_content; // A BoatTitleOuterContent: the harbour's single-player package.
    const char
        *data_dir; // Not remembered: --data-dir for the title, so tests never use the player's.
    bool work_new; // Set by START on WORKING HARBOUR: begin a new job instead of continuing.
    char message[160]; // Shown under the menu, e.g. why the last connection ended.
} BoatTitleSettings;

typedef enum {
    BOAT_TITLE_EXIT,
    BOAT_TITLE_SINGLE,
    BOAT_TITLE_JOIN,
    BOAT_TITLE_HOST
} BoatTitleChoice;
// The CONTENT row on GOTHENBURG HARBOUR: the Långedrag voyage, Harbor life, the Grodan canal tour
// in the Grodan with its passengers, the same tour in the player's own boat, or the working
// harbour (cargo jobs that save themselves; native builds only).
typedef enum {
    BOAT_TITLE_OUTER_STANDARD,
    BOAT_TITLE_OUTER_HARBOR_LIFE,
    BOAT_TITLE_OUTER_GRODAN,
    BOAT_TITLE_OUTER_CANAL_TOUR,
    BOAT_TITLE_OUTER_WORKING_HARBOUR,
    BOAT_TITLE_OUTER_CONTENTS
} BoatTitleOuterContent;
// The saved spelling of a content, also the --title-start suffix: standard, harbor-life, grodan,
// canal-tour, working-harbour.
const char *boat_title_outer_content(unsigned content);
// The bundled package for a content.
const char *boat_title_outer_package(unsigned content);
typedef enum {
    BOAT_TITLE_HOST_FREE,
    BOAT_TITLE_HOST_DEATHMATCH,
    BOAT_TITLE_HOST_RACE,
    BOAT_TITLE_HOST_HUNT,
    BOAT_TITLE_HOST_MODES
} BoatTitleHostMode;
typedef enum {
    BOAT_TITLE_LIGHTING_DAY,
    BOAT_TITLE_LIGHTING_SUNSET,
    BOAT_TITLE_LIGHTING_DUSK,
    BOAT_TITLE_LIGHTING_HOURS, // 00:00 is HOURS; the last hour is HOURS + 23.
    BOAT_TITLE_LIGHTING_COUNT = BOAT_TITLE_LIGHTING_HOURS + 24
} BoatTitleLighting;

// The command-line spelling of a host mode: free, deathmatch, race or hunt.
const char *boat_title_host_mode(unsigned mode);
// The saved spelling of a lighting choice: day, sunset, dusk or hour-v1:HH.
const char *boat_title_lighting(unsigned lighting);
// The selected hour of an hourly lighting choice, or -1 for a preset or invalid choice.
int boat_title_lighting_hour(unsigned lighting);
// The command-line option and value for a title launch, or false when EXTRA already contains
// --daylight or --lighting-hour. Both returned pointers are borrowed static storage.
bool boat_title_lighting_cli(
    unsigned lighting, int extra_count, char **extra, const char **option, const char **value);

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

// The working-harbour save slot as the SAVE row sees it (SV1h). The game side
// (wake_velocity_work.c) scans, deletes and sets aside; the title draws and asks.
typedef struct {
    bool present, available; // A file is in the slot; it can be continued.
    char summary[128]; // Upper case; empty when nothing is there.
    char reason[48]; // Why it is unavailable.
    char path[1100];
} BoatTitleSave;
// Scan DATA_DIR (NULL: the game's data directory) for the bundled work package's slot.
void wake_work_title_scan(const char *data_dir, BoatTitleSave *save);
// DELETE SAVE: remove the slot's file and its backup. True when neither remains.
bool wake_work_title_delete(const BoatTitleSave *save);
// NEW over an unavailable save: set it and its backup aside as .rejected (never deleted).
bool wake_work_title_set_aside(const BoatTitleSave *save);

// Capture options (--title --screenshot): the Single Player panel, with the SAVE row in a mode
// (0 continue, 1 new, 2 delete) and optionally with its confirmation open.
typedef struct {
    bool solo, ask;
    int save_mode;
} BoatTitleCapture;
bool boat_title_screenshot_panel(
    const BoatTitleSettings *settings, double time, const char *path, const BoatTitleCapture *cap);
#endif
