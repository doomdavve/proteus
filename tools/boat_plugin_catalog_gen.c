// PD1: evaluate bundled packages at build time, validate them exactly as staging does, and
// write their decoded definitions as byte arrays. An invalid package fails the build.
#include "boat_plugin.h"
#include "boat_plugin_catalog.h"
#include "boat_plugin_io.h"
#include "boat_plugin_lua.h"
#include "boat_plugin_wire.h"
#include "boat_world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    BoatPluginManifest manifest;
    BoatPluginKind kind;
    BoatPluginRevision revision;
    BoatPluginDecoded decoded;
    uint64_t digest;
    unsigned char *wire;
    size_t wire_size;
} Entry;

static void show(const char *directory, const BoatPluginDiagnostic *d)
{
    fprintf(stderr, "boat_plugin_catalog_gen: %s: %s error in %s%s%.0d %s: %s\n", directory,
        boat_plugin_status_name(d->status), d->file, d->line ? ":" : "", d->line, d->field,
        d->message);
}

static bool evaluate(const char *directory, Entry *entry)
{
    BoatPluginFiles files;
    BoatPluginDiagnostic d = { 0 };
    if (boat_plugin_read(directory, NULL, &files, &d) != BOAT_PLUGIN_OK) {
        show(directory, &d);
        return false;
    }
    if (boat_plugin_parse_manifest(files.manifest, files.manifest_size, NULL, &entry->manifest, &d)
        != BOAT_PLUGIN_OK) {
        show(directory, &d);
        return false;
    }
    entry->kind = boat_plugin_kind_parse(entry->manifest.kind);
    if (entry->manifest.objective) {
        d.status = BOAT_PLUGIN_MANIFEST;
        snprintf(d.file, sizeof(d.file), "%s", "plugin.json");
        snprintf(d.field, sizeof(d.field), "%s", "capabilities");
        snprintf(d.message, sizeof(d.message),
            "catalog packages have no objective callbacks; authoring packages only");
        show(directory, &d);
        return false;
    }
    BoatWorldResources resources;
    if (!boat_world_resources_init(&resources, boat_location_find(entry->manifest.location))) {
        fprintf(stderr, "boat_plugin_catalog_gen: %s: cannot prepare the %s world\n", directory,
            entry->manifest.location);
        return false;
    }
    BoatPluginStatus status = BOAT_PLUGIN_OK;
    entry->decoded.kind = entry->kind;
    entry->decoded.manifest_size = files.manifest_size;
    memcpy(entry->decoded.manifest, files.manifest, files.manifest_size);
    entry->decoded.source_size = files.source_size;
    memcpy(entry->decoded.source, files.source, files.source_size);
    switch (entry->kind) {
    case BOAT_PLUGIN_KIND_VOYAGE:
        status = boat_plugin_lua_voyage(files.source, files.source_size, entry->manifest.entry,
            &boat_plugin_default_limits, &entry->decoded.voyage, &d);
        if (status == BOAT_PLUGIN_OK)
            status = boat_plugin_decode_voyage(
                &entry->manifest, &entry->decoded.voyage, &entry->revision.voyage, &d);
        break;
    case BOAT_PLUGIN_KIND_RACE:
        status = boat_plugin_lua_race(files.source, files.source_size, entry->manifest.entry,
            &boat_plugin_default_limits, &entry->decoded.race, &d);
        if (status == BOAT_PLUGIN_OK)
            status = boat_plugin_decode_race(
                &entry->manifest, &entry->decoded.race, &entry->revision.race, &d);
        break;
    case BOAT_PLUGIN_KIND_FREE:
        status = boat_plugin_lua_free(files.source, files.source_size, entry->manifest.entry,
            &boat_plugin_default_limits, &entry->decoded.free, &d);
        if (status == BOAT_PLUGIN_OK)
            status = boat_plugin_decode_free(
                &entry->manifest, &entry->decoded.free, &entry->revision.free, &d);
        break;
    case BOAT_PLUGIN_KIND_COURSE:
        status = boat_plugin_lua_course(files.source, files.source_size, entry->manifest.entry,
            &boat_plugin_default_limits, &entry->decoded.course, &d);
        if (status == BOAT_PLUGIN_OK)
            status = boat_plugin_decode_course(
                &entry->manifest, &entry->decoded.course, &entry->revision.course, &d);
        break;
    default:
        status = BOAT_PLUGIN_MANIFEST;
        break;
    }
    if (status == BOAT_PLUGIN_OK) {
        switch (entry->kind) {
        case BOAT_PLUGIN_KIND_VOYAGE:
            status = boat_plugin_check_location(
                &entry->revision.voyage, resources.definition, &resources.map, &d);
            break;
        case BOAT_PLUGIN_KIND_RACE:
            status = boat_plugin_race_check_location(
                &entry->revision.race, resources.definition, &resources.map, &d);
            break;
        case BOAT_PLUGIN_KIND_FREE:
            status
                = boat_plugin_free_check_location(&entry->revision.free, resources.definition, &d);
            break;
        case BOAT_PLUGIN_KIND_COURSE:
            status = boat_plugin_course_check_location(
                &entry->revision.course, resources.definition, &resources.map, &d);
            break;
        default:
            break;
        }
    }
    boat_world_resources_destroy(&resources);
    if (status != BOAT_PLUGIN_OK) {
        show(directory, &d);
        return false;
    }
    entry->revision.manifest = entry->manifest;
    entry->revision.source_size = files.source_size;
    memcpy(entry->revision.source, files.source, files.source_size);
    switch (entry->kind) {
    case BOAT_PLUGIN_KIND_VOYAGE:
        entry->digest = entry->revision.voyage.digest;
        break;
    case BOAT_PLUGIN_KIND_RACE:
        entry->digest = entry->revision.race.digest;
        break;
    case BOAT_PLUGIN_KIND_COURSE:
        entry->digest = entry->revision.course.digest;
        break;
    case BOAT_PLUGIN_KIND_FREE:
        entry->digest = entry->revision.free.digest;
        break;
    default:
        break;
    }
    entry->wire_size = boat_plugin_wire_size(&entry->decoded);
    if (!entry->wire_size) {
        fprintf(stderr, "boat_plugin_catalog_gen: %s: definition does not encode\n", directory);
        return false;
    }
    entry->wire = malloc(entry->wire_size);
    size_t written = 0;
    if (!entry->wire
        || !boat_plugin_wire_encode(&entry->decoded, entry->wire, entry->wire_size, &written)
        || written != entry->wire_size) {
        fprintf(stderr, "boat_plugin_catalog_gen: %s: cannot encode definition\n", directory);
        return false;
    }
    return true;
}

