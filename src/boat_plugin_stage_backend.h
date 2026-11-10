#ifndef PROTEUS_BOAT_PLUGIN_STAGE_BACKEND_H
#define PROTEUS_BOAT_PLUGIN_STAGE_BACKEND_H
// Private to boat_plugin_stage.c and its CMake-selected backend: boat_plugin_stage_posix.c,
// boat_plugin_stage_windows.c or boat_plugin_stage_web.c. Common staging owns requests, wire
// framing, limits, deadlines and revision validity; a backend owns launch, polling, byte
// transfer, termination and its handles. No other file includes this header.
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum {
    BOAT_PLUGIN_WORKER_STAGE, // --plugin-stage-worker / browser "stage" mode
    BOAT_PLUGIN_WORKER_QUERY, // --plugin-query-worker / browser "query" mode
} BoatPluginWorkerRole;

typedef enum {
    BOAT_PLUGIN_WORKER_RUNNING = 0,
    BOAT_PLUGIN_WORKER_SUCCEEDED, // Exited 0 (browser state 1); RESPONSE holds its reply.
    BOAT_PLUGIN_WORKER_FAILED, // Exited non-zero (browser state 2); RESPONSE holds its reply.
    BOAT_PLUGIN_WORKER_BROKEN, // Poll failed, or the worker failed, was canceled or went stale.
    BOAT_PLUGIN_WORKER_UNREADABLE, // Completed, but its reply could not be transferred.
} BoatPluginWorkerState;

// Policy differences the selected backend imposes on common staging.
typedef struct {
    double deadline; // Acceptance deadline after start, in elapsed seconds.
    bool needs_executable; // Workers are EXECUTABLE re-launched with a worker argument.
    bool launch_receipt; // An empty host may consume a prepared launch receipt.
    bool solo_kinds_only; // The worker rejects room-only kinds (race, match).
    bool query_deadline_message; // A query deadline reports a diagnostic message.
} BoatPluginWorkerTraits;

extern const BoatPluginWorkerTraits boat_plugin_worker_traits;

typedef struct BoatPluginWorker BoatPluginWorker;

// Worker side: switch stdin/stdout to binary before reading a request.
void boat_plugin_worker_binary_stdio(void);
// Launch a disposable worker for the REQUEST_SIZE bytes in REQUEST, rewound to its start; the
// reply is collected into RESPONSE. PIN is the browser's base revision and epoch (low, high).
// The caller keeps owning both files and must not close them before boat_plugin_worker_close.
// NULL when the worker could not be started; nothing is left to release then.
BoatPluginWorker *boat_plugin_worker_start(const char *executable, BoatPluginWorkerRole role,
    FILE *request, size_t request_size, FILE *response, const unsigned pin[3]);
// Never blocks. On BROKEN or UNREADABLE, *REASON names the failure for diagnostics.
BoatPluginWorkerState boat_plugin_worker_poll(
    BoatPluginWorker *worker, FILE *response, const char **reason);
// Kill and reap, or terminate, a worker still running; release every backend resource. Safe on
// NULL, after any poll result and after a deadline or stale rejection.
void boat_plugin_worker_close(BoatPluginWorker *worker);
#endif
