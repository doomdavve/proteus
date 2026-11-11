// Browser staging paths without a browser: the real boat_plugin_stage.c and the real
// boat_plugin_stage_web.c, compiled natively, against a C model of the page's Transport
// (web/plugin_author.js). Workers run in process through the private stream entry points, and
// every deadline uses supplied virtual time.
#include "core/boat_world.h"
#include "plugins/boat_plugin_io.h"
#include "plugins/boat_plugin_stage.h"
#include "plugins/boat_plugin_stage_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// --- Model of Transport: at most two live jobs; status 0 running, 1/2 replied, -1 failed. ---

enum { MODEL_RUN, MODEL_HANG, MODEL_FAIL, MODEL_REPLY };
enum { MODEL_JOBS = 64, MODEL_REQUEST_MAX = 131072, MODEL_RESULT_LIMIT = 163840 };

typedef struct {
    bool live;
    int state;
    unsigned char *response;
    size_t size;
} ModelJob;

static ModelJob model_jobs[MODEL_JOBS];
static unsigned model_serial;
static int model_mode = MODEL_RUN;
static size_t model_reply_size; // MODEL_REPLY: bytes of 'x' the page hands back.

static unsigned model_live(void)
{
    unsigned live = 0;
    for (unsigned i = 0; i < MODEL_JOBS; ++i)
        live += model_jobs[i].live;
    return live;
}

static void model_drop(ModelJob *job)
{
    free(job->response);
    *job = (ModelJob) { 0 };
}

// Run the worker the page would run, in process, and keep its reply as the page does.
static void model_run(ModelJob *job, const unsigned char *data, unsigned size, bool query)
{
    FILE *in = tmpfile(), *out = tmpfile();
    job->state = -1;
    if (in && out && fwrite(data, 1, size, in) == size && !fflush(in)) {
        rewind(in);
        int code = query ? boat_plugin_query_worker_streams(in, out)
                         : boat_plugin_stage_worker_streams(in, out);
        long end = ftell(out);
        if (end >= 8 && end <= MODEL_RESULT_LIMIT && (job->response = malloc((size_t)end))) {
            rewind(out);
            job->size = fread(job->response, 1, (size_t)end, out);
            job->state = job->size == (size_t)end ? (code == 0 ? 1 : 2) : -1;
        }
    }
    if (in)
        fclose(in);
    if (out)
        fclose(out);
}

static unsigned model_start(const unsigned char *data, unsigned size, bool query)
{
    if (size > MODEL_REQUEST_MAX || model_live() >= 2 || model_serial + 1 >= MODEL_JOBS)
        return 0;
    unsigned id = ++model_serial;
    ModelJob *job = &model_jobs[id];
    job->live = true;
    if (model_mode == MODEL_RUN)
        model_run(job, data, size, query);
    else if (model_mode == MODEL_FAIL)
        job->state = -1;
    else if (model_mode == MODEL_REPLY) {
        job->response = malloc(model_reply_size);
        if (job->response)
            memset(job->response, 'x', model_reply_size);
        job->size = model_reply_size;
        job->state = job->response ? 1 : -1;
    }
    return id;
}

unsigned browser_start(
    const unsigned char *data, unsigned size, unsigned base, unsigned low, unsigned high)
{
    (void)base;
    (void)low;
    (void)high;
    return model_start(data, size, false);
}

unsigned browser_query_start(const unsigned char *data, unsigned size)
{
    return model_start(data, size, true);
}

int browser_status(unsigned id)
{
    return id < MODEL_JOBS && model_jobs[id].live ? model_jobs[id].state : -1;
}

int browser_read(unsigned id, unsigned char *data)
{
    const ModelJob *job = id < MODEL_JOBS ? &model_jobs[id] : NULL;
    if (!job || !job->live || !job->response || job->size > 163840)
        return -1;
    memcpy(data, job->response, job->size);
    return (int)job->size;
}

void browser_close(unsigned id)
{
    if (id < MODEL_JOBS)
        model_drop(&model_jobs[id]);
}

// --- Cases ---

static int answered(BoatPluginStageJob *job, char *text, size_t capacity, BoatPluginDiagnostic *d)
{
    int result;
    while (!(result = boat_plugin_query_poll(job, text, capacity, d, .25))) { }
    return result;
}

static int completed(BoatPluginStageJob *job, BoatPluginHost *host)
{
    int result;
    while (!(result = boat_plugin_stage_poll(job, host, .25))) { }
    return result;
}

static BoatPluginStageJob *query(const BoatPluginFiles *files, const char *expression, double now)
{
    return boat_plugin_query_start(NULL, files->manifest, files->manifest_size, files->source,
        files->source_size, expression, now);
}

