#include "wake_velocity_work.h"

#include "boat_cargo_view.h"
#include "boat_file.h"
#include "boat_plugin_catalog.h"
#include "boat_presentation.h"
#include "boat_time_control.h"
#include "boat_title.h"
#include "boat_ui.h"
#include "boat_work_menu.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Borrowed by the session for as long as the program runs.
static BoatPluginRevision revision;

bool wake_work_open(WakeWork *work, BoatSession *game, const BoatWorldResources *resources,
    const BoatWorldDefinition *location, char *diagnostic, size_t capacity)
{
    const BoatPluginCatalogEntry *entry = boat_plugin_catalog_work_default(location->name);
    BoatPluginDiagnostic d = { 0 };
    if (!entry || boat_plugin_kind_parse(entry->kind) != BOAT_PLUGIN_KIND_FREE
        || strcmp(entry->location, location->name)
        || !boat_plugin_catalog_decode(entry, &revision, &d) || !revision.free.has_scene
        || !revision.free.scene.work_present) {
        snprintf(diagnostic, capacity, "No bundled work package for %s.", location->name);
        return false;
    }
    *work = (WakeWork) { .open = true, .lighting_cycle = true };
    BoatWorkSession *s = &work->session;
    s->resources = resources;
    s->writer = "wake_velocity";
    s->world.scene = &revision.free.scene;
    s->revision = revision.free.manifest.objective ? &revision : NULL;
    s->site_count = revision.free.scene.work_count;
    memcpy(s->sites, revision.free.scene.work, sizeof(s->sites));
    boat_work_session_reset(s);
    wake_work_sync(work, game);
    return true;
}

// The session host management changes: the authority's once started, else the host's own.
static BoatWorkSession *authority(WakeWork *work)
{
    return work->local.running ? boat_work_local_authority(&work->local) : &work->session;
}

const BoatWorkSession *wake_work_view(const WakeWork *work)
{
    return work->local.running ? boat_work_local_view(&work->local) : &work->session;
}

bool wake_work_start(WakeWork *work, BoatSession *game, double now)
{
    work->local = (BoatWorkLocal) { 0 };
    if (!boat_work_local_start(&work->local, &work->session, 1, now))
        return false;
    wake_work_sync(work, game);
    return true;
}

void wake_work_stop(WakeWork *work) { boat_work_local_stop(&work->local); }

bool wake_work_reset(WakeWork *work, BoatSession *game, double now)
{
    BoatWorkSession *s = authority(work);
    boat_work_session_reset(s);
    // A reset returns the tick to zero. Mark the reset as a save milestone so an old last-write
    // tick cannot postpone the periodic write behind the new, lower tick.
    if (work->save.enabled)
        boat_autosave_note(&work->save.autosave, s->tick, true, true);
    boat_presentation_reset(&game->presentation);
    work->actions = 0;
    bool ok = !work->local.running || boat_work_local_restart(&work->local, now);
    wake_work_sync(work, game);
    return ok;
}

bool wake_work_review_pose(WakeWork *work, BoatSession *game, const char *pose)
{
    BoatWorkSession *s = &work->session;
    if (!work->open || !pose)
        return false;
    bool ok = false;
    if (!strcmp(pose, "swing")) {
        boat_work_hook_action(s);
        if (s->sandbox.cargo.state != BOAT_CARGO_HANGING)
            return false;
        for (int i = 0; i < 360; ++i) {
            boat_work_session_tick(s, .7f, .5f, 1, (BoatInput) { 0 });
            if (game->presentation_enabled)
                boat_presentation_step(
                    &game->presentation, &s->boat, &s->world, boat_tick_seconds(s->tick));
        }
        ok = true;
    } else if (!strcmp(pose, "deck")) {
        ok = boat_work_script_load_and_lash(s, 2);
    } else if (!strcmp(pose, "approach")) {
        unsigned landing = boat_work_find_site(s, "landing");
        ok = landing < s->site_count && boat_work_script_load_and_lash(s, 2);
        if (ok) {
            // The real departure action releases the home berth; the shared approach helper then
            // poses and validates the landing berth through its normal moor query.
            boat_work_moor_action(s);
            ok = !s->sandbox.moored && !s->sandbox.mooring
                && boat_work_script_moor_at(s, landing, true);
        }
    }
    if (!ok)
        return false;
    wake_work_sync(work, game);
    return true;
}

