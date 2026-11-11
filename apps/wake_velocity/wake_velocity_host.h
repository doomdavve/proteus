#ifndef WAKE_VELOCITY_HOST_H
#define WAKE_VELOCITY_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Host from wake_velocity (docs/MULTIPLAYER_PLAN.md#host-from-the-game--2026-10-25): a second copy
// of this executable runs the room as `--serve` (boat_server's command line, src/room/boat_server.h)
// in a child process, so the room's 120 Hz authority never waits for this window's frames, focus or
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
    const char *mode; // "free", "deathmatch", "race" or "hunt".
    const char *location; // A boat_location name; race and hunt rooms are always on the outer map.
    bool waves;
    uint16_t port;
} NetHostOptions;

// The child's arguments after the program path, NULL-terminated: at most 13 plus the NULL.
#define NET_HOST_ARGUMENTS 16
// Fill OUT (PORT holds the port's text) and return the argument count. Race rooms take the
// outer map from their mode; hunt rooms are free rooms on outer with the shelter hunt package.
unsigned net_host_arguments(
    const NetHostOptions *options, char port[8], const char *out[NET_HOST_ARGUMENTS]);
// Start the room listening on every IPv4 interface and wait (at most five seconds) until it says
// so. False with ERROR set when it cannot start or exits first.
bool net_host_start(NetHost *host, const NetHostOptions *options, char *error, size_t size);
// Pass on the child's output without blocking. False once it has exited (ERROR says why).
bool net_host_poll(NetHost *host, char *error, size_t size);
// Close the room: end its input, give it a second to say goodbye to clients, then kill it.
void net_host_stop(NetHost *host);
#endif
