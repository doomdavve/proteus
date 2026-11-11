// Plugin evaluation: each load parses and bounds the bundle, runs its Lua entry in the restricted
// evaluator, then hands the copied result to the Lua-free decoders in boat_plugin.c and
// boat_plugin_race.c.
#include "boat_plugin_load.h"
#include "boat_plugin_lua.h"

#include <stdio.h>
#include <string.h>

static const BoatPluginLimits *bounded(const BoatPluginLimits *limits)
{
    return limits ? limits : &boat_plugin_default_limits;
}

static BoatPluginStatus fail(BoatPluginDiagnostic *d, BoatPluginStatus status, const char *file,
    const char *field, const char *message)
{
    d->status = status;
    d->line = 0;
    snprintf(d->file, sizeof(d->file), "%s", file);
    snprintf(d->field, sizeof(d->field), "%s", field);
    snprintf(d->message, sizeof(d->message), "%s", message);
    return status;
}

BoatPluginStatus boat_plugin_load_voyage(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginVoyage *out, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginDiagnostic scratch;
    BoatPluginDiagnostic *d = diagnostic ? diagnostic : &scratch;
    BoatPluginManifest manifest;
    BoatPluginStatus status
        = boat_plugin_bundle_manifest(bundle, limits, BOAT_PLUGIN_KIND_VOYAGE, &manifest, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    BoatPluginVoyageData data;
    status = boat_plugin_lua_voyage(
        bundle->source, bundle->source_size, manifest.entry, bounded(limits), &data, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    return boat_plugin_decode_voyage(&manifest, &data, out, d);
}

BoatPluginStatus boat_plugin_load_free(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginFree *out, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginDiagnostic scratch;
    BoatPluginDiagnostic *d = diagnostic ? diagnostic : &scratch;
    BoatPluginManifest manifest;
    BoatPluginStatus status
        = boat_plugin_bundle_manifest(bundle, limits, BOAT_PLUGIN_KIND_FREE, &manifest, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    BoatPluginFreeData data;
    status = boat_plugin_lua_free(
        bundle->source, bundle->source_size, manifest.entry, bounded(limits), &data, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    return boat_plugin_decode_free(&manifest, &data, out, d);
}

BoatPluginStatus boat_plugin_load_course(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginCourse *out, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginDiagnostic scratch;
    BoatPluginDiagnostic *d = diagnostic ? diagnostic : &scratch;
    BoatPluginManifest manifest;
    BoatPluginStatus status
        = boat_plugin_bundle_manifest(bundle, limits, BOAT_PLUGIN_KIND_COURSE, &manifest, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    BoatPluginCourseData data;
    status = boat_plugin_lua_course(
        bundle->source, bundle->source_size, manifest.entry, bounded(limits), &data, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    return boat_plugin_decode_course(&manifest, &data, out, d);
}

BoatPluginStatus boat_plugin_load_match(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginMatch *out, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginDiagnostic scratch;
    BoatPluginDiagnostic *d = diagnostic ? diagnostic : &scratch;
    BoatPluginManifest manifest;
    BoatPluginStatus status
        = boat_plugin_bundle_manifest(bundle, limits, BOAT_PLUGIN_KIND_MATCH, &manifest, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    BoatPluginMatchData data;
    status = boat_plugin_lua_match(
        bundle->source, bundle->source_size, manifest.entry, bounded(limits), &data, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    return boat_plugin_decode_match(&manifest, &data, out, d);
}

BoatPluginStatus boat_plugin_load_race(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginRace *out, BoatPluginDiagnostic *diagnostic)
{
    BoatPluginDiagnostic scratch;
    BoatPluginDiagnostic *d = diagnostic ? diagnostic : &scratch;
    if (!limits)
        limits = &boat_plugin_default_limits;
    BoatPluginRace r = { 0 };
    BoatPluginStatus status = boat_plugin_parse_manifest(
        bundle->manifest, bundle->manifest_size, limits, &r.manifest, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    if (strcmp(r.manifest.kind, "race"))
        return fail(d, BOAT_PLUGIN_MANIFEST, "plugin.json", "kind", "requires a race host");
    const char *file = r.manifest.entry;
    if (bundle->source_size > limits->source_bytes || bundle->source_size > BOAT_PLUGIN_SOURCE_MAX)
        return fail(d, BOAT_PLUGIN_LIMIT, file, "", "source exceeds the host limit");
    BoatPluginRaceData data;
    status = boat_plugin_lua_race(bundle->source, bundle->source_size, file, limits, &data, d);
    if (status != BOAT_PLUGIN_OK)
        return status;
    return boat_plugin_decode_race(&r.manifest, &data, out, d);
}