static void present_tick(void *context, const BoatWorkSession *view)
{
    BoatSession *game = context;
    if (game->presentation_enabled)
        boat_presentation_step(
            &game->presentation, &view->boat, &view->world, boat_tick_seconds(view->tick));
}

bool wake_work_frame(
    WakeWork *work, BoatSession *game, double *accumulator, const BoatWorkFrame *frame, double now)
{
    unsigned grant = boat_time_grant(accumulator, BOAT_WORK_LOCAL_MAX_GRANT);
    BoatWorkObserver observer = { game, present_tick };
    bool ok = boat_work_local_frame(&work->local, now, grant, frame, &observer);
    wake_work_sync(work, game);
    return ok;
}

bool wake_work_script(WakeWork *work, BoatSession *game, uint64_t ticks)
{
    BoatWorkSession *s = &work->session;
    bool ok = true;
    // A resumed session continues idle: the delivery script belongs to a fresh start.
    bool fresh = s->tick == 0;
    for (uint64_t i = 0; i < ticks; ++i) {
        if (fresh && i == WAKE_WORK_DELIVERY_TICK)
            ok = boat_work_script_landing_delivery(s);
        else
            boat_work_session_tick(s, 0, 0, 0, (BoatInput) { 0 });
    }
    wake_work_sync(work, game);
    return ok;
}

void wake_work_sync(WakeWork *work, BoatSession *game)
{
    const BoatWorkSession *s = wake_work_view(work);
    game->boat = s->boat;
    game->tick = s->tick;
    game->world = s->world;
}

const BoatViewProps *wake_work_props(WakeWork *work)
{
    const BoatWorkSession *s = wake_work_view(work);
    BoatCargoView view = { .sandbox = &s->sandbox,
        .boat = &s->boat,
        .world = &s->world,
        .tick = s->tick,
        .crane_view = s->crane_view };
    size_t receivers = boat_cargo_view_scene(&view, &work->solids);
    Mesh3D receiving = work->solids.mesh;
    receiving.triangle_count = receivers;
    boat_model_begin(&work->decal);
    bool decal = boat_cargo_decal(&work->decal, &receiving, s->sandbox.cargo.position, NULL, 0,
        (Vector2) { 0 }, (Color) { 0 });
    work->props = (BoatViewProps) { .solids = &work->solids.mesh,
        .decal = decal ? &work->decal.mesh : NULL,
        .hide_player = true,
        .lighting_cycle = work->lighting_cycle,
        .lighting_hours = s->lighting_hours,
        .lighting_cycle_seconds = s->lighting_cycle_seconds };
    return &work->props;
}

int wake_work_save_open(WakeWork *work, BoatSession *game, const char *explicit_path,
    const char *data_dir, bool default_dir, bool fresh)
{
    BoatWorkSession *s = &work->session;
    work->save = (BoatWorkSave) { 0 };
    if (!s->revision || !(explicit_path || data_dir || default_dir))
        return 0;
    boat_work_save_path(&work->save, explicit_path, data_dir, s->revision->free.manifest.id);
    int loaded = work->save.enabled && !fresh ? boat_work_save_load(s, &work->save) : 0;
    work->watch = (BoatWorkFrameWatch) { .last = boat_work_watch(s), .was_focused = true };
    wake_work_sync(work, game);
    return loaded;
}

void wake_work_autosave(WakeWork *work, bool on)
{
    work->save.autosaving = on && work->save.enabled;
}

void wake_work_save_frame(WakeWork *work, bool paused, bool focused)
{
    if (work->save.autosaving)
        boat_work_save_frame(
            authority(work), &work->save, &work->watch, paused, focused, (int64_t)time(NULL));
}

bool wake_work_save_now(WakeWork *work)
{
    BoatWorkSession *s = authority(work);
    bool ok = work->save.enabled && boat_work_save_write(s, &work->save, (int64_t)time(NULL));
    printf("%s %s at tick %llu\n", ok ? "Wrote save" : "Could not write save", work->save.path,
        (unsigned long long)s->tick);
    return ok;
}

bool wake_work_save_leave(WakeWork *work)
{
    return !work->save.autosaving
        || boat_work_save_leave(authority(work), &work->save, (int64_t)time(NULL));
}

