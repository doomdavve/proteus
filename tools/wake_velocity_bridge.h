#ifndef WAKE_VELOCITY_BRIDGE_H
#define WAKE_VELOCITY_BRIDGE_H
// Plugin P4.6 live tool bridge (--plugin-bridge DIR, native only): a local file mailbox through
// which tools/plugin_author.py inspects the running plugin owner and stages, applies, undoes or
// discards revisions. Requests carry the launch's random token, read from DIR/session, which
// only the owner of the private DIR can read. Each mutation is pinned to the active revision and
// commit epoch; everything happens between ticks through the same transactions as F5-F7.
// docs/PLUGIN_API.md owns the request and response format.
#include "wake_command.h"
#include "wake_velocity_plugin.h"

#define WAKE_BRIDGE_PROTOCOL 1
#define WAKE_BRIDGE_ID 32 // Request ids: 1..32 lowercase hex digits.

typedef struct {
    char directory[1024];
    char token[33], session[17];
    double next_poll;
    // A stage request answered when its isolated staging finishes.
    char pending[WAKE_BRIDGE_ID + 1];
    unsigned pending_requests;
    char last_response[WAKE_BRIDGE_ID + 1]; // Removed when the next one is written.
    bool open;
    // PC: the same command layer the console uses. NULL in a session without a console.
    WakeCommand *commands;
    char console_lines[16][192];
    unsigned console_count;
} WakeBridge;

// Create or reuse the private DIRECTORY and publish a new session token. False with the reason
// printed. Ordinary play without --plugin-bridge never opens one.
bool wake_bridge_open(WakeBridge *bridge, const char *directory, double now);
// Share the console's command state so bridge and console submit the same edits.
void wake_bridge_commands(WakeBridge *bridge, WakeCommand *commands);
// Answer any pending request as cancelled and withdraw the session file.
void wake_bridge_close(WakeBridge *bridge);
// At a tick boundary, before wake_plugin_frame: answer a finished staging request, then poll for
// one new request every BOAT_PLUGIN_POLL seconds of NOW. Same return as wake_plugin_frame.
bool wake_bridge_frame(WakeBridge *bridge, WakePlugin *plugin, BoatSession *session, double now);
#endif
