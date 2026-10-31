// Explicit performance experiment, outside CTest: no sleeps or timing-based assertions.
#include "wake_velocity_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SAMPLES 100

static double now(void)
{
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int compare(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void report(const char *name, double *samples, const BoatPluginDiagnostic *d)
{
    qsort(samples, SAMPLES, sizeof(*samples), compare);
    printf("%s ms_p50=%.3f ms_p95=%.3f ms_max=%.3f status=%s lua_peak=%zu "
           "entry_instructions=%llu callback_instructions=%llu leaked=%zu\n",
        name, samples[49], samples[94], samples[99], boat_plugin_status_name(d->status),
        d->cost.peak_bytes, (unsigned long long)d->cost.entry_instructions,
        (unsigned long long)d->cost.callback_instructions, d->cost.leaked_bytes);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr,
            "Usage: boat_plugin_bench DIRECTORY\n"
            "100 synchronous samples per case; elapsed UTC time, no enforced deadline.\n");
        return 2;
    }
    BoatPluginFiles files;
    BoatPluginDiagnostic d;
    if (boat_plugin_read(argv[1], NULL, &files, &d) != BOAT_PLUGIN_OK)
        return 1;
    if (files.source_size > BOAT_PLUGIN_SOURCE_MAX - 128) {
        fprintf(stderr, "Benchmark needs 128 bytes of source headroom for generated variants.\n");
        return 2;
    }
    BoatPluginBundle bundle = boat_plugin_bundle(&files);
    BoatPluginManifest manifest;
    if (boat_plugin_parse_manifest(bundle.manifest, bundle.manifest_size, NULL, &manifest, &d)
        != BOAT_PLUGIN_OK)
        return 1;
    const BoatWorldDefinition *location = boat_location_find(manifest.location);
    BoatWorldResources resources;
    if (!location || !boat_world_resources_init(&resources, location))
        return 1;
    BoatPluginHost *host = calloc(1, sizeof(*host));
    int result = 1;
    if (!host || !boat_plugin_host_start(host, location, &resources.map, &bundle))
        goto done;
    double samples[SAMPLES];
    // Repeated source variants exercise staging, init probing, history eviction and slot reuse.
    char source[BOAT_PLUGIN_SOURCE_MAX];
    for (unsigned full = 0; full < 3; ++full) {
        for (unsigned i = 0; i < SAMPLES; ++i) {
            size_t prefix = 0;
            if (full == 2) {
                const char *table = "local unused={";
                prefix = strlen(table);
                memcpy(source, table, prefix);
                while (prefix + files.source_size + 64 < sizeof(source)) {
                    source[prefix++] = '1';
                    source[prefix++] = ',';
                }
                source[prefix++] = '}';
                source[prefix++] = ';';
            }
            memcpy(source + prefix, files.source, files.source_size);
            prefix += files.source_size;
            int suffix = snprintf(
                source + prefix, sizeof(source) - prefix, "\n-- revision %u %u\n", full, i);
            if (suffix < 0 || (size_t)suffix >= sizeof(source) - prefix)
                goto done;
            size_t size = prefix + (size_t)suffix;
            if (full) {
                memset(source + size, ' ', sizeof(source) - size);
                size = sizeof(source);
            }
            bundle.source = source;
            bundle.source_size = size;
            double started = now();
            bool ok = boat_plugin_host_stage(host, &bundle);
            samples[i] = (now() - started) * 1000;
            d = host->diagnostic;
            if (!ok || (host->held && !boat_plugin_host_commit(host)))
                goto done;
        }
        report(full == 2 ? "stage_dense_64KiB"
                : full   ? "stage_padded_64KiB"
                         : "stage_reference",
            samples, &d);
    }
    for (unsigned i = 0; i < SAMPLES; ++i) {
        double started = now();
        if (boat_plugin_read(argv[1], NULL, &files, &d) != BOAT_PLUGIN_OK)
            goto done;
        bundle = boat_plugin_bundle(&files);
        if (!boat_plugin_host_stage(host, &bundle))
            goto done;
        samples[i] = (now() - started) * 1000;
        d = host->diagnostic;
        if (host->held && !boat_plugin_host_commit(host))
            goto done;
    }
    report("read_and_stage", samples, &d);
    // A maximum-size token stream tests compiler rejection, which hooks cannot interrupt.
    memset(source, '(', sizeof(source));
    bundle.source = source;
    bundle.source_size = sizeof(source);
    for (unsigned i = 0; i < SAMPLES; ++i) {
        double started = now();
        if (boat_plugin_host_stage(host, &bundle))
            goto done;
        samples[i] = (now() - started) * 1000;
    }
    report("compiler_rejection_64KiB", samples, &host->diagnostic);
    BoatPluginHostMemory memory = boat_plugin_host_memory(host);
    printf("host_reserved=%zu retained_revisions=%u retained_source=%zu lua_limit=%zu\n",
        memory.reserved_bytes, memory.retained_revisions, memory.retained_source_bytes,
        boat_plugin_default_limits.memory_bytes);
    printf("native_owner=%zu owned_ceiling=%zu preview_checkpoint=%zu\n", sizeof(WakePlugin),
        sizeof(WakePlugin) + boat_plugin_default_limits.memory_bytes, sizeof(WakePluginPreview));
    if (manifest.objective) {
        const char *scripts[] = {
            files.source,
            "return {update=function(c,s) while true do end end}",
            "return {migrate=function(c,s) while true do end end}",
            "while true do end",
            "return {update=function(c,s) return {s=string.rep('x',3000000)},{} end}",
            "local n=0; for i=1,200000 do n=n+1 end; return {update=function(c,s) "
            "for i=1,20000 do n=n+1 end; return s,{} end}",
        };
        const char *names[] = { "update_reference", "callback_budget", "migration_budget",
            "entry_budget", "heap_rejection", "entry_plus_callback" };
        BoatPluginObjective initial = { .generation = 1,
            .rng = 12345,
            .state_version = manifest.state_version,
            .timers_enabled = manifest.timers };
        if (boat_plugin_objective_step(&initial, files.source, files.source_size,
                BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0, NULL, &d)
            != BOAT_PLUGIN_OK)
            goto done;
        for (unsigned script = 0; script < sizeof(scripts) / sizeof(*scripts); ++script) {
            for (unsigned i = 0; i < SAMPLES; ++i) {
                BoatPluginObjective objective = initial;
                double started = now();
                BoatPluginStatus status = boat_plugin_objective_step(&objective, scripts[script],
                    script == 0 ? files.source_size : strlen(scripts[script]),
                    script == 2 ? BOAT_PLUGIN_OBJECTIVE_MIGRATE : BOAT_PLUGIN_OBJECTIVE_UPDATE, 12,
                    NULL, 0, NULL, &d);
                samples[i] = (now() - started) * 1000;
                BoatPluginStatus expected = (script == 0 || script == 5) ? BOAT_PLUGIN_OK
                    : script == 4                                        ? BOAT_PLUGIN_MEMORY
                                                                         : BOAT_PLUGIN_BUDGET;
                if (status != expected || d.cost.leaked_bytes)
                    goto done;
            }
            report(names[script], samples, &d);
        }
    }
    result = 0;
done:
    if (result)
        fprintf(stderr, "Benchmark failed: %s %s\n", boat_plugin_status_name(d.status), d.message);
    free(host);
    boat_world_resources_destroy(&resources);
    return result;
}
