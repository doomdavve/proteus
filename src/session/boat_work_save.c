#include "session/boat_work_save.h"

#include "platform/boat_file.h"

#include <stdio.h>

void boat_work_save_path(
    BoatWorkSave *file, const char *explicit_path, const char *data_dir, const char *package_id)
{
    char directory[1000] = "";
    if (explicit_path)
        snprintf(file->path, sizeof(file->path), "%s", explicit_path);
    else if ((data_dir && snprintf(directory, sizeof(directory), "%s", data_dir) > 0)
        || boat_file_data_directory(directory, sizeof(directory)))
        snprintf(file->path, sizeof(file->path), "%s/cargo-%s.sav", directory, package_id);
    file->enabled = file->path[0] != 0;
    if (file->enabled && directory[0])
        boat_file_make_directory(directory);
}

bool boat_work_save_write(BoatWorkSession *e, BoatWorkSave *file, int64_t saved_at)
{
    static BoatSandboxSave record;
    static unsigned char bytes[BOAT_SANDBOX_SAVE_MAX];
    size_t size = 0;
    if (!boat_work_session_save(e, saved_at, &record))
        return false;
    if (file->stale_digest)
        record.summary.identity.work_digest ^= 1;
    return boat_sandbox_save_encode(&record, bytes, sizeof(bytes), &size)
        && boat_file_write_atomic(file->path, bytes, size);
}

// Try one file: true when it decoded, belongs to this package and the core resumed it.
// REASON gets the first problem otherwise.
static bool save_try(BoatWorkSession *e, const char *path, const char **reason)
{
    static unsigned char bytes[BOAT_SANDBOX_SAVE_MAX];
    size_t size = 0;
    BoatSandboxSummary summary;
    if (!boat_file_read(path, bytes, sizeof(bytes), &size)) {
        *reason = "DAMAGED";
        return false;
    }
    if (!boat_work_session_load(e, bytes, size, reason, &summary))
        return false;
    printf("Resumed save %s at tick %llu (load %u, %u of %u sites built)\n", path,
        (unsigned long long)summary.tick, summary.load, summary.built, summary.sites);
    return true;
}

int boat_work_save_load(BoatWorkSession *e, const BoatWorkSave *file)
{
    char backup[1100];
    snprintf(backup, sizeof(backup), "%s.bak", file->path);
    const char *reason = "DAMAGED";
    bool had_file = boat_file_exists(file->path), had_backup = boat_file_exists(backup);
    if (had_file && save_try(e, file->path, &reason))
        return 1;
    if (had_backup && save_try(e, backup, &reason)) {
        printf("Save %s was not usable (%s); resumed its backup\n", file->path, reason);
        return 1;
    }
    int result = 0;
    if (had_file && boat_file_set_aside(file->path, ".rejected")) {
        printf("Save %s set aside as %s.rejected: %s\n", file->path, file->path, reason);
        result = 2;
    }
    if (had_backup && boat_file_set_aside(backup, ".rejected")) {
        printf("Backup %s set aside as %s.rejected\n", backup, backup);
        result = 2;
    }
    return result;
}

void boat_work_save_frame(BoatWorkSession *e, BoatWorkSave *file, BoatWorkFrameWatch *frame,
    bool paused, bool focused, int64_t saved_at)
{
    bool show = false;
    if (!boat_work_frame_due(e, &file->autosave, frame, paused, focused, &show))
        return;
    bool ok = boat_work_save_write(e, file, saved_at);
    boat_autosave_written(&file->autosave, e->tick, ok);
    file->failed = !ok;
    file->writes += ok;
    if (ok && show)
        file->mark = 90; // 1.5 s of presentation at 60 frames.
    if (!ok)
        fprintf(stderr, "Could not write save %s\n", file->path);
}

bool boat_work_save_leave(BoatWorkSession *e, BoatWorkSave *file, int64_t saved_at)
{
    if (!boat_autosave_due(&file->autosave, e->tick, true)) {
        printf("Save %s is current at tick %llu\n", file->path, (unsigned long long)e->tick);
        return !file->failed;
    }
    bool ok = boat_work_save_write(e, file, saved_at);
    boat_autosave_written(&file->autosave, e->tick, ok);
    file->failed = !ok;
    printf("%s %s at tick %llu\n", ok ? "Saved" : "Could not save", file->path,
        (unsigned long long)e->tick);
    return ok;
}
