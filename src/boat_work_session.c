#include "boat_work_session.h"

#include "raymath.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

void boat_work_hash_bytes(uint64_t *h, const void *bytes, size_t size)
{
    const unsigned char *p = bytes;
    for (size_t i = 0; i < size; ++i)
        *h = (*h ^ p[i]) * UINT64_C(1099511628211);
}

const BoatCargoCrane *boat_work_active(const BoatWorkSession *e)
{
    return &e->sandbox.cranes[e->sandbox.active];
}

// The next visible site after FROM, for helm guidance; hidden sites are not there yet.
unsigned boat_work_next_site(const BoatWorkSession *e, unsigned from)
{
    unsigned n = e->sandbox.crane_count;
    for (unsigned i = 1; i <= n; ++i)
        if (!e->sandbox.cranes[(from + i) % n].hidden)
            return (from + i) % n;
    return from;
}

// KIND's template: the package's own notice, else the native default. Each {site} token becomes
// the site's name as a sign: capitals with underscores as spaces. The notice owns its text.
void boat_work_notice_text(
    const BoatWorkSession *e, BoatSceneNotice kind, unsigned site, char *out, size_t capacity)
{
    static const char *const defaults[BOAT_SCENE_NOTICES]
        = { "Crate delivered. It stays counted, even if moved again.", "{site} built.",
              "Supplies used. Next crate waits on the {site} pad.",
              "Crate returned to the {site} pad." };
    const char *format
        = e->world.scene && e->world.scene->work_present && e->world.scene->work_notice[kind][0]
        ? e->world.scene->work_notice[kind]
        : defaults[kind];
    char name[sizeof(e->sites[site].id)];
    snprintf(name, sizeof(name), "%s", e->sites[site].id);
    for (char *c = name; *c; ++c)
        *c = *c == '_' ? ' ' : (char)toupper((unsigned char)*c);
    size_t n = 0;
    for (const char *p = format; *p && n + 1 < capacity;) {
        if (!strncmp(p, "{site}", 6)) {
            for (const char *q = name; *q && n + 1 < capacity; ++q)
                out[n++] = *q;
            p += 6;
        } else
            out[n++] = *p++;
    }
    out[n] = 0;
}

void boat_work_notice(BoatWorkSession *e, BoatSceneNotice kind, unsigned site)
{
    boat_work_notice_text(e, kind, site, e->notice_text, sizeof(e->notice_text));
    e->notice = e->notice_text;
}

void boat_work_job_suspend(BoatWorkSession *e, const char *message)
{
    e->objective.quarantined = true;
    fprintf(stderr, "Cargo job suspended: %s\n", message);
}

// A fresh job for a fresh session: new generation, no queued events, init at tick zero.
void boat_work_job_reset(BoatWorkSession *e)
{
    e->event_count = 0;
    e->sequence = 0;
    e->waiting = NULL;
    e->guide = e->site_count;
    e->objective = (BoatPluginObjective) {
        .generation = ++e->generation, .rng = 12345, .work_enabled = true
    };
    if (!e->revision)
        return;
    e->objective.state_version = e->revision->free.manifest.state_version;
    BoatPluginDiagnostic d = { 0 };
    if (boat_plugin_objective_step(&e->objective, e->revision->source, e->revision->source_size,
            BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0, NULL, &d)
        != BOAT_PLUGIN_OK)
        boat_work_job_suspend(e, d.message);
}

// Move the native cargo events into the job's unacknowledged queue (and raise the delivery
// notice). Saving does the same first, so a key action's event is never left in the core.
void boat_work_job_queue_events(BoatWorkSession *e)
{
    static const char *const kinds[]
        = { "cargo.secured", "cargo.delivered", "cargo.lost", "cargo.reset" };
    BoatCargoEvent native[BOAT_CARGO_EVENTS];
    unsigned count = boat_cargo_take_events(&e->sandbox, native, BOAT_CARGO_EVENTS);
    for (unsigned i = 0; i < count; ++i) {
        if (native[i].kind == BOAT_CARGO_EVENT_DELIVERED)
            boat_work_notice(e, BOAT_SCENE_NOTICE_DELIVERED, native[i].site);
        if (!e->revision || e->objective.quarantined)
            continue;
        if (e->event_count == BOAT_PLUGIN_EVENTS) {
            boat_work_job_suspend(e, "event queue full");
            break;
        }
        BoatPluginEvent *event = &e->events[e->event_count++];
        *event = (BoatPluginEvent) {
            .tick = (int64_t)e->tick, .sequence = ++e->sequence, .load = native[i].load
        };
        snprintf(event->kind, sizeof(event->kind), "%s", kinds[native[i].kind]);
        snprintf(event->id, sizeof(event->id), "%s", e->sites[native[i].site].id);
    }
}

