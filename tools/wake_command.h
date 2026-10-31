#ifndef WAKE_COMMAND_H
#define WAKE_COMMAND_H
// PC (docs/PLUGIN_DEFAULT_PLAN.md): the live console and tool bridge's one command layer.
// A line is an edit of `def`, a read-only `=` query, or a `/` command. Edits are composed as one
// immutable entry file and submitted through the isolated staging worker; nothing here runs Lua
// on the game thread, writes the live world, or moves the boat. No raylib and no file access:
// saving is a caller-supplied operation.
#include "wake_velocity_plugin.h"

#define WAKE_COMMAND_LINE 512
#define WAKE_COMMAND_CHUNKS 32
#define WAKE_COMMAND_CHUNK_TEXT 384

typedef void (*WakeCommandPrint)(void *context, const char *line);
typedef void (*WakeCommandClear)(void *context);
typedef bool (*WakeCommandSave)(
    void *context, const WakePlugin *plugin, const char *directory, char *message, size_t capacity);

typedef struct {
    WakeCommandPrint print;
    WakeCommandPrint tee; // An optional second sink, used by the tool bridge.
    WakeCommandClear clear;
    WakeCommandSave save;
    void *context, *tee_context;
    // Console edits, oldest first. Each is composed onto BASE, the package's unchanged entry.
    char chunks[WAKE_COMMAND_CHUNKS][WAKE_COMMAND_CHUNK_TEXT];
    unsigned chunk_count;
    // Chunk count after each revision the console has observed, so Undo steps back through
    // revisions and their chunks together.
    unsigned counts[256];
    bool counts_known[256];
    unsigned last_revision;
    bool pending; // An edit is staging; CHUNK is its 1-based number.
    unsigned chunk;
    bool query_pending;
    // The package's unchanged entry source, captured before the first staged edit.
    char base[BOAT_PLUGIN_SOURCE_MAX];
    size_t base_size;
    bool base_ready;
} WakeCommand;

void wake_command_init(WakeCommand *command, WakeCommandPrint print, WakeCommandClear clear,
    WakeCommandSave save, void *context);
// Also send each result line to TEE while it is set; NULL clears it.
void wake_command_tee(WakeCommand *command, WakeCommandPrint tee, void *context);
// Parse and submit one line. True when the caller must restart the session (a forced /apply of a
// restarting change or /restart). Appends bounded result lines through PRINT.
bool wake_command_submit(
    WakeCommand *command, WakePlugin *plugin, BoatSession *session, const char *line, double now);
// Once a frame, after wake_plugin_frame: report a completed edit or query, and keep the chunk
// count aligned with the active revision (Undo, saves and bridge commits included).
bool wake_command_frame(WakeCommand *command, WakePlugin *plugin, BoatSession *session, double now);
// Compose BASE plus the accepted chunks, or false when the source cap would be exceeded.
size_t wake_command_source(
    const WakeCommand *command, const char *base, size_t base_size, char *out, size_t capacity);
// The console's own view of the active package: its revision, chunk count and pending edit.
void wake_command_status(
    const WakeCommand *command, const WakePlugin *plugin, char *out, size_t capacity);
#endif