BoatWorkFrame wake_work_frame_input(WakeWork *work, bool active, bool demo, float steering_target,
    float steering_rate, float thrust_target)
{
    BoatWorkFrame frame = { .actions = work->actions,
        .active = active,
        .demo = demo,
        .steering_target = steering_target,
        .steering_rate = steering_rate,
        .thrust_target = thrust_target };
    work->actions = 0;
    if (!active)
        return frame;
    static const struct {
        int key;
        unsigned action;
    } presses[] = { { KEY_F, BOAT_WORK_HOOK }, { KEY_L, BOAT_WORK_LASH }, { KEY_G, BOAT_WORK_MOOR },
        { KEY_TAB, BOAT_WORK_VIEW }, { KEY_J, BOAT_WORK_NEXT_TARGET } };
    for (size_t i = 0; i < sizeof(presses) / sizeof(presses[0]); ++i)
        if (IsKeyPressed(presses[i].key))
            frame.actions |= presses[i].action;
    if (IsKeyDown(KEY_SPACE))
        frame.actions |= BOAT_WORK_BRAKE;
    for (unsigned i = 1; i <= 3; ++i)
        if (IsKeyPressed(KEY_ONE + (int)i - 1))
            frame.aim = i; // The highest pressed, as the keys were read in order.
    frame.slew = (float)(IsKeyDown(KEY_D) - IsKeyDown(KEY_A));
    frame.reach = (float)(IsKeyDown(KEY_W) - IsKeyDown(KEY_S));
    frame.hoist = (float)(IsKeyDown(KEY_Q) - IsKeyDown(KEY_E));
    return frame;
}

void wake_work_return_crate(WakeWork *work) { work->actions |= BOAT_WORK_RETURN_CRATE; }

static void label(const char *text, int x, int y, Color color)
{
    int width = boat_ui_width(text, 10);
    DrawRectangle(x - 3, y - 2, width + 6, 13, (Color) { 20, 31, 42, 220 });
    boat_ui_text(text, x, y, 10, color);
}

// A marker over the target berth, pinned to the view's edge when it is off screen, so a distant
// site can be found from the helm.
static void target_marker(const WakeWork *work, const WakeWorkProjection *p, int width, int height)
{
    const BoatWorkSession *s = wake_work_view(work);
    Vector3 berth = boat_cargo_site_point(&s->sandbox.cranes[s->target], (Vector3) { 0, 4, 0 });
    Vector2 at = { 0 };
    bool visible = boat_view_project(
        p->view, p->presentation, p->world, p->camera, berth, width, height, &at);
    bool above = !visible || at.y < 40;
    at.x = visible ? Clamp(at.x, 12, (float)width - 12) : (float)width / 2;
    at.y = visible ? Clamp(at.y, 40, (float)height - 70) : 40;
    Color yellow = { 241, 183, 57, 255 };
    if (above)
        DrawTriangle((Vector2) { at.x, at.y - 10 }, (Vector2) { at.x - 6, at.y },
            (Vector2) { at.x + 6, at.y }, yellow);
    else
        DrawTriangle((Vector2) { at.x - 5, at.y - 8 }, (Vector2) { at.x, at.y },
            (Vector2) { at.x + 5, at.y - 8 }, yellow);
    Vector2 to = Vector2Subtract(boat_cargo_berth(&s->sandbox.cranes[s->target]), s->boat.position);
    const char *text
        = TextFormat("%s %.0f M", TextToUpper(s->sites[s->target].id), Vector2Length(to));
    int w = boat_ui_width(text, 10);
    boat_ui_text(text, (int)Clamp(at.x - w / 2.0f, 4, (float)(width - 4 - w)),
        above ? (int)at.y + 3 : (int)at.y - 20, 10, yellow);
}

