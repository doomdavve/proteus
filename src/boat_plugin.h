#ifndef PROTEUS_BOAT_PLUGIN_H
#define PROTEUS_BOAT_PLUGIN_H

// Lua plugin P0 (docs/PLUGIN_PLAN.md, docs/PLUGIN_API.md): a bounded, data-only loader that
// evaluates one plugin's manifest and Lua entry text and decodes its voyage into owned native
// data. Input is immutable bytes; nothing here touches files (boat_plugin_io.h acquires them).
// Every load uses a fresh restricted Lua state that is closed before the call returns.
#include "boat_voyage.h"

#include <stddef.h>
#include <stdint.h>

#define BOAT_PLUGIN_API 1
#define BOAT_PLUGIN_STATE_VERSION 1
// Host capacities. Limits passed to a load may be lower, never higher.
#define BOAT_PLUGIN_MANIFEST_MAX 4096
#define BOAT_PLUGIN_SOURCE_MAX 65536
#define BOAT_PLUGIN_TEXT 24 // Shelter id and name capacity, including the terminator.

typedef enum {
    BOAT_PLUGIN_OK,
    BOAT_PLUGIN_LIMIT, // Input larger than the host allows.
    BOAT_PLUGIN_IO, // The package's files could not be acquired (boat_plugin_io).
    BOAT_PLUGIN_MANIFEST, // plugin.json is malformed, unsupported or asks for what is not granted.
    BOAT_PLUGIN_SYNTAX, // The entry text does not compile, or is not text.
    BOAT_PLUGIN_RUNTIME, // The entry raised an error while evaluating.
    BOAT_PLUGIN_MEMORY, // The Lua state exhausted its memory limit.
    BOAT_PLUGIN_BUDGET, // The entry exhausted its instruction budget.
    BOAT_PLUGIN_SCHEMA, // The returned definition is not a valid voyage.
} BoatPluginStatus;

typedef struct {
    size_t manifest_bytes, source_bytes; // At most the capacities above.
    size_t memory_bytes; // Everything the Lua state allocates, compiler included.
    uint64_t instructions; // Lua VM instructions while evaluating the entry.
} BoatPluginLimits;
extern const BoatPluginLimits boat_plugin_default_limits; // 4 KiB, 64 KiB, 2 MiB, 1 M.

// Measured work of one load; filled whether or not it succeeds.
typedef struct {
    // Completed steps of BOAT_PLUGIN_HOOK_STEP instructions: the true count is below
    // INSTRUCTIONS + BOAT_PLUGIN_HOOK_STEP.
    uint64_t instructions;
    size_t peak_bytes; // Largest Lua heap, compiler included.
    size_t leaked_bytes; // Still allocated after the state closed: always zero.
    unsigned fields; // Values the native decoder read.
} BoatPluginCost;
#define BOAT_PLUGIN_HOOK_STEP 100

typedef struct {
    BoatPluginStatus status;
    char file[16]; // "plugin.json" or the entry's name; empty when not about one file.
    int line; // Source line, or 0 when unknown (always 0 for schema errors).
    char field[64]; // Path of the offending value, e.g. "voyage.shelters[3].heading".
    char message[160];
    BoatPluginCost cost;
} BoatPluginDiagnostic;

typedef struct {
    const char *manifest;
    size_t manifest_size;
    const char *source; // The manifest's entry file.
    size_t source_size;
} BoatPluginBundle;

typedef struct {
    char id[64], name[64], kind[16], entry[16], location[16];
    unsigned api, state_version;
} BoatPluginManifest;

// Owns its strings: ROUTE's shelter names point into SHELTER_NAMES, so copy it only with
// boat_plugin_voyage_copy.
typedef struct BoatPluginVoyage {
    BoatPluginManifest manifest;
    BoatVoyageRoute route;
    char shelter_ids[BOAT_VOYAGE_SHELTERS][BOAT_PLUGIN_TEXT];
    char shelter_names[BOAT_VOYAGE_SHELTERS][BOAT_PLUGIN_TEXT];
    uint64_t digest; // boat_plugin_route_digest(&route).
} BoatPluginVoyage;

const char *boat_plugin_status_name(BoatPluginStatus status);
// Parse and check a manifest against the host policy. OUT is written only on success; DIAGNOSTIC
// (may be NULL) is always written.
BoatPluginStatus boat_plugin_parse_manifest(const char *json, size_t size,
    const BoatPluginLimits *limits, BoatPluginManifest *out, BoatPluginDiagnostic *diagnostic);
// Evaluate BUNDLE and decode its voyage. OUT is written only on success, so a failure leaves the
// caller's last good definition intact. LIMITS NULL selects the defaults.
BoatPluginStatus boat_plugin_load_voyage(const BoatPluginBundle *bundle,
    const BoatPluginLimits *limits, BoatPluginVoyage *out, BoatPluginDiagnostic *diagnostic);
void boat_plugin_voyage_copy(BoatPluginVoyage *destination, const BoatPluginVoyage *source);
// FNV-1a over every field of ROUTE, names included, with float bits least significant first.
uint64_t boat_plugin_route_digest(const BoatVoyageRoute *route);

#endif
