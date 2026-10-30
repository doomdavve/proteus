#ifndef PROTEUS_BOAT_PLUGIN_EVAL_H
#define PROTEUS_BOAT_PLUGIN_EVAL_H

#include "boat_plugin_state.h"

#define BOAT_PLUGIN_EVAL_REQUEST_MAX 131072u
#define BOAT_PLUGIN_EVAL_REPLY_MAX 49152u

// Private PEQ1/PER1 protocol. No native layouts, pointers or JavaScript numeric conversion.
typedef struct {
    uint64_t serial, epoch;
    unsigned revision, candidate;
    BoatPluginTransition transition;
    int64_t tick;
    unsigned version, generation;
    bool timers;
    BoatPluginObjective objective;
    char source[BOAT_PLUGIN_SOURCE_MAX];
    size_t source_size;
    BoatPluginEvent events[BOAT_PLUGIN_EVENTS];
    unsigned event_count;
} BoatPluginEvalRequest;

size_t boat_plugin_eval_encode_request(
    const BoatPluginEvalRequest *request, unsigned char *bytes, size_t capacity);
// Invalid requests leave OUT unchanged. This only decodes/validates data; it never runs Lua.
bool boat_plugin_eval_decode_request(
    const unsigned char *bytes, size_t size, BoatPluginEvalRequest *out);
// Worker only. Invalid framing/allocation/output capacity returns zero. Every valid request
// gets a matched reply, including Lua failures. Each execution creates a fresh Lua VM.
size_t boat_plugin_eval_execute(
    const unsigned char *input, size_t size, unsigned char *output, size_t capacity);
// True means a completely framed and matched reply, not necessarily a successful callback.
// DIAGNOSTIC is written on matched replies; OUT is written only for BOAT_PLUGIN_OK.
// This validates the checkpoint and protected fields without evaluating Lua.
bool boat_plugin_eval_result(const BoatPluginEvalRequest *expected, const unsigned char *reply,
    size_t size, BoatPluginObjective *out, BoatPluginDiagnostic *diagnostic);

#endif
