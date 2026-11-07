#ifndef PROTEUS_BOAT_WORK_MENU_H
#define PROTEUS_BOAT_WORK_MENU_H

// SV1h: what the Single Player menu knows about working-harbour saves. Discovery is a bounded
// scan of a data directory that reads only each file's summary header; the SAVE row is a small
// pure state machine (Continue, New and Delete, the last two behind a confirmation). No raylib,
// no clock: callers pass the display time zone, and the row never touches a file.

#include "boat_sandbox_save.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOAT_WORK_MENU_SAVES 16

typedef struct {
    char name[128]; // File name in the directory, e.g. cargo-proteus.outer-work.sav.
    char path[1100];
    char package[64]; // From the header; empty when the header cannot be read.
    char summary[128]; // Upper case, e.g. OUTER - LOAD 2 - JETTY SUPPLIED: 1 - SAVED ...
    char reason[48]; // Why it is unavailable; empty when available.
    bool available;
} BoatWorkMenuSave;

// Scan DIRECTORY for cargo-*.sav (at most CAPACITY, never more than BOAT_WORK_MENU_SAVES),
// read the first BOAT_SANDBOX_SAVE_HEADER_MAX bytes of each, and judge the header against the
// INSTALLED packages. UTC_OFFSET (seconds) is the display time zone for the saved time. Never
// writes or deletes; a missing directory gives 0. Returns the entries found.
unsigned boat_work_menu_discover(const char *directory, const BoatSandboxIdentity *installed,
    unsigned installed_count, int utc_offset, BoatWorkMenuSave *out, unsigned capacity);
// "YYYY-MM-DD HH:MM" for UNIX_SECONDS shifted by UTC_OFFSET seconds. False when it cannot be shown.
bool boat_work_menu_format_time(int64_t unix_seconds, int utc_offset, char *out, size_t capacity);
// The file the package's slot uses: cargo-PACKAGE.sav. False when it does not fit.
bool boat_work_menu_file_name(const char *package, char *out, size_t capacity);
// The entry for PACKAGE's slot, or NULL.
const BoatWorkMenuSave *boat_work_menu_find(
    const BoatWorkMenuSave *saves, unsigned count, const char *package);
// Set a save the game cannot use aside as NAME.rejected, with its .bak (never deletes). True when
// nothing is left at PATH.
bool boat_work_menu_set_aside(const char *path);

typedef enum { BOAT_SAVE_CONTINUE, BOAT_SAVE_NEW, BOAT_SAVE_DELETE } BoatSaveMode;
typedef enum {
    BOAT_SAVE_NOTHING,
    BOAT_SAVE_START_CONTINUE,
    BOAT_SAVE_START_NEW,
    BOAT_SAVE_DELETE_FILE
} BoatSaveAction;

typedef struct {
    BoatSaveMode mode;
    bool present, available; // A file is in the slot; it can be continued.
    bool confirming; // Waiting for YES or NO.
    bool yes; // The confirmation's current choice; it starts at NO.
    BoatSaveAction pending; // START_NEW or DELETE_FILE while confirming.
} BoatSaveRow;

// (Re)describe the slot: Continue is the default when a save is available, otherwise New.
void boat_save_row_set(BoatSaveRow *row, bool present, bool available);
// Left (-1) or right (+1). Cycles Continue, New, Delete over the modes that apply; while
// confirming it switches YES and NO instead.
void boat_save_row_turn(BoatSaveRow *row, int turn);
// START. Continue starts at once; New over a save asks first; Delete does nothing here. While a
// question is open START answers it.
BoatSaveAction boat_save_row_start(BoatSaveRow *row);
// Enter or a click on the row itself. While confirming it answers (NO cancels). Otherwise
// Delete asks for confirmation and the other modes cycle.
BoatSaveAction boat_save_row_press(BoatSaveRow *row);
// Leave a question unanswered (Escape, or the selection moving elsewhere). True if one was open.
bool boat_save_row_cancel(BoatSaveRow *row);
// The row's value: "< CONTINUE >", "< NEW >", "< DELETE >", or the open question.
void boat_save_row_text(const BoatSaveRow *row, char *out, size_t capacity);

#endif