void wake_work_hud(WakeWork *work, const WakeWorkProjection *projection, int width, int height)
{
    const BoatWorkSession *s = wake_work_view(work);
    const BoatCargo *c = &s->sandbox.cargo;
    Color yellow = { 241, 183, 57, 255 }, green = { 99, 216, 156, 255 },
          text = { 218, 223, 202, 255 }, red = { 232, 106, 84, 255 };
    int y = 18;
    if (s->revision) {
        for (unsigned i = 0; i < s->objective.hud_count; ++i, y += 14)
            label(TextFormat(
                      "%s: %lld", s->objective.hud[i].label, (long long)s->objective.hud[i].value),
                4, y, yellow);
        const char *problem = s->objective.quarantined ? "JOB SUSPENDED" : s->waiting;
        if (problem) {
            label(problem, 4, y, red);
            y += 14;
        }
    }
    static const char *const states[]
        = { "ON PAD", "ON HOOK", "LOOSE ON DECK", "LASHED", "FREE / FLOATING" };
    BoatCargoMoorStatus berth = boat_cargo_moor_status(&s->sandbox, &s->boat);
    label(TextFormat("%s / %s / %s", s->crane_view ? "CRANE" : "HELM",
              s->sandbox.moored        ? "MOORED"
                  : s->sandbox.mooring ? "MOORING"
                                       : "UNMOORED",
              states[c->state]),
        4, y, text);
    y += 14;
    if (!s->crane_view && !s->sandbox.moored && !s->sandbox.mooring
        && (berth.site != s->target || berth.distance > 12) && projection)
        target_marker(work, projection, width, height);
    const char *prompt = s->notice;
    if (prompt && !prompt[0])
        prompt = NULL;
    if (!prompt && !s->sandbox.moored && !s->sandbox.mooring && !s->crane_view
        && berth.reason == BOAT_CARGO_MOOR_READY)
        prompt = "G MOOR";
    if (prompt && prompt[0])
        label(prompt,
            (width - boat_ui_width(prompt, 10)) / 2 > 4 ? (width - boat_ui_width(prompt, 10)) / 2
                                                        : 4,
            height - 64, text);
    if (work->save.failed || work->save.mark > 0) {
        const char *mark = work->save.failed ? "SAVE FAILED" : "SAVED";
        label(mark, width - boat_ui_width(mark, 10) - 6, height - 14,
            work->save.failed ? red : green);
        if (work->save.mark > 0)
            --work->save.mark;
    }
}

// SV1h: the title's view of the bundled package's save slot.

// The display time zone: this machine's offset from UTC now (display only).
static int local_utc_offset(void)
{
    time_t now = time(NULL);
    struct tm utc = *gmtime(&now);
    utc.tm_isdst = -1;
    return (int)difftime(now, mktime(&utc));
}

// Decode the location's bundled work package. False when there is none.
static bool bundled_identity(BoatSandboxIdentity *identity)
{
    static BoatPluginRevision scanned;
    BoatPluginDiagnostic d = { 0 };
    const BoatPluginCatalogEntry *entry = boat_plugin_catalog_work_default("outer");
    if (!entry || !boat_plugin_catalog_decode(entry, &scanned, &d) || !scanned.free.has_scene)
        return false;
    boat_work_revision_identity(&scanned, "wake_velocity", identity);
    return true;
}

void wake_work_title_scan(const char *data_dir, BoatTitleSave *save)
{
    static BoatWorkMenuSave found[BOAT_WORK_MENU_SAVES];
    char directory[1000] = "";
    BoatSandboxIdentity installed;
    *save = (BoatTitleSave) { 0 };
    if (!bundled_identity(&installed))
        return;
    if (data_dir)
        snprintf(directory, sizeof(directory), "%s", data_dir);
    else if (!boat_file_data_directory(directory, sizeof(directory)))
        return;
    unsigned count
        = boat_work_menu_discover(directory, &installed, 1, local_utc_offset(), found, 16);
    const BoatWorkMenuSave *slot = boat_work_menu_find(found, count, installed.package);
    if (!slot)
        return;
    save->present = true;
    save->available = slot->available;
    snprintf(save->summary, sizeof(save->summary), "%s", slot->summary);
    snprintf(save->reason, sizeof(save->reason), "%s", slot->reason);
    snprintf(save->path, sizeof(save->path), "%s", slot->path);
}

bool wake_work_title_delete(const BoatTitleSave *save)
{
    return save->present && boat_file_remove_save(save->path);
}

bool wake_work_title_set_aside(const BoatTitleSave *save)
{
    bool ok = save->present && boat_work_menu_set_aside(save->path);
    printf("%s %s as .rejected\n", ok ? "Set aside" : "Could not set aside", save->path);
    return ok;
}
