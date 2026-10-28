#include "wake_velocity_plugin.h"
#include "boat_ui.h"

#include <stdarg.h>
#include <stdio.h>

static void report(WakePlugin *plugin, bool alert, const char *detail, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(plugin->status, sizeof(plugin->status), format, arguments);
    va_end(arguments);
    snprintf(plugin->detail, sizeof(plugin->detail), "%s", detail);
    plugin->alert = alert;
    fprintf(stderr, "Plugin %s: %s%s%s\n", plugin->directory, plugin->status,
        detail[0] ? " / " : "", detail);
}

static void report_failure(WakePlugin *plugin)
{
    const BoatPluginDiagnostic *d = &plugin->host.diagnostic;
    char where[96];
    snprintf(where, sizeof(where), "%s%s%.0d%s%s", d->file, d->line ? ":" : "", d->line,
        d->field[0] ? " " : "", d->field);
    report(plugin, true, d->message, "%s ERROR %s; KEEPING R%u", boat_plugin_status_name(d->status),
        where, boat_plugin_host_active(&plugin->host)->number);
}

static BoatPluginStatus read_package(WakePlugin *plugin, uint64_t *key)
{
    BoatPluginDiagnostic d;
    BoatPluginStatus status = boat_plugin_read(plugin->directory, NULL, &plugin->files, &d);
    *key = boat_plugin_read_key(status, &plugin->files, &d);
    if (status != BOAT_PLUGIN_OK)
        plugin->host.diagnostic = d;
    return status;
}

const BoatWorldDefinition *wake_plugin_location(const char *directory)
{
    static BoatPluginFiles files;
    BoatPluginDiagnostic d;
    BoatPluginManifest manifest;
    if (boat_plugin_read(directory, NULL, &files, &d) != BOAT_PLUGIN_OK
        || boat_plugin_parse_manifest(files.manifest, files.manifest_size, NULL, &manifest, &d)
            != BOAT_PLUGIN_OK) {
        fprintf(stderr, "Plugin %s: %s error in %s %s: %s\n", directory,
            boat_plugin_status_name(d.status), d.file, d.field, d.message);
        return NULL;
    }
    return boat_location_find(manifest.location);
}

bool wake_plugin_open(WakePlugin *plugin, const char *directory, BoatSession *session, double now)
{
    *plugin = (WakePlugin) { .directory = directory };
    uint64_t key;
    bool read = read_package(plugin, &key) == BOAT_PLUGIN_OK;
    BoatPluginBundle bundle = boat_plugin_bundle(&plugin->files);
    if (!read
        || !boat_plugin_host_start(
            &plugin->host, session->world.definition, &session->world.resources->map, &bundle)) {
        const BoatPluginDiagnostic *d = &plugin->host.diagnostic;
        fprintf(stderr, "Plugin %s: %s error in %s%s%.0d %s: %s\n", directory,
            boat_plugin_status_name(d->status), d->file, d->line ? ":" : "", d->line, d->field,
            d->message);
        return false;
    }
#if !defined(__EMSCRIPTEN__)
    plugin->watching = true;
    boat_plugin_watch_start(&plugin->watch, key, now);
#else
    (void)now;
#endif
    // At tick zero nothing has been observed, so this is the voyage's start.
    boat_session_restart_voyage(session, &boat_plugin_host_active(&plugin->host)->voyage.route);
    const BoatPluginVoyage *voyage = &boat_plugin_host_active(&plugin->host)->voyage;
    report(plugin, false, "F5 RELOAD / F6 UNDO / SAVES RELOAD", "%s R1 ACTIVE %016llx",
        voyage->manifest.id, (unsigned long long)voyage->digest);
    return true;
}

// Install the held candidate under its policy. True when the session must restart.
static bool commit(WakePlugin *plugin, BoatSession *session)
{
    BoatPluginChange change = plugin->host.change;
    bool undo = plugin->host.undo;
    const BoatPluginRevision *revision = boat_plugin_host_commit(&plugin->host);
    if (!revision)
        return false;
    const BoatVoyageRoute *route = &revision->voyage.route;
    const char *how = undo ? "UNDONE TO" : "ACTIVE";
    switch (change) {
    case BOAT_PLUGIN_UNCHANGED:
    case BOAT_PLUGIN_RENAMED:
        boat_session_rebind_voyage(session, route);
        report(plugin, false, "PROGRESS KEPT", "R%u %s, %s", revision->number, how,
            boat_plugin_change_name(change));
        return false;
    case BOAT_PLUGIN_RESTART_VOYAGE:
        boat_session_restart_voyage(session, route);
        report(plugin, false, "VOYAGE RESTARTED AT THE START SHELTER", "R%u %s", revision->number,
            how);
        return false;
    default:
        session->voyage_route = route; // The caller's restart begins it.
        report(
            plugin, false, "SESSION RESTARTED WITH THE NEW WATER", "R%u %s", revision->number, how);
        return true;
    }
}

