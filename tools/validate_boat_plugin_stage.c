#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "wake_velocity_plugin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void fault(const char *value)
{
#ifdef _WIN32
    _putenv_s("PROTEUS_STAGE_TEST_FAULT", value);
#else
    setenv("PROTEUS_STAGE_TEST_FAULT", value, 1);
#endif
}

static int worker(void)
{
    const char *mode = getenv("PROTEUS_STAGE_TEST_FAULT");
    if (mode && !strcmp(mode, "invalid")) {
        puts("invalid output");
        return 0;
    }
    if (mode && !strcmp(mode, "oversized")) {
        for (unsigned i = 0; i < 4097; ++i)
            putchar('x');
        return 0;
    }
    if (mode && !strcmp(mode, "stuck")) {
        for (;;) { }
    }
    return boat_plugin_stage_worker();
}

static void rename_manifest(BoatPluginFiles *files)
{
    // Same-length harmless display edit, preserving immutable source and gameplay.
    char *name = strstr(files->manifest, "Shelter");
    if (name)
        name[0] = 's';
}

static int completed(BoatPluginStageJob *job, BoatPluginHost *host)
{
    // No wall-clock sleeps or simulated timeout. Process completion is an external event;
    // CTest supplies the failure deadline. Deadline behavior itself uses a virtual clock below.
    int result;
    while (!(result = boat_plugin_stage_poll(job, host, .25))) { }
    return result;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--plugin-stage-worker"))
        return worker();
    BoatPluginFiles *files = calloc(1, sizeof(*files));
    WakePlugin *plugin = calloc(1, sizeof(*plugin));
    WakePluginPreview *checkpoint = calloc(1, sizeof(*checkpoint));
    CHECK(files && plugin && checkpoint);
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_read("plugins/shelter-tour", NULL, files, &d) == BOAT_PLUGIN_OK);
    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, boat_location_find("outer")));
    BoatSession session;
    boat_session_init(&session, &resources, (BoatWater) { 0 });
    CHECK(wake_plugin_open(plugin, "plugins/shelter-tour", &session, 0));
    plugin->watching = false;
    BoatPluginHost *host = &plugin->host;
    rename_manifest(files);
    BoatPluginStageJob *job = boat_plugin_stage_start(argv[0], host, files, 0);
    CHECK(job);
    plugin->staging = job;
    CHECK(!wake_plugin_preview_save(checkpoint, plugin, &session));
    CHECK(boat_session_step(&session, (BoatInput) { .throttle_rate = 1 }));
    CHECK(completed(job, host) == 1);
    CHECK(host->change == BOAT_PLUGIN_RENAMED && host->held);
    CHECK(boat_plugin_host_active(host)->number == 1 && session.tick == 1);
    wake_plugin_close(plugin, &session);
    unsigned before = host->numbered;
    job = boat_plugin_stage_start(argv[0], host, files, 0);
    CHECK(job && completed(job, host) == 1);
    CHECK(host->numbered == before); // Repeated held candidate keeps its number.
    boat_plugin_stage_close(job);
    CHECK(!wake_plugin_frame(plugin, &session, false, false, true, .5));
    CHECK(host->epoch == 1 && boat_plugin_host_active(host)->number == 2 && session.tick == 1);
    CHECK(session.voyage.route == &boat_plugin_host_active(host)->voyage.route);
    CHECK(wake_plugin_preview_save(checkpoint, plugin, &session));

    // A job pinned to R2 is stale even after R2 -> R3 -> R2 (same number, new epoch).
    job = boat_plugin_stage_start(argv[0], host, files, 0);
    CHECK(job);
    char *name = strstr(files->manifest, "shelter tour");
    CHECK(name);
    name[0] = 'S';
    BoatPluginBundle bundle = boat_plugin_bundle(files);
    CHECK(boat_plugin_host_stage(host, &bundle) && boat_plugin_host_commit(host));
    CHECK(boat_plugin_host_undo(host) && boat_plugin_host_commit(host));
    CHECK(boat_plugin_host_active(host)->number == 2); // Same base number, newer epoch.
    name[0] = 's';
    CHECK(boat_plugin_stage_poll(job, host, .25) == -1);
    CHECK(strstr(host->diagnostic.message, "stale"));
    boat_plugin_stage_close(job);

    // Virtual deadline kills/reaps a genuinely stuck process; close also cancels in flight.
    fault("stuck");
    job = boat_plugin_stage_start(argv[0], host, files, 0);
    CHECK(job && boat_plugin_stage_poll(job, host, 5) == -1);
    CHECK(strstr(host->diagnostic.message, "deadline"));
    boat_plugin_stage_close(job);
    plugin->staging = boat_plugin_stage_start(argv[0], host, files, 0);
    CHECK(plugin->staging);
    unsigned active = boat_plugin_host_active(host)->number;
    CHECK(!wake_plugin_frame(plugin, &session, false, true, false, .25));
    CHECK(!plugin->staging && boat_plugin_host_active(host)->number == active);
    fault("");

    for (unsigned i = 0; i < 2; ++i) {
        fault(i ? "oversized" : "invalid");
        job = boat_plugin_stage_start(argv[0], host, files, 0);
        CHECK(job && completed(job, host) == -1);
        CHECK(boat_plugin_host_active(host)->number == active);
        boat_plugin_stage_close(job);
    }
    fault("");
    // Worker syntax errors retain committed state, source and native route.
    memcpy(files->source, "return !!!", 10);
    files->source_size = 10;
    uint64_t objective = boat_plugin_objective_digest(&plugin->objective);
    job = boat_plugin_stage_start(argv[0], host, files, 0);
    CHECK(job && completed(job, host) == -1 && host->diagnostic.status == BOAT_PLUGIN_SYNTAX);
    CHECK(boat_plugin_host_active(host)->number == active);
    CHECK(boat_plugin_objective_digest(&plugin->objective) == objective);
    boat_plugin_stage_close(job);
    CHECK(!boat_plugin_stage_start("/missing/worker", host, files, 0));
    CHECK(!boat_plugin_stage_start(argv[0], host, files, NAN));
    // Drive the actual live F5/async completion path through a relocated executable/package.
    CHECK(boat_plugin_read("plugins/shelter-tour", NULL, files, &d) == BOAT_PLUGIN_OK);
    FILE *output = fopen(BOAT_PLUGIN_STAGE_DIR "/plugin.json", "wb");
    CHECK(
        output && fwrite(files->manifest, 1, files->manifest_size, output) == files->manifest_size);
    CHECK(!fclose(output));
    output = fopen(BOAT_PLUGIN_STAGE_DIR "/main.lua", "wb");
    CHECK(output && fwrite(files->source, 1, files->source_size, output) == files->source_size);
    CHECK(!fclose(output));
    plugin->directory = BOAT_PLUGIN_STAGE_DIR;
    plugin->stage_executable = argv[0];
    CHECK(!wake_plugin_frame(plugin, &session, true, false, false, 0));
    CHECK(plugin->staging && boat_plugin_host_active(host)->number == active);
    CHECK(boat_session_step(&session, (BoatInput) { 0 }));
    while (plugin->staging)
        CHECK(!wake_plugin_frame(plugin, &session, false, false, false, .25));
    CHECK(!plugin->alert && boat_plugin_host_active(host)->number != active);
    CHECK(session.voyage.route == &boat_plugin_host_active(host)->voyage.route);
    CHECK(boat_plugin_objective_digest(&plugin->objective) == objective && session.tick == 2);
    boat_session_destroy(&session);
    boat_world_resources_destroy(&resources);
    free(checkpoint);
    free(plugin);
    free(files);
    puts("isolated staging: checked data, live ticks, held apply, Undo, stale epochs, deadlines, "
         "cancellation, worker faults OK");
    return 0;
}