// After each tick: queue the native cargo events, and at each 12-tick boundary run the update.
// Its work.enable intents are checked against the live boat and crate before the batch commits;
// a refusal keeps the previous checkpoint and its unacknowledged events for the next boundary.
void boat_work_job_tick(BoatWorkSession *e)
{
    boat_work_job_queue_events(e);
    if (!e->revision || e->objective.quarantined || e->tick % 12)
        return;
    BoatPluginObjective next = e->objective;
    BoatPluginDiagnostic d = { 0 };
    if (boat_plugin_objective_step(&next, e->revision->source, e->revision->source_size,
            BOAT_PLUGIN_OBJECTIVE_UPDATE, (int64_t)e->tick, e->events, e->event_count, NULL, &d)
        != BOAT_PLUGIN_OK) {
        boat_work_job_suspend(e, d.message);
        return;
    }
    // Check every intent against the live sandbox first; apply them only if all can happen.
    bool build[BOAT_SCENE_WORK_SITES] = { false };
    unsigned supply = e->site_count, guide = e->site_count;
    for (unsigned i = 0; i < next.command_count; ++i) {
        const char *kind = next.commands[i].kind;
        bool enable = !strcmp(kind, "work.enable"), supplying = !strcmp(kind, "cargo.supply");
        if (!enable && !supplying && strcmp(kind, "work.guide"))
            continue;
        unsigned site = 0;
        while (site < e->site_count && strcmp(e->sites[site].id, next.commands[i].id))
            ++site;
        if (site == e->site_count) {
            boat_work_job_suspend(e, "a work command names no work site");
            return;
        }
        if (supplying)
            supply = site;
        else if (!enable)
            guide = site;
        else if (e->sandbox.cranes[site].hidden) { // Building a visible site is idempotent.
            if (boat_cargo_site_occupied(&e->sandbox, site, &e->boat)) {
                e->waiting = "BUILD WAITING: MOVE CLEAR";
                return;
            }
            build[site] = true;
        }
    }
    if (guide < e->site_count && e->sandbox.cranes[guide].hidden && !build[guide]) {
        boat_work_job_suspend(e, "work.guide names a site that is not built");
        return;
    }
    if (supply < e->site_count) {
        if (e->sites[supply].preset != BOAT_SCENE_WORK_SERVICE_PLATFORM
            || (e->sandbox.cranes[supply].hidden && !build[supply])) {
            boat_work_job_suspend(e, "cargo.supply names no built service platform");
            return;
        }
        if (!boat_cargo_supply_ready(&e->sandbox)) {
            e->waiting = "SUPPLY WAITING: SET CRATE DOWN";
            return;
        }
    }
    for (unsigned i = 0; i < e->site_count; ++i)
        if (build[i] && boat_cargo_site_enable(&e->sandbox, i, &e->boat))
            boat_work_notice(e, BOAT_SCENE_NOTICE_BUILT, i);
    if (supply < e->site_count && boat_cargo_supply(&e->sandbox, supply))
        boat_work_notice(e, BOAT_SCENE_NOTICE_SUPPLIED, supply);
    if (guide < e->site_count)
        e->guide = e->target = guide;
    e->waiting = NULL;
    e->objective = next;
    e->event_count = 0; // The update acknowledged every queued event.
}

