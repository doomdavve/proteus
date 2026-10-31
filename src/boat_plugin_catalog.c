#include "boat_plugin_catalog.h"

#include "boat_plugin_wire.h"

#include <stdlib.h>
#include <string.h>

const BoatPluginCatalogEntry *boat_plugin_catalog_at(unsigned index)
{
    return index < boat_plugin_catalog_count ? &boat_plugin_catalog_table[index] : NULL;
}

const BoatPluginCatalogEntry *boat_plugin_catalog_find(const char *id)
{
    if (!id)
        return NULL;
    for (unsigned i = 0; i < boat_plugin_catalog_count; ++i)
        if (!strcmp(boat_plugin_catalog_table[i].id, id))
            return &boat_plugin_catalog_table[i];
    return NULL;
}

const BoatPluginCatalogEntry *boat_plugin_catalog_default(BoatPluginKind kind, const char *location)
{
    const char *id = NULL;
    if (!location)
        return NULL;
    if (kind == BOAT_PLUGIN_KIND_FREE)
        id = !strcmp(location, "basin")  ? BOAT_PLUGIN_DEFAULT_BASIN_FREE
            : !strcmp(location, "river") ? BOAT_PLUGIN_DEFAULT_RIVER_FREE
                                         : NULL;
    else if (kind == BOAT_PLUGIN_KIND_VOYAGE && !strcmp(location, "outer"))
        id = BOAT_PLUGIN_DEFAULT_OUTER_VOYAGE;
    else if (kind == BOAT_PLUGIN_KIND_COURSE && !strcmp(location, "basin"))
        id = BOAT_PLUGIN_DEFAULT_BASIN_COURSE;
    else if (kind == BOAT_PLUGIN_KIND_RACE && !strcmp(location, "river"))
        id = BOAT_PLUGIN_DEFAULT_RIVER_RACE;
    const BoatPluginCatalogEntry *entry = boat_plugin_catalog_find(id);
    if (!entry || boat_plugin_kind_parse(entry->kind) != kind || strcmp(entry->location, location))
        return NULL;
    return entry;
}

bool boat_plugin_catalog_decode(const BoatPluginCatalogEntry *entry, BoatPluginRevision *revision,
    BoatPluginDiagnostic *diagnostic)
{
    if (!entry || !entry->wire || !entry->wire_size)
        return false;
    BoatPluginDecoded *decoded = malloc(sizeof(*decoded));
    if (!decoded)
        return false;
    bool ok = boat_plugin_wire_decode(entry->wire, entry->wire_size, decoded, diagnostic);
    if (ok
        && (boat_plugin_kind_parse(entry->kind) != decoded->kind
            || strcmp(entry->id, decoded->parsed.id)))
        ok = false;
    if (ok)
        ok = boat_plugin_decoded_revision(decoded, revision, diagnostic);
    free(decoded);
    return ok;
}
