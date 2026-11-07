#ifndef PROTEUS_BOAT_WORK_SESSION_H
#define PROTEUS_BOAT_WORK_SESSION_H

// The working-harbour session: the simulation of one boat, its work sites and crate, the
// package's job host, save and resume of that state, and the pure automatic-saving watch. It
// draws nothing, reads no input and touches no files (callers own bytes, windows and paths).

#include "boat.h"
#include "boat_cargo.h"
#include "boat_plugin_host.h"
#include "boat_sandbox_save.h"
#include "boat_world.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct BoatWorkSession {
    BoatWorldResources resources;
    BoatWorld world;
    Boat boat;
    BoatSceneWorkSite sites[BOAT_SCENE_WORK_SITES]; // The package's, or the basin reference.
    unsigned site_count;
    BoatCargoSandbox sandbox;
    uint64_t tick;
    bool crane_view, paused;
    bool braking;
    int aim; // 1 pad, 2/3 port/starboard. Motors, never a pose teleport.
    // The site helm guidance points to: after departure the job's guided site, else the next
    // one; N cycles.
    unsigned target;
    unsigned guide; // S1d: the job's work.guide site, or site_count for none.
    const char *notice;
    // A formatted notice; raylib's TextFormat buffers are reused within the next frame.
    char notice_text[64];
    // S1c: the package's job. Native cargo events queue here until an update commits them.
    const BoatPluginRevision *revision; // NULL: no job (the basin reference or no objective).
    BoatPluginObjective objective;
    BoatPluginEvent events[BOAT_PLUGIN_EVENTS];
    unsigned event_count, generation;
    int64_t sequence;
    // Why the last update's batch waits: a build the boat or crate occupies, or a supply whose
    // delivered crate is not set down yet. NULL when the job is not waiting.
    const char *waiting;
} BoatWorkSession;

// The session's active crane, and the next visible site after FROM for helm guidance.
const BoatCargoCrane *boat_work_active(const BoatWorkSession *e);
unsigned boat_work_next_site(const BoatWorkSession *e, unsigned from);

// KIND's template: the package's own notice, else the native default; {site} becomes the site's
// name as a sign. boat_work_notice also makes it the session's current notice.
void boat_work_notice_text(
    const BoatWorkSession *e, BoatSceneNotice kind, unsigned site, char *out, size_t capacity);
void boat_work_notice(BoatWorkSession *e, BoatSceneNotice kind, unsigned site);

// The job: suspend it, restart it for a fresh session, move native cargo events into its queue,
// and run the per-tick update (commits at each 12-tick boundary).
void boat_work_job_suspend(BoatWorkSession *e, const char *message);
void boat_work_job_reset(BoatWorkSession *e);
void boat_work_job_queue_events(BoatWorkSession *e);
void boat_work_job_tick(BoatWorkSession *e);

// A fresh session. The world form leaves the job alone; both keep the package's scene.
void boat_work_session_reset_world(BoatWorkSession *e);
void boat_work_session_reset(BoatWorkSession *e);

// One 120 Hz tick of the boat, crane motors and cargo, then the job.
void boat_work_session_tick(
    BoatWorkSession *e, float slew, float reach, float hoist, BoatInput input);

// Key actions; each sets the notice.
void boat_work_hook_action(BoatWorkSession *e);
void boat_work_lash_action(BoatWorkSession *e);
void boat_work_moor_action(BoatWorkSession *e);
void boat_work_recover_action(BoatWorkSession *e);
void boat_work_view_action(BoatWorkSession *e);

// Save and resume, between ticks. SAVE refuses a jobless session. RESUME replaces the session
// or leaves it untouched. LOAD decodes BYTES, compares the identity and resumes: it returns
// false with a short REASON, and fills SUMMARY (when not NULL) on success.
void boat_work_session_identity(const BoatWorkSession *e, BoatSandboxIdentity *id);
bool boat_work_session_save(BoatWorkSession *e, int64_t saved_at, BoatSandboxSave *save);
bool boat_work_session_resume(BoatWorkSession *e, const BoatSandboxSave *save);
bool boat_work_session_load(BoatWorkSession *e, const unsigned char *bytes, size_t size,
    const char **reason, BoatSandboxSummary *summary);

// Automatic saving. A watch is what a frame changes; the frame check feeds the pure policy
// (BoatAutosave) once per frame, after the tick loop, and says whether a write is due now.
typedef struct {
    Vector2 position;
    float heading;
    uint64_t progress; // The committed job state, HUD and timers, not its tick.
    unsigned built, load, active;
    bool moored, delivered, secured, on_pad;
    unsigned char receivers;
} BoatWorkWatch;
typedef struct {
    BoatWorkWatch last;
    bool was_paused, was_focused;
} BoatWorkFrameWatch;

void boat_work_hash_bytes(uint64_t *h, const void *bytes, size_t size);
BoatWorkWatch boat_work_watch(const BoatWorkSession *e);
bool boat_work_frame_due(const BoatWorkSession *e, BoatAutosave *autosave,
    BoatWorkFrameWatch *frame, bool paused, bool focused, bool *show);

#endif