// The world, cargo and controls of a fresh session, without the job.
void boat_work_session_reset_world(BoatWorkSession *e)
{
    // The reset must keep the package's scene: its notices and workboat drive this session.
    const BoatSceneDefinition *scene = e->world.scene;
    boat_world_init(&e->world, e->resources, boat_water_gentle());
    e->world.scene = scene;
    e->world.water.upstream = e->resources->definition->upstream;
    e->world.water.player_wake_enabled = true;
    boat_cargo_reset_sites_with(
        &e->sandbox, e->world.scene ? e->world.scene->work_boat : 0, e->sites, e->site_count);
    e->boat = (Boat) { .position = boat_cargo_berth(boat_work_active(e)),
        .heading = boat_work_active(e)->heading };
    e->target = boat_work_next_site(e, e->sandbox.active);
    e->tick = 0;
    e->crane_view = true;
    e->aim = 0;
    e->paused = false;
    e->braking = false;
    e->notice = "F: catch crate. 2 / 3: lift and aim at deck.";
}

void boat_work_session_reset(BoatWorkSession *e)
{
    boat_work_session_reset_world(e);
    boat_work_job_reset(e);
}

// SV1c: the session as a save record. The package's identity compares the work digest of its
// scene, the manifest state_version and the physics revision; the source hash and build string
// are for messages only.
void boat_work_revision_identity(
    const BoatPluginRevision *revision, const char *writer, BoatSandboxIdentity *identity)
{
    const BoatPluginManifest *m = &revision->free.manifest;
    *identity
        = (BoatSandboxIdentity) { .work_digest = boat_scene_work_digest(&revision->free.scene),
              .source_hash = boat_sandbox_source_hash(revision->source, revision->source_size),
              .state_version = m->state_version,
              .physics_revision = BOAT_PHYSICS_REVISION,
              .tick_hz = BOAT_TICK_HZ };
    snprintf(identity->package, sizeof(identity->package), "%s", m->id);
    snprintf(identity->build, sizeof(identity->build), "%s", writer ? writer : "unknown");
}

void boat_work_session_identity(const BoatWorkSession *e, BoatSandboxIdentity *id)
{
    boat_work_revision_identity(e->revision, e->writer, id);
}

// Between ticks only. A key action's core events move into the job's queue first; nothing
// else is re-derived. Jobless sessions (the basin reference) have nothing to save.
bool boat_work_session_save(BoatWorkSession *e, int64_t saved_at, BoatSandboxSave *save)
{
    if (!e->revision)
        return false;
    boat_work_job_queue_events(e);
    memset(save, 0, sizeof(*save));
    boat_cargo_snapshot(&e->sandbox, &e->boat, &save->cargo);
    BoatSandboxSummary *sum = &save->summary;
    boat_work_session_identity(e, &sum->identity);
    const BoatPluginManifest *m = &e->revision->free.manifest;
    snprintf(sum->name, sizeof(sum->name), "%s", m->name);
    snprintf(sum->location, sizeof(sum->location), "%s", m->location);
    sum->saved_at = saved_at;
    sum->tick = e->tick;
    sum->load = e->sandbox.cargo.load;
    sum->sites = e->site_count;
    for (unsigned i = 0; i < e->site_count; ++i) {
        snprintf(save->site_ids[i], sizeof(save->site_ids[i]), "%s", e->sites[i].id);
        sum->built += !e->sandbox.cranes[i].hidden;
    }
    if (e->objective.hud_count) {
        sum->has_progress = true;
        snprintf(sum->progress_label, sizeof(sum->progress_label), "%s", e->objective.hud[0].label);
        sum->progress_value = e->objective.hud[0].value;
    }
    save->guide = e->guide;
    save->target = e->target;
    save->has_objective = true;
    save->objective = e->objective;
    memset(save->objective.commands, 0, sizeof(save->objective.commands));
    save->objective.command_count = 0; // Inspection only; not part of the checkpoint.
    memcpy(save->events, e->events, e->event_count * sizeof(e->events[0]));
    save->event_count = e->event_count;
    save->generation = e->generation;
    save->sequence = e->sequence;
    return true;
}

