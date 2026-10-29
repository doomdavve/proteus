// Private P4 worker: immutable framed bytes in, bounded JSON evidence out. No package paths.
#include "boat_plugin_host.h"
#include "boat_world.h"

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

int main(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    unsigned char header[8];
    BoatPluginFiles *files = calloc(1, sizeof(*files));
    BoatPluginHost *host = calloc(1, sizeof(*host));
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
    if (!boat_world_resources_init(&resources, boat_location_find(manifest.location))) {
        d.status = BOAT_PLUGIN_MEMORY;
        goto done;
    }
    boat_plugin_host_start(host, resources.definition, &resources.map, &bundle);
    d = host->diagnostic;
    if (d.status == BOAT_PLUGIN_OK) {
        const BoatPluginRevision *revision = boat_plugin_host_active(host);
        digest = host->racing ? revision->race.digest : revision->voyage.digest;
    }
done:
    printf("{\"status\":");
    json_text(boat_plugin_status_name(d.status));
    printf(",\"file\":");
    json_text(d.file);
    printf(",\"line\":%d,\"field\":", d.line);
    json_text(d.field);
    printf(",\"message\":");
    json_text(d.message);
    printf(",\"gameplay_digest\":\"%016llx\",\"cost\":{\"instructions\":%llu,"
           "\"entry_instructions\":%llu,\"callback_instructions\":%llu,"
           "\"peak_bytes\":%zu,\"leaked_bytes\":%zu,\"fields\":%u}}\n",
        (unsigned long long)digest, (unsigned long long)d.cost.instructions,
        (unsigned long long)d.cost.entry_instructions,
        (unsigned long long)d.cost.callback_instructions, d.cost.peak_bytes, d.cost.leaked_bytes,
        d.cost.fields);
    boat_world_resources_destroy(&resources);
    free(host);
    free(files);
    return d.status == BOAT_PLUGIN_OK ? 0 : 1;
}