int main(void)
{
    CHECK(boat_plugin_worker_traits.deadline == 15.0 && !boat_plugin_worker_traits.needs_executable
        && boat_plugin_worker_traits.launch_receipt && boat_plugin_worker_traits.solo_kinds_only
        && boat_plugin_worker_traits.query_deadline_message);
    static BoatPluginFiles files, room;
    static BoatPluginHost host, race_host;
    BoatPluginDiagnostic d;
    CHECK(boat_plugin_read("plugins/shelter-tour", NULL, &files, &d) == BOAT_PLUGIN_OK);
    BoatWorldResources resources;
    CHECK(boat_world_resources_init(&resources, boat_location_find("outer")));

    // Launch receipt: an empty host with no executable takes the worker's revision.
    host.location = resources.definition;
    host.map = &resources.map;
    host.kind = BOAT_PLUGIN_KIND_VOYAGE;
    BoatPluginStageJob *job = boat_plugin_stage_start(NULL, &host, &files, 0);
    CHECK(job && completed(job, &host) == 1);
    CHECK(host.depth == 1 && boat_plugin_host_active(&host)->number == 1);
    boat_plugin_stage_close(job);
    CHECK(model_live() == 0);
    unsigned active = boat_plugin_host_active(&host)->number;

    // Browser solo rejects room-only kinds in the worker, with the rooms message.
    const char *rooms[] = { "plugins/harbor-race", "plugins/deathmatch" };
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(boat_plugin_read(rooms[i], NULL, &room, &d) == BOAT_PLUGIN_OK);
        race_host = (BoatPluginHost) { .location = resources.definition,
            .map = &resources.map,
            .kind = i ? BOAT_PLUGIN_KIND_MATCH : BOAT_PLUGIN_KIND_RACE };
        job = boat_plugin_stage_start(NULL, &race_host, &room, 0);
        CHECK(job && completed(job, &race_host) == -1 && !race_host.depth);
        CHECK(race_host.diagnostic.status == BOAT_PLUGIN_MANIFEST
            && strstr(race_host.diagnostic.message, "join a room"));
        boat_plugin_stage_close(job);
    }
    CHECK(model_live() == 0);

    // A console query answers through the page and leaves no job behind.
    char text[BOAT_PLUGIN_QUERY_REPLY_MAX];
    job = query(&files, "def.voyage.shelters[1].name", 0);
    d = (BoatPluginDiagnostic) { 0 };
    CHECK(job && answered(job, text, sizeof(text), &d) == 1 && text[0]);
    boat_plugin_stage_close(job);
    CHECK(model_live() == 0);

    // Query deadline: running at 14.75 s, rejected with a message at exactly 15 s.
    model_mode = MODEL_HANG;
    job = query(&files, "def.voyage.shelters[1].name", 0);
    CHECK(job && boat_plugin_query_poll(job, text, sizeof(text), &d, 14.75) == 0);
    d = (BoatPluginDiagnostic) { 0 };
    CHECK(boat_plugin_query_poll(job, text, sizeof(text), &d, 15) == -1);
    CHECK(d.status == BOAT_PLUGIN_IO && !strcmp(d.message, "worker deadline exceeded"));
    boat_plugin_stage_close(job);
    CHECK(model_live() == 0);

    // Failed and unreadable queries release their jobs: three in a row, then one still starts.
    for (unsigned i = 0; i < 3; ++i) {
        model_mode = i == 1 ? MODEL_REPLY : MODEL_FAIL;
        model_reply_size = 163841; // Over the bridge limit: browser_read refuses it.
        job = query(&files, "def.voyage.shelters[1].name", 0);
        CHECK(job && answered(job, text, sizeof(text), &d) == -1);
        boat_plugin_stage_close(job);
        CHECK(model_live() == 0);
    }
    // A reply at the 160 KiB bridge limit is copied whole, then rejected by the query parser.
    model_mode = MODEL_REPLY;
    model_reply_size = 163840;
    job = query(&files, "def.voyage.shelters[1].name", 0);
    CHECK(job && answered(job, text, sizeof(text), &d) == -1);
    boat_plugin_stage_close(job);
    // The same reply to staging exceeds the response limit and keeps the active revision.
    job = boat_plugin_stage_start(NULL, &host, &files, 0);
    CHECK(job && completed(job, &host) == -1);
    CHECK(strstr(host.diagnostic.message, "exceeds limit"));
    CHECK(boat_plugin_host_active(&host)->number == active);
    boat_plugin_stage_close(job);
    CHECK(model_live() == 0);
    model_mode = MODEL_RUN;
    job = query(&files, "def.voyage.shelters[1].name", 0);
    CHECK(job && answered(job, text, sizeof(text), &d) == 1 && text[0]);
    boat_plugin_stage_close(job);
    CHECK(model_live() == 0);

    boat_world_resources_destroy(&resources);
    puts("browser staging: launch receipt, room kinds, query deadline message, job release, "
         "bridge-sized replies OK");
    return 0;
}