// Replace the session with SAVE, or return false leaving it untouched when the record does not
// belong to this package's sites or the core refuses it. Resume never queues events and never
// re-runs the job's init. The boat resumes where the core puts it (at rest), the crane at rest.
bool boat_work_session_resume(BoatWorkSession *e, const BoatSandboxSave *save)
{
    if (!e->revision || !save->has_objective || save->summary.sites != e->site_count
        || save->guide > e->site_count || save->target >= e->site_count)
        return false;
    for (unsigned i = 0; i < e->site_count; ++i)
        if (strcmp(save->site_ids[i], e->sites[i].id))
            return false;
    BoatCargoSandbox sandbox;
    Boat boat;
    if (!boat_cargo_restore(&sandbox, e->revision->free.scene.work_boat, e->sites, e->site_count,
            &save->cargo, &boat))
        return false;
    boat_work_session_reset_world(e);
    e->sandbox = sandbox;
    e->boat = boat;
    e->tick = save->summary.tick;
    e->crane_view = e->sandbox.moored;
    e->guide = save->guide;
    e->target = save->target;
    e->notice = "Resumed the saved session.";
    e->waiting = NULL;
    e->objective = save->objective;
    memset(e->events, 0, sizeof(e->events));
    memcpy(e->events, save->events, save->event_count * sizeof(e->events[0]));
    e->event_count = save->event_count;
    e->sequence = save->sequence;
    e->generation = save->objective.generation;
    return true;
}

void boat_work_session_tick(
    BoatWorkSession *e, float slew, float reach, float hoist, BoatInput input)
{
    BoatCargoSandbox *s = &e->sandbox;
    if (e->crane_view && s->moored) {
        if (slew || reach || hoist)
            e->aim = 0;
        BoatCargoCrane *crane = &s->cranes[s->active];
        if (e->aim) {
            Vector3 target = e->aim == 1
                ? boat_cargo_site_point(crane, (Vector3) { -5, 1, 0 })
                : boat_pose_attachment(&e->boat, boat_cargo_deck_aim_point(e->aim));
            Vector3 local = boat_cargo_site_local(crane, target);
            float dx = local.x + 4;
            float dz = local.z - 3;
            float angle = atan2f(dx, dz);
            float radius = hypotf(dx, dz);
            // Lift clear before slewing; the final lowering is the player's action.
            if (crane->rope > .9f)
                hoist = 1;
            else {
                slew = Clamp(remainderf(angle - crane->angle, 2 * PI) * 3, -1, 1);
                reach = Clamp((radius - crane->reach) * 2, -1, 1);
            }
        }
        boat_cargo_motor(crane, slew, reach, hoist);
    }
    Boat before = e->boat;
    bool assisting = s->mooring;
    BoatHandling handling = { .thrust = s->boat->thrust, .hull = &s->boat->hull };
    boat_step_handling(&e->boat, &e->world, boat_tick_seconds(++e->tick),
        s->moored || s->mooring ? (BoatInput) { 0 } : input, &handling);
    if (assisting) {
        // Keep dynamic wave pose, then let the surveyed assist own planar motion exactly once.
        e->boat.position = before.position;
        e->boat.heading = before.heading;
        e->boat.velocity = before.velocity;
        e->boat.yaw_velocity = before.yaw_velocity;
    }
    boat_cargo_load_effect(s, &before, &e->boat);
    if (e->braking && !s->moored)
        boat_cargo_brake(&e->boat);
    boat_cargo_moor_step(s, &e->boat);
    if (s->moored) {
        // Assisted berth constrains only horizontal movement; wave heave/roll remain live.
        e->boat.position = boat_cargo_berth(boat_work_active(e));
        e->boat.velocity = (Vector2) { 0 };
        e->boat.heading = boat_work_active(e)->heading;
        e->boat.yaw_velocity = 0;
        e->boat.throttle = e->boat.engine_response = 0;
    } else {
        // Conservative circle against each solid test quay; this fixture is not scene editing.
        for (unsigned i = 0; i < s->crane_count; ++i) {
            if (s->cranes[i].hidden)
                continue;
            Vector3 local = boat_cargo_site_local(
                &s->cranes[i], (Vector3) { -e->boat.position.x, 0, e->boat.position.y });
            float nearest_x = Clamp(local.x, -6.5f, -3.5f), nearest_z = Clamp(local.z, -2, 4);
            Vector2 away = { local.x - nearest_x, local.z - nearest_z };
            if (Vector2Length(away) < 2.65f) {
                e->boat.position = before.position;
                e->boat.velocity = Vector2Scale(before.velocity, -.12f);
                break;
            }
        }
    }
    boat_cargo_step(s, &e->boat, &e->world, boat_tick_seconds(e->tick));
    boat_work_job_tick(e);
}

