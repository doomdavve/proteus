#include "wake_velocity_work.h"

#include "boat_cargo_view.h"
#include "boat_plugin_catalog.h"
#include "boat_presentation.h"
#include "boat_ui.h"

#include <stdio.h>
#include <string.h>

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
    *work = (WakeWork) { .open = true };
    BoatWorkSession *s = &work->session;
    s->resources = resources;
    s->world.scene = &revision.free.scene;
    s->revision = revision.free.manifest.objective ? &revision : NULL;
    s->site_count = revision.free.scene.work_count;
    memcpy(s->sites, revision.free.scene.work, sizeof(s->sites));
    boat_work_session_reset(s);
    wake_work_sync(work, game);
    return true;
}

void wake_work_reset(WakeWork *work, BoatSession *game)
{
    boat_work_session_reset(&work->session);
    boat_presentation_reset(&game->presentation);
    wake_work_sync(work, game);
}

void wake_work_tick(
    WakeWork *work, BoatSession *game, float slew, float reach, float hoist, BoatInput input)
{
    BoatWorkSession *s = &work->session;
    boat_work_session_tick(s, slew, reach, hoist, input);
    if (game->presentation_enabled)
        boat_presentation_step(
            &game->presentation, &s->boat, &s->world, boat_tick_seconds(s->tick));
}

bool wake_work_script(WakeWork *work, BoatSession *game, uint64_t ticks)
{
    BoatWorkSession *s = &work->session;
    bool ok = true;
    for (uint64_t i = 0; i < ticks; ++i) {
        if (i == WAKE_WORK_DELIVERY_TICK)
            ok = boat_work_script_landing_delivery(s);
        else
            boat_work_session_tick(s, 0, 0, 0, (BoatInput) { 0 });
    }
    wake_work_sync(work, game);
    return ok;
}

void wake_work_sync(WakeWork *work, BoatSession *game)
{
    const BoatWorkSession *s = &work->session;
    game->boat = s->boat;
    game->tick = s->tick;
    game->world = s->world;
}

const BoatViewProps *wake_work_props(WakeWork *work)
{
    const BoatWorkSession *s = &work->session;
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
    work->props = (BoatViewProps) {
        .solids = &work->solids.mesh, .decal = decal ? &work->decal.mesh : NULL, .hide_player = true
    };
    return &work->props;
}

static void label(const char *text, int x, int y, Color color)
{
    int width = boat_ui_width(text, 10);
    DrawRectangle(x - 3, y - 2, width + 6, 13, (Color) { 20, 31, 42, 220 });
    boat_ui_text(text, x, y, 10, color);
}

void wake_work_hud(const WakeWork *work, int width, int height)
{
    const BoatWorkSession *s = &work->session;
    int y = 18;
    if (s->revision) {
        for (unsigned i = 0; i < s->objective.hud_count; ++i, y += 14)
            label(TextFormat(
                      "%s: %lld", s->objective.hud[i].label, (long long)s->objective.hud[i].value),
                4, y, (Color) { 241, 183, 57, 255 });
        const char *problem = s->objective.quarantined ? "JOB SUSPENDED" : s->waiting;
        if (problem)
            label(problem, 4, y, (Color) { 232, 106, 84, 255 });
    }
    if (s->notice)
        label(s->notice, (width - boat_ui_width(s->notice, 10)) / 2, height - 98,
            (Color) { 218, 223, 202, 255 });
}