// Apply a rename now; hold a restart for APPLY.
static bool offer(WakePlugin *plugin, BoatSession *session)
{
    const BoatPluginRevision *held = boat_plugin_host_held(&plugin->host);
    BoatPluginChange change = plugin->host.change;
    if (change == BOAT_PLUGIN_INCOMPATIBLE) {
        report(plugin, true, "PLUGIN ID, KIND OR LOCATION CHANGED: RELAUNCH TO USE IT",
            "NOT APPLIED; KEEPING R%u", boat_plugin_host_active(&plugin->host)->number);
        return false;
    }
    if (!held) {
        report(plugin, false, "", "R%u UNCHANGED", boat_plugin_host_active(&plugin->host)->number);
        return false;
    }
    if (change == BOAT_PLUGIN_UNCHANGED || change == BOAT_PLUGIN_RENAMED)
        return commit(plugin, session);
    bool water = change == BOAT_PLUGIN_RESTART_SESSION;
    report(plugin, true,
        water ? "F7 RESTART SESSION / F6 DISCARD" : "F7 RESTART VOYAGE / F6 DISCARD", "R%u %s %s",
        held->number, plugin->host.undo ? "(UNDO) CHANGES" : "CHANGES",
        water ? "THE EXPOSED WATER" : "THE ROUTE");
    return false;
}

static bool stage(WakePlugin *plugin, BoatSession *session, uint64_t key, bool read_ok)
{
    if (plugin->watching)
        boat_plugin_watch_staged(&plugin->watch, key);
    BoatPluginBundle bundle = boat_plugin_bundle(&plugin->files);
    if (!read_ok) {
        boat_plugin_host_discard(&plugin->host);
        report_failure(plugin);
        return false;
    }
    if (!boat_plugin_host_stage(&plugin->host, &bundle)) {
        report_failure(plugin);
        return false;
    }
    return offer(plugin, session);
}

bool wake_plugin_frame(
    WakePlugin *plugin, BoatSession *session, bool reload, bool undo, bool apply, double now)
{
    if (apply && boat_plugin_host_held(&plugin->host))
        return commit(plugin, session);
    if (undo) {
        if (boat_plugin_host_held(&plugin->host)) {
            boat_plugin_host_discard(&plugin->host);
            report(plugin, false, "", "DISCARDED; KEEPING R%u",
                boat_plugin_host_active(&plugin->host)->number);
            return false;
        }
        if (!boat_plugin_host_undo(&plugin->host)) {
            report(plugin, false, "", "NOTHING TO UNDO");
            return false;
        }
        return offer(plugin, session);
    }
    uint64_t key;
    if (reload) {
        bool ok = read_package(plugin, &key) == BOAT_PLUGIN_OK;
        return stage(plugin, session, key, ok);
    }
    if (plugin->watching && boat_plugin_watch_due(&plugin->watch, now)) {
        bool ok = read_package(plugin, &key) == BOAT_PLUGIN_OK;
        if (boat_plugin_watch_settled(&plugin->watch, key, now))
            return stage(plugin, session, key, ok);
    }
    return false;
}

void wake_plugin_draw(const WakePlugin *plugin, int x, int bottom)
{
    const Color accent = { 90, 240, 255, 255 }, warning = { 255, 161, 0, 255 };
    const char *lines[] = { plugin->status, plugin->detail };
    int rows = plugin->detail[0] ? 2 : 1;
    int width = 0;
    for (int i = 0; i < rows; ++i) {
        int w = boat_ui_width(lines[i], 12);
        width = w > width ? w : width;
    }
    int top = bottom - rows * 16 - 4;
    DrawRectangle(x - 4, top, width + 8, rows * 16 + 4, (Color) { 14, 23, 32, 220 });
    for (int i = 0; i < rows; ++i)
        boat_ui_text(lines[i], x, top + 3 + i * 16, 12,
            i == 0 ? (plugin->alert ? warning : accent) : RAYWHITE);
}