void boat_work_hook_action(BoatWorkSession *e)
{
    e->notice = boat_cargo_hook_toggle(&e->sandbox)
        ? "Hook changed. Lower to a surface before releasing."
        : "Move hook near the crate's top. Unlash first; moor to operate.";
    e->aim = 0;
}

void boat_work_lash_action(BoatWorkSession *e)
{
    if (e->sandbox.mooring) {
        e->notice = "Wait for mooring to finish, or G to cancel, before unlashing.";
        return;
    }
    e->notice = boat_cargo_lash_toggle(&e->sandbox, &e->boat)
        ? "Lashing changed. Green crate + straps = secured."
        : "Detach hook, settle fully inside deck, stop boat, then lash.";
}

void boat_work_moor_action(BoatWorkSession *e)
{
    bool moored = e->sandbox.moored;
    bool changed = boat_cargo_moor_toggle(&e->sandbox, &e->boat);
    if (changed && e->sandbox.mooring)
        e->target = e->sandbox.active;
    else if (changed && moored) // Departure: guide toward the job's site, else the next one.
        e->target = e->guide < e->site_count && e->guide != e->sandbox.active
            ? e->guide
            : boat_work_next_site(e, e->sandbox.active);
    e->notice = changed
        ? (e->sandbox.moored           ? "Moored. TAB: crane controls."
                  : e->sandbox.mooring ? "Mooring assist active. G cancels."
                                       : "Ready to drive. TAB: helm controls.")
        : (e->sandbox.moored
                  ? "Release hook and lash loose deck cargo before departure."
                  : "Approach the berth outline; SPACE brakes; G starts mooring assist.");
    e->aim = 0;
}

void boat_work_recover_action(BoatWorkSession *e)
{
    if (boat_cargo_recover(&e->sandbox)) {
        boat_work_notice(e, BOAT_SCENE_NOTICE_RETURNED, e->sandbox.source);
    } else
        e->notice = "Release the hook and unlash before returning the crate.";
    e->aim = 0;
}

void boat_work_view_action(BoatWorkSession *e)
{
    if (e->crane_view || e->sandbox.moored) {
        e->crane_view = !e->crane_view;
        e->aim = 0;
        e->boat.throttle = 0;
        e->notice = e->crane_view ? "Crane controls active: A/D slew, W/S reach, Q/E hoist."
                                  : "Helm controls active: arrows drive, hold SPACE to brake.";
    } else
        e->notice = "Moor at a work berth to operate its crane.";
}

bool boat_work_session_load(BoatWorkSession *e, const unsigned char *bytes, size_t size,
    const char **reason, BoatSandboxSummary *summary)
{
    static BoatSandboxSave record;
    BoatSandboxSaveStatus status = boat_sandbox_save_decode(bytes, size, &record);
    if (status != BOAT_SANDBOX_SAVE_OK) {
        *reason = status == BOAT_SANDBOX_SAVE_NEWER ? "NEWER FORMAT" : "DAMAGED";
        return false;
    }
    BoatSandboxIdentity now;
    static char text[48];
    boat_work_session_identity(e, &now);
    if (boat_sandbox_save_check(&record.summary.identity, &now, false, text, sizeof(text))
        != BOAT_SANDBOX_RESUME) {
        *reason = text;
        return false;
    }
    if (!boat_work_session_resume(e, &record)) {
        *reason = "PACKAGE CHANGED";
        return false;
    }
    if (summary)
        *summary = record.summary;
    return true;
}

