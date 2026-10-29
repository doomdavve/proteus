#include "boat_race_plugin.h"

#include <stdio.h>
#include <string.h>

static void report(RacePlugin *plugin)
{
    const BoatPluginDiagnostic *d = &plugin->host.diagnostic;
    unsigned active = boat_plugin_host_active(&plugin->host)->number;
    if (d->status != BOAT_PLUGIN_OK) {
        char where[40];
        if (d->line > 0)
            snprintf(where, sizeof(where), "%s:%d", d->file, d->line);
        else
            snprintf(where, sizeof(where), "%s", d->file);
        snprintf(plugin->notice, sizeof(plugin->notice), "R%u kept: %s %.63s: %.120s", active,
            where, d->field, d->message);
    } else if (plugin->host.change == BOAT_PLUGIN_INCOMPATIBLE)
        snprintf(plugin->notice, sizeof(plugin->notice),
            "R%u kept: incompatible plugin identity; relaunch", active);
    else if (boat_plugin_host_held(&plugin->host))
        snprintf(plugin->notice, sizeof(plugin->notice),
            "R%u active / R%u staged: F7 restart run; F6 discard", active,
            boat_plugin_host_held(&plugin->host)->number);
    else
        snprintf(plugin->notice, sizeof(plugin->notice), "R%u active: %.63s / F5 reload, F6 undo",
            active, boat_plugin_host_active(&plugin->host)->race.manifest.name);
    fprintf(stderr, "Race plugin: %s\n", plugin->notice);
}

bool race_plugin_open(
    RacePlugin *plugin, const char *directory, const BoatWorldResources *resources, double now)
{
    // Clear in place: a compound literal duplicates the revision history on the stack.
    memset(plugin, 0, sizeof(*plugin));
    plugin->directory = directory;
    BoatPluginDiagnostic d;
    BoatPluginStatus status = boat_plugin_read(plugin->directory, NULL, &plugin->files, &d);
    BoatPluginBundle bundle = boat_plugin_bundle(&plugin->files);
    if (status != BOAT_PLUGIN_OK
        || !boat_plugin_host_start(
            &plugin->host, resources->definition, &resources->map, &bundle)) {
        if (status != BOAT_PLUGIN_OK)
            plugin->host.diagnostic = d;
        d = plugin->host.diagnostic;
        fprintf(stderr, "Race plugin: %s:%d %s: %s\n", d.file, d.line, d.field, d.message);
        return false;
    }
    if (!plugin->host.racing) {
        fprintf(stderr, "Race trial requires kind race.\n");
        return false;
    }
    boat_plugin_watch_start(&plugin->watch, boat_plugin_read_key(status, &plugin->files, &d), now);
    report(plugin);
    return true;
}

// At a frame boundary, stage immutable saves. Gameplay commits only on an explicit new run.
// Returns true when preview state, clock and wakes must be reset together.
bool race_plugin_frame(RacePlugin *plugin, bool reload, bool undo, bool apply, double now)
{
    plugin->staged = false;
    if (apply && boat_plugin_host_held(&plugin->host)) {
        bool restart = plugin->host.change == BOAT_PLUGIN_RESTART_SESSION;
        if (!boat_plugin_host_commit(&plugin->host))
            return false;
        report(plugin);
        return restart;
    }
    if (undo) {
        if (boat_plugin_host_held(&plugin->host))
            boat_plugin_host_discard(&plugin->host);
        else
            boat_plugin_host_undo(&plugin->host);
        report(plugin);
    } else if (reload || boat_plugin_watch_due(&plugin->watch, now)) {
        BoatPluginDiagnostic d;
        BoatPluginStatus status = boat_plugin_read(plugin->directory, NULL, &plugin->files, &d);
        uint64_t key = boat_plugin_read_key(status, &plugin->files, &d);
        if (reload || boat_plugin_watch_settled(&plugin->watch, key, now)) {
            plugin->staged = true;
            boat_plugin_watch_staged(&plugin->watch, key);
            if (status != BOAT_PLUGIN_OK) {
                boat_plugin_host_discard(&plugin->host);
                plugin->host.diagnostic = d;
            } else {
                BoatPluginBundle bundle = boat_plugin_bundle(&plugin->files);
                boat_plugin_host_stage(&plugin->host, &bundle);
            }
            report(plugin);
        }
    }
    if (boat_plugin_host_held(&plugin->host) && plugin->host.change == BOAT_PLUGIN_RENAMED) {
        bool restart = plugin->host.change == BOAT_PLUGIN_RESTART_SESSION;
        if (!boat_plugin_host_commit(&plugin->host))
            return false;
        report(plugin);
        return restart;
    }
    return false;
}
