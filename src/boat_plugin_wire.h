#ifndef PROTEUS_BOAT_PLUGIN_WIRE_H
#define PROTEUS_BOAT_PLUGIN_WIRE_H

// PD1: one fieldwise, portable encoding for a fully decoded plugin definition. Producer and
// consumer come from one build, so the format needs no compatibility promise with older
// releases, only with itself. Byte arrays, not structs, cross the boundary: padding and native
// endianness never become content.
#include "boat_plugin_host.h"
#include "boat_plugin_lua.h"

#define BOAT_PLUGIN_WIRE_VERSION 0x50535437u // PST7: match kind and scenes in free/race/match.

typedef struct {
    BoatPluginKind kind;
    char manifest[BOAT_PLUGIN_MANIFEST_MAX];
    size_t manifest_size;
    BoatPluginManifest parsed; // Filled by decoding the manifest text.
    BoatPluginVoyageData voyage;
    BoatPluginRaceData race;
    BoatPluginFreeData free;
    BoatPluginCourseData course;
    BoatPluginMatchData match;
    char source[BOAT_PLUGIN_SOURCE_MAX];
    size_t source_size;
} BoatPluginDecoded;

// Exact encoded size, or 0 if the decoded values are outside the wire's bounds.
size_t boat_plugin_wire_size(const BoatPluginDecoded *decoded);
bool boat_plugin_wire_encode(
    const BoatPluginDecoded *decoded, unsigned char *bytes, size_t capacity, size_t *size);
// Reject truncation, extra bytes, non-finite values, out-of-range counts and unknown enums.
bool boat_plugin_wire_decode(const unsigned char *bytes, size_t size, BoatPluginDecoded *decoded,
    BoatPluginDiagnostic *diagnostic);
// Apply the full native schema checks to decoded values without opening a Lua VM.
bool boat_plugin_decoded_revision(const BoatPluginDecoded *decoded, BoatPluginRevision *revision,
    BoatPluginDiagnostic *diagnostic);

#endif