// Scripts: deterministic sequences of the real actions, used by review fixtures and the
// game's scripted captures. They run many ticks inline; a staged pose stands in for a crossing.
// Lift the crate on the moored site's pad to the deck's AIM target (2 left, 3 right), release
// and lash it, through the real hook, aim, release and lash paths.
bool boat_work_script_load_and_lash(BoatWorkSession *e, int aim)
{
    if (!boat_cargo_hook_toggle(&e->sandbox))
        return false;
    e->aim = aim;
    for (int i = 0; i < 2000; ++i)
        boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
    e->aim = 0;
    for (int i = 0; i < 900 && !e->sandbox.cargo.deck_contact; ++i)
        boat_work_session_tick(e, 0, 0, -.5f, (BoatInput) { 0 });
    if (!e->sandbox.cargo.deck_contact || !boat_cargo_hook_toggle(&e->sandbox))
        return false;
    for (int i = 0; i < 600 && e->sandbox.cargo.settled < .5f; ++i)
        boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
    return boat_cargo_lash_toggle(&e->sandbox, &e->boat);
}

// Pose the unmoored boat at SITE's approach (in place of a crossing) and moor there by the real
// G path. With APPROACH_ONLY, stop once the berth reads ready.
bool boat_work_script_moor_at(BoatWorkSession *e, unsigned site, bool approach_only)
{
    const BoatCargoCrane *crane = &e->sandbox.cranes[site];
    e->crane_view = false;
    Vector3 approach = boat_cargo_site_point(crane, (Vector3) { 1.8f, 0, 4.3f });
    e->boat.position = (Vector2) { -approach.x, approach.z };
    e->boat.heading = crane->heading + .5f;
    e->boat.velocity = (Vector2) { 0 };
    boat_cargo_step(&e->sandbox, &e->boat, &e->world, boat_tick_seconds(e->tick));
    BoatCargoMoorStatus status = boat_cargo_moor_status(&e->sandbox, &e->boat);
    if (status.site != site || status.reason != BOAT_CARGO_MOOR_READY)
        return false;
    if (approach_only)
        return true;
    boat_work_moor_action(e);
    for (int i = 0; i < 900 && !e->sandbox.moored; ++i)
        boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
    if (!e->sandbox.moored || e->sandbox.active != site)
        return false;
    boat_work_view_action(e);
    return true;
}

// Carry the lashed crate (lashed on the left target) to RECEIVER, moor and unload it onto that
// site's pad with its crane, until the native delivery commits there. The job may use the
// delivered crate up meanwhile, so the commit is watched tick by tick.
bool boat_work_script_unload_at(
    BoatWorkSession *e, unsigned receiver, bool approach_only, bool stop_at_pending_delivery)
{
    if (!boat_work_script_moor_at(e, receiver, approach_only))
        return false;
    if (approach_only)
        return true;
    if (!boat_cargo_lash_toggle(&e->sandbox, &e->boat))
        return false;
    e->aim = 2;
    for (int i = 0; i < 2000; ++i)
        boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
    e->aim = 0;
    Vector3 top = { 0, BOAT_CARGO_HALF, 0 };
    for (int i = 0; i < 900
        && Vector3Distance(boat_cargo_hook(&e->sandbox), Vector3Add(e->sandbox.cargo.position, top))
            > .6f;
        ++i)
        boat_work_session_tick(e, 0, 0, -.5f, (BoatInput) { 0 });
    if (!boat_cargo_hook_toggle(&e->sandbox))
        return false;
    e->aim = 1;
    for (int i = 0; i < 2000; ++i)
        boat_work_session_tick(e, 0, 0, 0, (BoatInput) { 0 });
    e->aim = 0;
    for (int i = 0; i < 900 && e->sandbox.cargo.position.y > 1.41f; ++i)
        boat_work_session_tick(e, 0, 0, -.5f, (BoatInput) { 0 });
    if (!boat_cargo_hook_toggle(&e->sandbox))
        return false;
    unsigned load = e->sandbox.cargo.load;
    bool delivered = false;
    for (int i = 0; i < 240; ++i) {
        boat_work_session_tick(e, 0, 0, i < 120 ? 1 : 0, (BoatInput) { 0 });
        delivered |= e->sandbox.cargo.load == load && e->sandbox.cargo.state == BOAT_CARGO_PAD
            && e->sandbox.cargo.pad == receiver && e->sandbox.cargo.receivers & 1u << receiver;
        if (stop_at_pending_delivery && delivered && e->event_count)
            return true;
    }
    return delivered;
}

