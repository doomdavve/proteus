// Private P4 worker: immutable framed bytes in, bounded JSON evidence out. No package paths.
#include "boat_plugin_host.h"
#include "boat_plugin_stage.h"
#include "boat_world.h"
#include "wake_velocity_plugin.h"
#include "waterfront_traffic.h"
#include <errno.h>
#include <math.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static void json_text(const char *text)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < 32 || *p >= 127)
            printf("\\u%04x", *p);
        else {
            if (*p == '"' || *p == '\\')
                putchar('\\');
            putchar(*p);
        }
    }
    putchar('"');
}

static unsigned length(const unsigned char *p)
{
    return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}

// Fixed, versioned virtual fixture. No display, files, wall-clock waits or playback effects.
static bool preview(WakePlugin *plugin, const BoatWorldResources *resources, unsigned ticks,
    BoatPluginDiagnostic *d)
{
    BoatPluginHost *host = &plugin->host;
    if (host->kind != BOAT_PLUGIN_KIND_VOYAGE && host->kind != BOAT_PLUGIN_KIND_RACE) {
        d->status = BOAT_PLUGIN_RUNTIME;
        snprintf(
            d->message, sizeof(d->message), "preview is not available for free or course packages");
        return false;
    }
    if (host->kind == BOAT_PLUGIN_KIND_RACE) {
        BoatFleet *fleet = calloc(1, sizeof(*fleet));
        BoatRace race;
        bool ok = fleet
            && boat_fleet_init(fleet, resources, (BoatWater) { 0 }, BOAT_OBJECT_LIFE_TICKS,
                BOAT_OBJECT_DEFAULT_HEIGHT)
            && boat_race_init(&race, &boat_plugin_host_active(host)->race.rules,
                &boat_plugin_host_active(host)->race.course);
        for (unsigned i = 0; ok && i < BOAT_FLEET_SLOTS; ++i)
            ok = boat_race_join(&race, fleet, i);
        for (unsigned t = 0; ok && t < ticks; ++t) {
            BoatInput inputs[BOAT_FLEET_SLOTS] = { 0 };
            for (unsigned i = 0; i < BOAT_FLEET_SLOTS; ++i)
                inputs[i].throttle_rate = t < 120 ? 1 : 0;
            BoatRaceOutput output;
            ok = boat_fleet_step(fleet, inputs);
            boat_race_observe(&race, fleet, &output);
        }
        if (ok)
            printf("\"preview\":{\"fixture\":\"race-four-grid-v1\",\"ticks\":%u,"
                   "\"phase\":%u,\"round\":%u,\"fleet_digest\":\"%016llx\"}",
                ticks, (unsigned)race.phase, race.round,
                (unsigned long long)boat_fleet_hash(&fleet->state));
        free(fleet);
        if (!ok)
            d->status = BOAT_PLUGIN_RUNTIME;
        return ok;
    }
    BoatSession session;
    boat_session_init(&session, resources, (BoatWater) { 0 });
    BoatWakeObjects objects, traffic;
    WakePluginPreview *origin = calloc(1, sizeof(*origin));
    WakePluginPreview *checkpoint = calloc(1, sizeof(*checkpoint));
    WakePluginReviewTick *inputs = calloc(ticks + 1, sizeof(*inputs));
    bool ok = origin && checkpoint && inputs
        && boat_wake_objects_init(&objects, BOAT_OBJECT_LIFE_TICKS, BOAT_OBJECT_DEFAULT_HEIGHT)
        && waterfront_traffic_wakes_init(&traffic, 0);
    if (!ok) {
        d->status = BOAT_PLUGIN_MEMORY;
        goto done;
    }
    session.world.water.objects = &objects;
    session.world.water.traffic_objects = &traffic;
    const BoatPluginRevision *active = boat_plugin_host_active(host);
    boat_session_restart_voyage(&session, &active->voyage.route);
    session.world.scene = active->voyage.has_scene ? &active->voyage.scene : NULL;
    wake_plugin_reset_objective(plugin, &session);
    for (unsigned t = 0; t < ticks; ++t)
        inputs[t].input = (BoatInput) { .throttle_rate = t < 120 ? 1 : 0,
            .steering_rate = (t / 240) % 2 ? -.1f : .1f };
    unsigned split = ticks / 2;
    ok = wake_plugin_preview_save(origin, plugin, &session)
        && wake_plugin_preview_seek(origin, plugin, &session, &objects, &traffic, inputs, split)
        && wake_plugin_preview_save(checkpoint, plugin, &session)
        && wake_plugin_preview_seek(
            checkpoint, plugin, &session, &objects, &traffic, inputs + split, ticks - split);
    if (plugin->objective.quarantined) {
        *d = plugin->objective_diagnostic;
        ok = false;
    }
    if (ok) {
        printf("\"preview\":{\"fixture\":\"voyage-calm-steer-v1\",\"ticks\":%u,"
               "\"checkpoint_tick\":%u,\"position\":[%.9g,%.9g],\"player_wakes\":%u,"
               "\"traffic_wakes\":%u,\"objective_digest\":\"%016llx\","
               "\"pending_digest\":\"%016llx\",\"pending\":%s,\"generation\":%u,\"hud\":[",
            ticks, split, (double)session.boat.position.x, (double)session.boat.position.y,
            objects.live, traffic.live,
            (unsigned long long)boat_plugin_objective_digest(&plugin->objective),
            (unsigned long long)(plugin->objective_pending
                    ? boat_plugin_objective_digest(&plugin->staged_objective)
                    : 0),
            plugin->objective_pending ? "true" : "false", plugin->generation);
        for (unsigned i = 0; i < plugin->objective.hud_count; ++i) {
            if (i)
                putchar(',');
            printf("{\"id\":");
            json_text(plugin->objective.hud[i].id);
            printf(",\"label\":");
            json_text(plugin->objective.hud[i].label);
            // int64 JSON string avoids rounding in browser/tool consumers.
            printf(",\"value\":\"%lld\"}", (long long)plugin->objective.hud[i].value);
        }
        printf("]}");
    } else if (d->status == BOAT_PLUGIN_OK)
        d->status = BOAT_PLUGIN_RUNTIME;
done:
    boat_session_destroy(&session);
    free(inputs);
    free(checkpoint);
    free(origin);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--plugin-stage-worker"))
        return boat_plugin_stage_worker();
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    unsigned ticks = 0;
    bool run_preview = argc == 3 && !strcmp(argv[1], "--preview");
    if (run_preview) {
        char *end;
        errno = 0;
        unsigned long parsed = strtoul(argv[2], &end, 10);
        if (errno || !argv[2][0] || *end || parsed > 72000)
            return 2;
        ticks = (unsigned)parsed;
    } else if (argc != 1)
        return 2;
    unsigned char header[8];
    BoatPluginFiles *files = calloc(1, sizeof(*files));
    WakePlugin *plugin = calloc(1, sizeof(*plugin));
    BoatPluginHost *host = plugin ? &plugin->host : NULL;
    BoatPluginDiagnostic d = { .status = BOAT_PLUGIN_IO };
    BoatWorldResources resources = { 0 };
    uint64_t digest = 0;
    if (!files || !host)
        goto done;
    if (fread(header, 1, sizeof(header), stdin) != sizeof(header))
        goto done;
    files->manifest_size = length(header);
    files->source_size = length(header + 4);
    if (files->manifest_size > BOAT_PLUGIN_MANIFEST_MAX
        || files->source_size > BOAT_PLUGIN_SOURCE_MAX
        || fread(files->manifest, 1, files->manifest_size, stdin) != files->manifest_size
        || fread(files->source, 1, files->source_size, stdin) != files->source_size
        || fgetc(stdin) != EOF || ferror(stdin))
        goto done;
    BoatPluginBundle bundle = boat_plugin_bundle(files);
    BoatPluginManifest manifest;
    if (boat_plugin_parse_manifest(bundle.manifest, bundle.manifest_size, NULL, &manifest, &d)
        != BOAT_PLUGIN_OK)
        goto done;
    // A match package without a scene serves every location; validate it against the basin.
    const char *map_name = !strcmp(manifest.location, "any") ? "basin" : manifest.location;
    if (!boat_world_resources_init(&resources, boat_location_find(map_name))) {
        d.status = BOAT_PLUGIN_MEMORY;
        goto done;
    }
    boat_plugin_host_start(host, resources.definition, &resources.map, &bundle);
    d = host->diagnostic;
    if (d.status == BOAT_PLUGIN_OK) {
        const BoatPluginRevision *revision = boat_plugin_host_active(host);
        switch (host->kind) {
        case BOAT_PLUGIN_KIND_RACE:
            digest = revision->race.digest;
            break;
        case BOAT_PLUGIN_KIND_COURSE:
            digest = revision->course.digest;
            break;
        case BOAT_PLUGIN_KIND_FREE:
            digest = revision->free.digest;
            break;
        case BOAT_PLUGIN_KIND_MATCH:
            digest = revision->match.digest;
            break;
        default:
            digest = revision->voyage.digest;
            break;
        }
    }
done:
    printf("{");
    if (run_preview && d.status == BOAT_PLUGIN_OK) {
        // Open a nested evidence member first; a failed scenario emits no success artifact.
        printf("\"scenario\":{");
        preview(plugin, &resources, ticks, &d);
        printf("},");
    }
    printf("\"status\":");
    json_text(boat_plugin_status_name(d.status));
    printf(",\"file\":");
    json_text(d.file);
    printf(",\"line\":%d,\"field\":", d.line);
    json_text(d.field);
    printf(",\"message\":");
    json_text(d.message);
    unsigned scene_present = 0, scene_version = 0, scene_content = 0, berth_count = 0,
             module_count = 0, vessel_count = 0, flock_count = 0;
    bool traffic_present = false, gulls_present = false;
    uint64_t scene_digest = 0;
    if (d.status == BOAT_PLUGIN_OK && host->kind == BOAT_PLUGIN_KIND_VOYAGE) {
        const BoatPluginRevision *active = boat_plugin_host_active(host);
        scene_present = active->voyage.has_scene;
        scene_version = active->voyage.scene.version;
        scene_digest = active->voyage.scene_digest;
        scene_content = boat_scene_has_content(&active->voyage.scene);
        berth_count = active->voyage.scene.berth_count;
        module_count = active->voyage.scene.module_count;
        traffic_present = active->voyage.scene.traffic_present;
        vessel_count = traffic_present ? active->voyage.scene.traffic.actor_count : 0;
        gulls_present = active->voyage.scene.gulls_present;
        flock_count = gulls_present ? active->voyage.scene.gulls.flock_count : 0;
    }
    printf(",\"gameplay_digest\":\"%016llx\",\"scene\":{\"present\":%s,"
           "\"version\":%u,\"digest\":\"%016llx\",\"content\":%s,"
           "\"subsystems\":[%s%s%s%s%s%s%s],\"berths\":%u,\"modules\":%u,\"vessels\":%u,"
           "\"flocks\":%u},"
           "\"cost\":{\"instructions\":%llu,"
           "\"entry_instructions\":%llu,\"callback_instructions\":%llu,"
           "\"peak_bytes\":%zu,\"leaked_bytes\":%zu,\"fields\":%u}}\n",
        (unsigned long long)digest, scene_present ? "true" : "false", scene_version,
        (unsigned long long)scene_digest, scene_content ? "true" : "false",
        berth_count ? "\"marina.berths\"" : "",
        berth_count && (module_count || traffic_present || gulls_present) ? "," : "",
        module_count ? "\"marina.modules\"" : "",
        module_count && (traffic_present || gulls_present) ? "," : "",
        traffic_present ? "\"traffic.vessels\"" : "", traffic_present && gulls_present ? "," : "",
        gulls_present ? "\"gulls.flocks\"" : "", berth_count, module_count, vessel_count,
        flock_count, (unsigned long long)d.cost.instructions,
        (unsigned long long)d.cost.entry_instructions,
        (unsigned long long)d.cost.callback_instructions, d.cost.peak_bytes, d.cost.leaked_bytes,
        d.cost.fields);
    boat_world_resources_destroy(&resources);
    free(plugin);
    free(files);
    return d.status == BOAT_PLUGIN_OK ? 0 : 1;
}
