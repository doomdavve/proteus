// Browser staging backend: the page's Module.boatPluginStage transport (web/plugin_author.js)
// runs each request in a disposable Web Worker; the game Wasm only copies bytes in and out.
#include "boat_plugin_stage_backend.h"

#include <stdlib.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

// The bridge's reply ceiling, matching browser_read below.
#define BROWSER_REPLY_MAX 163840u
// The bridge's request ceiling, matching Transport.start in web/plugin_author.js.
#define BROWSER_REQUEST_MAX 131072u

// Browser workers start and evaluate Lua on the visitor's device; allow a slow phone. Browser
// solo has no race trial or match, and an empty host may consume a prepared launch receipt.
const BoatPluginWorkerTraits boat_plugin_worker_traits = {
    .deadline = 15.0,
    .launch_receipt = true,
    .solo_kinds_only = true,
    .query_deadline_message = true,
};

struct BoatPluginWorker {
    unsigned job; // Transport job id; 0 once closed.
    BoatPluginWorkerRole role;
};

#if defined(__EMSCRIPTEN__)
EM_JS(unsigned, browser_start,
    (const unsigned char *data, unsigned size, unsigned base, unsigned low, unsigned high), {
        if (!Module.boatPluginStage)
            return 0;
        return Module.boatPluginStage.start(
            HEAPU8.slice(data, data + size), [ base >>> 0, low >>> 0, high >>> 0 ]);
    })
EM_JS(int, browser_status, (unsigned job),
    { return Module.boatPluginStage ? Module.boatPluginStage.status(job) : -1; })
EM_JS(int, browser_read, (unsigned job, unsigned char *data), {
    const bytes = Module.boatPluginStage.read(job);
    if (!bytes || bytes.length > 163840)
        return -1;
    HEAPU8.set(bytes, data);
    return bytes.length;
})
EM_JS(void, browser_close, (unsigned job), {
    if (Module.boatPluginStage)
        Module.boatPluginStage.close(job);
})
EM_JS(unsigned, browser_query_start, (const unsigned char *data, unsigned size), {
    if (!Module.boatPluginStage || !Module.boatPluginStage.query)
        return 0;
    return Module.boatPluginStage.query(HEAPU8.slice(data, data + size), [ 0, 0, 0 ]);
})
#else
// A native build of this backend exists only in validate_boat_plugin_stage_web, which links a
// C model of the page transport (Transport in web/plugin_author.js) in place of these bridges.
unsigned browser_start(
    const unsigned char *data, unsigned size, unsigned base, unsigned low, unsigned high);
int browser_status(unsigned job);
int browser_read(unsigned job, unsigned char *data);
void browser_close(unsigned job);
unsigned browser_query_start(const unsigned char *data, unsigned size);
#endif

void boat_plugin_worker_binary_stdio(void) { }

BoatPluginWorker *boat_plugin_worker_start(const char *executable, BoatPluginWorkerRole role,
    FILE *request, size_t request_size, FILE *response, const unsigned pin[3])
{
    (void)executable;
    (void)response;
    if (!request_size || request_size > BROWSER_REQUEST_MAX)
        return NULL;
    BoatPluginWorker *w = calloc(1, sizeof(*w));
    unsigned char *payload = malloc(request_size);
    if (w && payload && fread(payload, 1, request_size, request) == request_size) {
        w->role = role;
        w->job = role == BOAT_PLUGIN_WORKER_QUERY
            ? browser_query_start(payload, (unsigned)request_size)
            : browser_start(payload, (unsigned)request_size, pin[0], pin[1], pin[2]);
    }
    free(payload);
    if (w && !w->job) {
        free(w);
        w = NULL;
    }
    return w;
}

BoatPluginWorkerState boat_plugin_worker_poll(
    BoatPluginWorker *w, FILE *response, const char **reason)
{
    int status = browser_status(w->job);
    if (!status)
        return BOAT_PLUGIN_WORKER_RUNNING;
    if (status < 0) {
        *reason = "browser worker failed, canceled or stale";
        return BOAT_PLUGIN_WORKER_BROKEN;
    }
    *reason = "invalid browser worker response";
    unsigned char *bytes = malloc(BROWSER_REPLY_MAX);
    int size = bytes ? browser_read(w->job, bytes) : -1;
    // A staging reply always has a header; an empty query reply is left to the query parser.
    bool read = w->role == BOAT_PLUGIN_WORKER_QUERY ? size >= 0 : size > 0;
    if (read) {
        rewind(response);
        read = fwrite(bytes, 1, (size_t)size, response) == (size_t)size && !fflush(response);
    }
    free(bytes);
    if (!read)
        return BOAT_PLUGIN_WORKER_UNREADABLE;
    browser_close(w->job);
    w->job = 0;
    return status == 1 ? BOAT_PLUGIN_WORKER_SUCCEEDED : BOAT_PLUGIN_WORKER_FAILED;
}

void boat_plugin_worker_close(BoatPluginWorker *w)
{
    if (!w)
        return;
    // Closing an already finished transport job only drops its entry, so every job leaves the
    // page's bounded job table here.
    if (w->job)
        browser_close(w->job);
    free(w);
}