// The committed value of the job's HUD row LABEL, or -1 when there is none.
int64_t boat_work_hud_value(const BoatWorkSession *e, const char *label)
{
    for (unsigned i = 0; i < e->objective.hud_count; ++i)
        if (!strcmp(e->objective.hud[i].label, label))
            return e->objective.hud[i].value;
    return -1;
}

// The review fixtures follow the bundled outer job by the package's own site ids: the landing
// receiver, the quay (a service platform the job builds) and the jetty receiver. SITE_COUNT
// means the package has no such site.
unsigned boat_work_find_site(const BoatWorkSession *e, const char *id)
{
    for (unsigned i = 0; i < e->site_count; ++i)
        if (!strcmp(e->sites[i].id, id))
            return i;
    return e->site_count;
}

bool boat_work_script_landing_delivery(BoatWorkSession *e)
{
    unsigned landing = boat_work_find_site(e, "landing");
    if (landing == e->site_count || !boat_work_script_load_and_lash(e, 2)
        || !boat_cargo_moor_toggle(&e->sandbox, &e->boat))
        return false;
    return boat_work_script_unload_at(e, landing, false, false);
}

BoatWorkWatch boat_work_watch(const BoatWorkSession *e)
{
    BoatWorkWatch w = { .position = e->boat.position,
        .heading = e->boat.heading,
        .progress = UINT64_C(14695981039346656037),
        .load = e->sandbox.cargo.load,
        .active = e->sandbox.active,
        .moored = e->sandbox.moored,
        .delivered = e->sandbox.cargo.delivered,
        .receivers = e->sandbox.cargo.receivers,
        .secured = e->sandbox.cargo.state == BOAT_CARGO_SECURED,
        .on_pad = e->sandbox.cargo.state == BOAT_CARGO_PAD };
    for (unsigned i = 0; i < e->sandbox.crane_count; ++i)
        w.built |= (unsigned)!e->sandbox.cranes[i].hidden << i;
    const BoatPluginObjective *o = &e->objective;
    boat_work_hash_bytes(&w.progress, o->state.bytes, o->state.size);
    for (unsigned i = 0; i < o->hud_count; ++i) {
        boat_work_hash_bytes(&w.progress, o->hud[i].label, strlen(o->hud[i].label) + 1);
        boat_work_hash_bytes(&w.progress, &o->hud[i].value, sizeof(o->hud[i].value));
    }
    boat_work_hash_bytes(&w.progress, &o->quarantined, sizeof(o->quarantined));
    return w;
}

// Dirty: the boat or the job moved on. Milestone: something the player would hate to repeat.
static void boat_work_watch_note(
    BoatAutosave *autosave, uint64_t tick, const BoatWorkWatch *before, const BoatWorkWatch *now)
{
    bool pose = before->position.x != now->position.x || before->position.y != now->position.y
        || before->heading != now->heading;
    bool milestone = before->progress != now->progress || before->built != now->built
        || before->load != now->load || before->active != now->active
        || before->moored != now->moored || before->delivered != now->delivered
        || before->receivers != now->receivers || before->secured != now->secured
        || before->on_pad != now->on_pad;
    boat_autosave_note(autosave, tick, pose || milestone, milestone);
}

// Once per frame, after its tick loop and outside every tick. LEAVING (pausing, losing focus)
// writes when dirty; a milestone writes now; otherwise the period decides. SHOW is whether the
// SAVED mark applies: only milestone and leaving writes show it.
bool boat_work_frame_due(const BoatWorkSession *e, BoatAutosave *autosave,
    BoatWorkFrameWatch *frame, bool paused, bool focused, bool *show)
{
    BoatWorkWatch now = boat_work_watch(e);
    boat_work_watch_note(autosave, e->tick, &frame->last, &now);
    frame->last = now;
    bool leaving = (paused && !frame->was_paused) || (frame->was_focused && !focused);
    frame->was_paused = paused;
    frame->was_focused = focused;
    if (!boat_autosave_due(autosave, e->tick, leaving))
        return false;
    *show = autosave->milestone || leaving;
    return true;
}
