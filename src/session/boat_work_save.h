#ifndef PROTEUS_BOAT_WORK_SAVE_H
#define PROTEUS_BOAT_WORK_SAVE_H

// The working-harbour session's save file and its automatic saving: one file per package in the
// data directory, an atomic write that keeps the previous file as NAME.bak, a resume that falls
// back to the backup, and NAME.rejected for a file that cannot be used. Saving is never a
// player action: the host calls boat_work_save_frame once per frame after its tick loop and
// boat_work_save_leave when it stops. It takes the wall-clock time for display from the caller,
// reads no clock and draws nothing.

#include "session/boat_work_session.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char path[1100];
    bool enabled;
    bool autosaving; // The live window loop writes; staged runs only write on --save-now.
    bool failed;
    int mark; // Presentation frames left to show SAVED.
    unsigned writes; // Successful automatic writes, for the autosave check.
    bool stale_digest; // Test fixture: write a save whose work digest no longer matches.
    BoatAutosave autosave;
} BoatWorkSave;

// Choose the file: EXPLICIT_PATH, else DATA_DIR (or the game's data directory when NULL)
// holding cargo-PACKAGE_ID.sav, creating the directory. Sets enabled.
void boat_work_save_path(
    BoatWorkSave *file, const char *explicit_path, const char *data_dir, const char *package_id);
// Write the session now. SAVED_AT is Unix seconds for the file's display header.
bool boat_work_save_write(BoatWorkSession *e, BoatWorkSave *file, int64_t saved_at);
// Resume from the file, else from its backup. A file that cannot be used is set aside as
// NAME.rejected (never deleted), with its backup, and the session starts fresh. Returns 0 when
// nothing was resumed or set aside, 1 when resumed, 2 when something was set aside.
int boat_work_save_load(BoatWorkSession *e, const BoatWorkSave *file);
// Once per frame, after its tick loop and outside every tick. LEAVING-type triggers (pausing,
// losing focus) write when dirty; a milestone writes now; otherwise the period decides. The
// SAVED mark shows only for milestone and leaving writes.
void boat_work_save_frame(BoatWorkSession *e, BoatWorkSave *file, BoatWorkFrameWatch *frame,
    bool paused, bool focused, int64_t saved_at);
// Leaving play (title, window close, quit): write when dirty. Returns whether the file is
// current afterwards.
bool boat_work_save_leave(BoatWorkSession *e, BoatWorkSave *file, int64_t saved_at);

#endif