static void c_string(FILE *out, const char *text)
{
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '"' || *p == '\\')
            fputc('\\', out);
        if (*p >= 32 && *p < 127)
            fputc(*p, out);
        else
            fprintf(out, "\\x%02x", *p);
    }
    fputc('"', out);
}

static bool write_output(const char *path, const Entry *entries, unsigned count)
{
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "boat_plugin_catalog_gen: cannot write %s\n", path);
        return false;
    }
    fprintf(out,
        "// Generated by boat_plugin_catalog_gen. Do not edit.\n"
        "#include \"boat_plugin_catalog.h\"\n\n");
    for (unsigned i = 0; i < count; ++i) {
        fprintf(
            out, "static const unsigned char entry_%u_wire[%zu] = {\n", i, entries[i].wire_size);
        for (size_t b = 0; b < entries[i].wire_size; ++b) {
            if (b % 12 == 0)
                fputs("    ", out);
            fprintf(out, "0x%02x,", entries[i].wire[b]);
            if (b % 12 == 11 || b + 1 == entries[i].wire_size)
                fputc('\n', out);
        }
        fputs("};\n\n", out);
    }
    fputs("const BoatPluginCatalogEntry boat_plugin_catalog_table[] = {\n", out);
    for (unsigned i = 0; i < count; ++i) {
        fputs("    { ", out);
        c_string(out, entries[i].manifest.id);
        fputs(", ", out);
        c_string(out, entries[i].manifest.name);
        fputs(", ", out);
        c_string(out, entries[i].manifest.kind);
        fputs(", ", out);
        c_string(out, entries[i].manifest.location);
        fprintf(out, ", entry_%u_wire, sizeof entry_%u_wire, 0x%016llxull },\n", i, i,
            (unsigned long long)entries[i].digest);
    }
    fputs("};\n\nconst unsigned boat_plugin_catalog_count\n"
          "    = sizeof(boat_plugin_catalog_table) / sizeof(boat_plugin_catalog_table[0]);\n",
        out);
    bool ok = fclose(out) == 0;
    if (!ok)
        fprintf(stderr, "boat_plugin_catalog_gen: cannot close %s\n", path);
    return ok;
}

int main(int argc, char **argv)
{
    const char *output = NULL;
    int first = 1;
    if (argc > 2 && !strcmp(argv[1], "--output")) {
        output = argv[2];
        first = 3;
    }
    if (first >= argc) {
        fprintf(stderr, "Usage: boat_plugin_catalog_gen [--output FILE] PACKAGE...\n");
        return 2;
    }
    unsigned count = (unsigned)(argc - first);
    Entry *entries = calloc(count, sizeof(*entries));
    if (!entries)
        return 1;
    bool ok = true;
    for (unsigned i = 0; i < count && ok; ++i) {
        ok = evaluate(argv[first + (int)i], &entries[i]);
        if (ok)
            for (unsigned j = 0; j < i; ++j)
                if (!strcmp(entries[j].manifest.id, entries[i].manifest.id)) {
                    fprintf(stderr, "boat_plugin_catalog_gen: duplicate package id %s\n",
                        entries[i].manifest.id);
                    ok = false;
                }
    }
    if (ok && output)
        ok = write_output(output, entries, count);
    if (ok)
        printf("boat_plugin_catalog=%u packages\n", count);
    for (unsigned i = 0; i < count; ++i)
        free(entries[i].wire);
    free(entries);
    return ok ? 0 : 1;
}
