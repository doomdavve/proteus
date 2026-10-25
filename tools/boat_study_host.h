#ifndef BOAT_STUDY_HOST_H
#define BOAT_STUDY_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Host from boat_study (docs/MULTIPLAYER_PLAN.md#host-from-the-game--2026-10-25): a second copy of
// this executable runs the room as `--serve` (boat_server's command line, tools/boat_server.h) in a
// child process, so the room's 120 Hz authority never waits for this window's frames, focus or
// audio, and shares none of this process's lazily built map and model caches. The child reads its
// standard input from a pipe and closes the room when that pipe ends, including when this process
// dies; its output comes back through a second pipe, is echoed here and keeps the last line for
// error messages.
typedef struct {
    bool running;
    uint16_t port;
    char last[200]; // The newest complete output line.
    char partial[200];
    size_t partial_length;
#ifdef _WIN32
    void *process, *input, *output; // HANDLEs.
#else
    int pid, input, output;
#endif
} NetHost;

typedef struct {
    const char *mode; // "free", "deathmatch" or "race".
    const char *location; // A boat_location name; race rooms are always on the river.
    bool waves;
    uint16_t port;
} NetHostOptions;

// Start the room listening on every IPv4 interface and wait (at most five seconds) until it says
// so. False with ERROR set when it cannot start or exits first.
bool net_host_start(NetHost *host, const NetHostOptions *options, char *error, size_t size);
// Pass on the child's output without blocking. False once it has exited (ERROR says why).
bool net_host_poll(NetHost *host, char *error, size_t size);
// Close the room: end its input, give it a second to say goodbye to clients, then kill it.
void net_host_stop(NetHost *host);
#endif
