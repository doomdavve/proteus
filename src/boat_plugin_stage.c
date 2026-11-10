#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "boat_plugin_stage.h"
#include "boat_plugin_lua.h"
#include "boat_plugin_stage_backend.h"
#include "boat_plugin_wire.h"
#include "boat_scene_definition.h"
#include "boat_world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Common staging: request/reply wire framing, limits, deadlines and revision validity. Launch,
// polling, byte transfer and termination live in the CMake-selected backend.
#define BOAT_PLUGIN_STAGE_RESPONSE_MAX (BOAT_PLUGIN_SOURCE_MAX + 32768u)

// Deliberately fieldwise: network-order unsigned integers and IEEE-754 binary64 values;
// strings have bounded uint32 lengths, with no NUL on the wire.
typedef struct {
    FILE *file;
    bool reading, ok;
} Wire;
static uint64_t integer(Wire *w, uint64_t v, unsigned n)
{
    unsigned char b[8];
    if (w->reading) {
        if (fread(b, 1, n, w->file) != n)
            w->ok = false;
        else {
            v = 0;
            for (unsigned i = 0; i < n; ++i)
                v = (v << 8) | b[i];
        }
    } else {
        for (unsigned i = 0; i < n; ++i)
            b[n - i - 1] = (unsigned char)(v >> (i * 8));
        if (fwrite(b, 1, n, w->file) != n)
            w->ok = false;
    }
    return v;
}
static void string(Wire *w, char *s, size_t capacity)
{
    size_t n = (size_t)integer(w, w->reading ? 0 : strlen(s), 4);
    if (n >= capacity) {
        w->ok = false;
        return;
    }
    if (w->reading) {
        if (fread(s, 1, n, w->file) != n || memchr(s, 0, n))
            w->ok = false;
        s[n] = 0;
    } else if (fwrite(s, 1, n, w->file) != n)
        w->ok = false;
}
static void diagnostic(Wire *w, BoatPluginDiagnostic *d)
{
    d->status = (BoatPluginStatus)integer(w, (unsigned)d->status, 4);
    if ((unsigned)d->status > BOAT_PLUGIN_SCHEMA)
        w->ok = false;
    string(w, d->file, sizeof(d->file));
    d->line = (int)integer(w, (unsigned)d->line, 4);
    string(w, d->field, sizeof(d->field));
    string(w, d->message, sizeof(d->message));
    d->cost.instructions = integer(w, d->cost.instructions, 8);
    d->cost.entry_instructions = integer(w, d->cost.entry_instructions, 8);
    d->cost.callback_instructions = integer(w, d->cost.callback_instructions, 8);
    d->cost.peak_bytes = (size_t)integer(w, d->cost.peak_bytes, 8);
    d->cost.leaked_bytes = (size_t)integer(w, d->cost.leaked_bytes, 8);
    d->cost.fields = (unsigned)integer(w, d->cost.fields, 4);
}
static bool files_wire(Wire *w, BoatPluginFiles *f)
{
    f->manifest_size = (size_t)integer(w, f->manifest_size, 4);
    f->source_size = (size_t)integer(w, f->source_size, 4);
    if (f->manifest_size > BOAT_PLUGIN_MANIFEST_MAX || f->source_size > BOAT_PLUGIN_SOURCE_MAX)
        return false;
    if (w->reading) {
        w->ok &= fread(f->manifest, 1, f->manifest_size, w->file) == f->manifest_size;
        w->ok &= fread(f->source, 1, f->source_size, w->file) == f->source_size;
        w->ok &= fgetc(w->file) == EOF && !ferror(w->file);
    } else {
        w->ok &= fwrite(f->manifest, 1, f->manifest_size, w->file) == f->manifest_size;
        w->ok &= fwrite(f->source, 1, f->source_size, w->file) == f->source_size;
    }
    return w->ok;
}

// PC query request: manifest, source and expression, each with a 32-bit length. The reply is a
// diagnostic followed by the bounded rendered text.
static void query_write(Wire *w, const BoatPluginFiles *f, const char *expression)
{
    integer(w, f->manifest_size, 4);
    integer(w, f->source_size, 4);
    integer(w, strlen(expression), 4);
    w->ok &= fwrite(f->manifest, 1, f->manifest_size, w->file) == f->manifest_size;
    w->ok &= fwrite(f->source, 1, f->source_size, w->file) == f->source_size;
    w->ok &= fwrite(expression, 1, strlen(expression), w->file) == strlen(expression);
}

static bool query_read(Wire *w, BoatPluginFiles *f, char *expression, size_t capacity)
{
    f->manifest_size = (size_t)integer(w, 0, 4);
    f->source_size = (size_t)integer(w, 0, 4);
    size_t expression_size = (size_t)integer(w, 0, 4);
    if (f->manifest_size > BOAT_PLUGIN_MANIFEST_MAX || f->source_size > BOAT_PLUGIN_SOURCE_MAX
        || expression_size >= capacity)
        return false;
    w->ok &= fread(f->manifest, 1, f->manifest_size, w->file) == f->manifest_size;
    w->ok &= fread(f->source, 1, f->source_size, w->file) == f->source_size;
    w->ok &= fread(expression, 1, expression_size, w->file) == expression_size;
    expression[expression_size] = 0;
    w->ok &= fgetc(w->file) == EOF && !ferror(w->file);
    return w->ok;
}

int boat_plugin_query_worker(void)
{
    boat_plugin_worker_binary_stdio();
    BoatPluginFiles *f = calloc(1, sizeof(*f));
    char *expression = calloc(1, BOAT_PLUGIN_QUERY_MAX);
    char *text = calloc(1, BOAT_PLUGIN_QUERY_REPLY_MAX);
    BoatPluginDiagnostic d = { .status = BOAT_PLUGIN_IO };
    Wire in = { stdin, true, true }, out = { stdout, false, true };
    if (!f || !expression || !text || !query_read(&in, f, expression, BOAT_PLUGIN_QUERY_MAX))
        goto done;
    BoatPluginManifest manifest;
    if (boat_plugin_parse_manifest(f->manifest, f->manifest_size, NULL, &manifest, &d)
        != BOAT_PLUGIN_OK)
        goto done;
    boat_plugin_lua_query(f->source, f->source_size, manifest.entry, expression,
        &boat_plugin_default_limits, text, BOAT_PLUGIN_QUERY_REPLY_MAX, &d);
done:
    integer(&out, BOAT_PLUGIN_WIRE_VERSION, 4);
    diagnostic(&out, &d);
    size_t size = d.status == BOAT_PLUGIN_OK ? strlen(text) : 0;
    integer(&out, size, 4);
    if (size)
        out.ok &= fwrite(text, 1, size, stdout) == size;
    if (fflush(stdout))
        out.ok = false;
    free(text);
    free(expression);
    free(f);
    return out.ok && d.status == BOAT_PLUGIN_OK ? 0 : 1;
}

int boat_plugin_stage_worker(void)
{
    boat_plugin_worker_binary_stdio();
    BoatPluginFiles *f = calloc(1, sizeof(*f));
    BoatPluginDecoded *decoded = calloc(1, sizeof(*decoded));
    BoatPluginRevision *revision = calloc(1, sizeof(*revision));
    BoatPluginDiagnostic d = { .status = BOAT_PLUGIN_IO };
    BoatWorldResources resources = { 0 };
    Wire in = { stdin, true, true }, out = { stdout, false, true };
    if (!f || !decoded || !revision || !files_wire(&in, f))
        goto done;
    BoatPluginBundle bundle = boat_plugin_bundle(f);
    BoatPluginManifest m;
    if (boat_plugin_parse_manifest(f->manifest, f->manifest_size, NULL, &m, &d) != BOAT_PLUGIN_OK)
        goto done;
    BoatPluginKind kind = boat_plugin_kind_parse(m.kind);
    if (kind == BOAT_PLUGIN_KIND_NONE) {
        d.status = BOAT_PLUGIN_MANIFEST;
        snprintf(d.file, sizeof(d.file), "plugin.json");
        snprintf(d.field, sizeof(d.field), "kind");
        snprintf(d.message, sizeof(d.message), "is not a supported kind");
        goto done;
    }
    // Rooms carry their own definition as data. Browser solo has no race trial or match.
    if (boat_plugin_worker_traits.solo_kinds_only
        && (kind == BOAT_PLUGIN_KIND_RACE || kind == BOAT_PLUGIN_KIND_MATCH)) {
        d.status = BOAT_PLUGIN_MANIFEST;
        snprintf(d.file, sizeof(d.file), "plugin.json");
        snprintf(d.field, sizeof(d.field), "kind");
        snprintf(d.message, sizeof(d.message),
            "%s packages are room content; join a room instead of importing one", m.kind);
        goto done;
    }
    decoded->kind = kind;
    decoded->manifest_size = f->manifest_size;
    memcpy(decoded->manifest, f->manifest, f->manifest_size);
    decoded->source_size = bundle.source_size;
    memcpy(decoded->source, bundle.source, bundle.source_size);
    BoatPluginStatus status;
    switch (kind) {
    case BOAT_PLUGIN_KIND_VOYAGE:
        status = boat_plugin_lua_voyage(bundle.source, bundle.source_size, m.entry,
            &boat_plugin_default_limits, &decoded->voyage, &d);
        if (status == BOAT_PLUGIN_OK)
            status = boat_plugin_decode_voyage(&m, &decoded->voyage, &revision->voyage, &d);
        break;
    case BOAT_PLUGIN_KIND_RACE:
        status = boat_plugin_lua_race(bundle.source, bundle.source_size, m.entry,
            &boat_plugin_default_limits, &decoded->race, &d);
        if (status == BOAT_PLUGIN_OK)
            status = boat_plugin_decode_race(&m, &decoded->race, &revision->race, &d);
        break;
    case BOAT_PLUGIN_KIND_FREE:
        status = boat_plugin_lua_free(bundle.source, bundle.source_size, m.entry,
            &boat_plugin_default_limits, &decoded->free, &d);
        if (status == BOAT_PLUGIN_OK)
            status = boat_plugin_decode_free(&m, &decoded->free, &revision->free, &d);
        break;
    case BOAT_PLUGIN_KIND_COURSE:
        status = boat_plugin_lua_course(bundle.source, bundle.source_size, m.entry,
            &boat_plugin_default_limits, &decoded->course, &d);
        if (status == BOAT_PLUGIN_OK)
            status = boat_plugin_decode_course(&m, &decoded->course, &revision->course, &d);
        break;
    case BOAT_PLUGIN_KIND_MATCH:
        // A match package takes the authority's --location unless its scene fixes one; the
        // isolated worker has neither, so rooms evaluate it directly on the authority.
        status = BOAT_PLUGIN_MANIFEST;
        d.status = status;
        snprintf(d.file, sizeof(d.file), "%s", "plugin.json");
        snprintf(d.field, sizeof(d.field), "%s", "kind");
        snprintf(d.message, sizeof(d.message), "%s",
            "match packages are evaluated by the room authority, not by the worker");
        break;
    default:
        status = BOAT_PLUGIN_MANIFEST;
        break;
    }
    if (status != BOAT_PLUGIN_OK)
        goto done;
    revision->manifest = m;
    if (!boat_world_resources_init(&resources, boat_location_find(m.location))) {
        d.status = BOAT_PLUGIN_MEMORY;
        goto done;
    }
    switch (kind) {
    case BOAT_PLUGIN_KIND_VOYAGE:
        status = boat_plugin_check_location(
            &revision->voyage, resources.definition, &resources.map, &d);
        break;
    case BOAT_PLUGIN_KIND_RACE:
        status = boat_plugin_race_check_location(
            &revision->race, resources.definition, &resources.map, &d);
        break;
    case BOAT_PLUGIN_KIND_FREE:
        status = boat_plugin_free_check_location(
            &revision->free, resources.definition, &resources.map, &d);
        break;
    case BOAT_PLUGIN_KIND_COURSE:
        status = boat_plugin_course_check_location(
            &revision->course, resources.definition, &resources.map, &d);
        break;
    default:
        status = BOAT_PLUGIN_MANIFEST;
        break;
    }
    if (status != BOAT_PLUGIN_OK)
        goto done;
    if ((kind == BOAT_PLUGIN_KIND_VOYAGE || kind == BOAT_PLUGIN_KIND_FREE) && m.objective) {
        BoatPluginCost cost = d.cost;
        BoatPluginObjective probe = { .generation = 1,
            .rng = 12345,
            .timers_enabled = m.timers,
            .work_enabled = kind == BOAT_PLUGIN_KIND_FREE,
            .state_version = m.state_version };
        boat_plugin_objective_step(&probe, bundle.source, bundle.source_size,
            BOAT_PLUGIN_OBJECTIVE_INIT, 0, NULL, 0, NULL, &d);
        d.cost.instructions += cost.instructions;
        d.cost.entry_instructions += cost.entry_instructions;
        d.cost.fields += cost.fields;
        if (cost.peak_bytes > d.cost.peak_bytes)
            d.cost.peak_bytes = cost.peak_bytes;
    }
done:
    integer(&out, BOAT_PLUGIN_WIRE_VERSION, 4);
    diagnostic(&out, &d);
    if (d.status == BOAT_PLUGIN_OK) {
        size_t size = boat_plugin_wire_size(decoded);
        unsigned char *bytes = size && size <= BOAT_PLUGIN_STAGE_RESPONSE_MAX ? malloc(size) : NULL;
        size_t written = 0;
        if (!bytes || !boat_plugin_wire_encode(decoded, bytes, size, &written) || written != size
            || fwrite(bytes, 1, size, stdout) != size) {
            d.status = BOAT_PLUGIN_LIMIT;
            d = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_LIMIT };
            snprintf(d.message, sizeof(d.message), "encoded definition exceeds the response limit");
            out.ok = false;
        }
        free(bytes);
    }
    if (fflush(stdout))
        out.ok = false;
    boat_world_resources_destroy(&resources);
    free(revision);
    free(decoded);
    free(f);
    return out.ok && d.status == BOAT_PLUGIN_OK ? 0 : 1;
}

struct BoatPluginStageJob {
    BoatPluginFiles files;
    char expression[BOAT_PLUGIN_QUERY_MAX];
    char query_text[BOAT_PLUGIN_QUERY_REPLY_MAX];
    FILE *input, *output;
    BoatPluginWorker *worker;
    uint64_t epoch;
    unsigned base;
    double deadline;
    bool finished, query;
};

_Static_assert(sizeof(BoatPluginStageJob) <= (80u << 10), "staging request exceeds 80 KiB");

size_t boat_plugin_stage_bytes(const BoatPluginStageJob *job) { return job ? sizeof(*job) : 0; }

static int failure(BoatPluginStageJob *j, BoatPluginHost *host, const char *reason)
{
    if (host->depth && host->epoch == j->epoch && boat_plugin_host_active(host)->number == j->base)
        boat_plugin_host_discard(host);
    host->diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_IO };
    snprintf(host->diagnostic.message, sizeof(host->diagnostic.message), "%s", reason);
    j->finished = true;
    return -1;
}

// Hand the request already written to J's input to the backend. Query and staging differ only
// in the request bytes, the worker role and the browser pin.
static BoatPluginStageJob *launch(
    BoatPluginStageJob *j, const char *executable, BoatPluginWorkerRole role)
{
    long end = ftell(j->input);
    if (end < 0)
        goto failed;
    rewind(j->input);
    const unsigned pin[3] = { j->base, (unsigned)j->epoch, (unsigned)(j->epoch >> 32) };
    j->worker = boat_plugin_worker_start(executable, role, j->input, (size_t)end, j->output, pin);
    if (!j->worker)
        goto failed;
    return j;
failed:
    boat_plugin_stage_close(j);
    return NULL;
}

BoatPluginStageJob *boat_plugin_stage_start(
    const char *executable, const BoatPluginHost *host, const BoatPluginFiles *files, double now)
{
    if (!isfinite(now) || (boat_plugin_worker_traits.needs_executable && !executable)
        || (!boat_plugin_worker_traits.launch_receipt && !host->depth))
        return NULL;
    BoatPluginStageJob *j = calloc(1, sizeof(*j));
    if (!j)
        return NULL;
    j->files = *files;
    j->epoch = host->epoch;
    j->base = host->depth ? boat_plugin_host_active(host)->number : 0;
    j->deadline = now + boat_plugin_worker_traits.deadline;
    j->input = tmpfile();
    j->output = tmpfile();
    if (!j->input || !j->output)
        goto failed;
    Wire in = { j->input, false, true };
    if (!files_wire(&in, &j->files) || fflush(j->input))
        goto failed;
    return launch(j, executable, BOAT_PLUGIN_WORKER_STAGE);
failed:
    boat_plugin_stage_close(j);
    return NULL;
}

int boat_plugin_stage_poll(BoatPluginStageJob *j, BoatPluginHost *host, double now)
{
    if (!j || j->finished)
        return -1;
    if (!isfinite(now) || now >= j->deadline)
        return failure(j, host, "isolated staging deadline");
    if (host->epoch != j->epoch
        || (host->depth ? boat_plugin_host_active(host)->number : 0) != j->base)
        return failure(j, host, "stale staging base revision");
    const char *reason = "worker poll failed";
    BoatPluginWorkerState state = boat_plugin_worker_poll(j->worker, j->output, &reason);
    if (state == BOAT_PLUGIN_WORKER_RUNNING)
        return 0;
    if (state != BOAT_PLUGIN_WORKER_SUCCEEDED && state != BOAT_PLUGIN_WORKER_FAILED)
        return failure(j, host, reason);
    bool success = state == BOAT_PLUGIN_WORKER_SUCCEEDED;
    j->finished = true;
    // Fixed maximum response even if a broken worker writes more.
    if (fseek(j->output, 0, SEEK_END) || ftell(j->output) > (long)BOAT_PLUGIN_STAGE_RESPONSE_MAX)
        return failure(j, host, "worker output exceeds limit");
    long end = ftell(j->output);
    rewind(j->output);
    Wire out = { j->output, true, true };
    if (integer(&out, 0, 4) != BOAT_PLUGIN_WIRE_VERSION)
        out.ok = false;
    BoatPluginDiagnostic d = { 0 };
    diagnostic(&out, &d);
    long at = ftell(j->output);
    size_t wire_size = at >= 0 && end >= at ? (size_t)(end - at) : 0;
    unsigned char *wire
        = wire_size && wire_size <= BOAT_PLUGIN_STAGE_RESPONSE_MAX ? malloc(wire_size) : NULL;
    if (wire && fread(wire, 1, wire_size, j->output) != wire_size)
        wire_size = 0;
    if (!out.ok || fgetc(j->output) != EOF || ferror(j->output)
        || (d.status == BOAT_PLUGIN_OK) != success) {
        free(wire);
        return failure(j, host, "invalid isolated worker response");
    }
    host->diagnostic = d;
    if (!success) {
        free(wire);
        boat_plugin_host_discard(host);
        return -1;
    }
    BoatPluginDecoded *decoded = calloc(1, sizeof(*decoded));
    BoatPluginRevision *r = calloc(1, sizeof(*r));
    bool ok = wire && decoded && r && boat_plugin_wire_decode(wire, wire_size, decoded, &d)
        && boat_plugin_decoded_revision(decoded, r, &d);
    free(wire);
    if (ok && decoded->kind != host->kind)
        ok = false;
    if (ok) {
        switch (decoded->kind) {
        case BOAT_PLUGIN_KIND_VOYAGE:
            ok = boat_plugin_check_location(&r->voyage, host->location, host->map, &d)
                == BOAT_PLUGIN_OK;
            break;
        case BOAT_PLUGIN_KIND_RACE:
            ok = boat_plugin_race_check_location(&r->race, host->location, host->map, &d)
                == BOAT_PLUGIN_OK;
            break;
        case BOAT_PLUGIN_KIND_FREE:
            ok = boat_plugin_free_check_location(&r->free, host->location, host->map, &d)
                == BOAT_PLUGIN_OK;
            break;
        case BOAT_PLUGIN_KIND_COURSE:
            ok = boat_plugin_course_check_location(&r->course, host->location, host->map, &d)
                == BOAT_PLUGIN_OK;
            break;
        case BOAT_PLUGIN_KIND_MATCH:
            ok = boat_plugin_match_check_location(&r->match, host->location, host->map, &d)
                == BOAT_PLUGIN_OK;
            break;
        default:
            ok = false;
            break;
        }
    }
    // A receipt belongs only to this request/host epoch; the public host never accepts a digest
    // or caller-supplied revision as evidence. No Lua runs in this completion path.
    if (ok && !host->depth && !j->base && !j->epoch) {
        // Browser launch consumes a prepared worker receipt; there is no launch-time Lua VM.
        ok = boat_plugin_host_start_decoded(host, host->location, host->map, r);
    } else if (ok)
        ok = boat_plugin_host_stage_decoded(host, r, j->base, j->epoch);
    else {
        boat_plugin_host_discard(host);
        host->diagnostic = d;
    }
    free(decoded);
    free(r);
    return ok ? 1 : -1;
}

BoatPluginStageJob *boat_plugin_query_start(const char *executable, const char *manifest,
    size_t manifest_size, const char *source, size_t source_size, const char *expression,
    double now)
{
    if ((boat_plugin_worker_traits.needs_executable && !executable) || !manifest || !source
        || !expression || !isfinite(now) || manifest_size > BOAT_PLUGIN_MANIFEST_MAX
        || source_size > BOAT_PLUGIN_SOURCE_MAX || strlen(expression) >= BOAT_PLUGIN_QUERY_MAX)
        return NULL;
    BoatPluginStageJob *j = calloc(1, sizeof(*j));
    if (!j)
        return NULL;
    j->query = true;
    j->files.manifest_size = manifest_size;
    j->files.source_size = source_size;
    memcpy(j->files.manifest, manifest, manifest_size);
    memcpy(j->files.source, source, source_size);
    snprintf(j->expression, sizeof(j->expression), "%s", expression);
    j->deadline = now + boat_plugin_worker_traits.deadline;
    j->input = tmpfile();
    j->output = tmpfile();
    if (!j->input || !j->output)
        goto failed;
    Wire in = { j->input, false, true };
    query_write(&in, &j->files, j->expression);
    if (!in.ok || fflush(j->input))
        goto failed;
    return launch(j, executable, BOAT_PLUGIN_WORKER_QUERY);
failed:
    boat_plugin_stage_close(j);
    return NULL;
}

// Read one completed query reply from OUTPUT. Both the native child and the browser worker
// produce the same wire, so this is the one parser.
static int query_parse(
    FILE *output, bool success, char *text, size_t capacity, BoatPluginDiagnostic *result)
{
    if (fseek(output, 0, SEEK_END) || ftell(output) > (long)BOAT_PLUGIN_QUERY_REPLY_MAX + 8192)
        return -1;
    long end = ftell(output);
    rewind(output);
    Wire out = { output, true, true };
    if (integer(&out, 0, 4) != BOAT_PLUGIN_WIRE_VERSION)
        out.ok = false;
    BoatPluginDiagnostic d = { 0 };
    diagnostic(&out, &d);
    long at = ftell(output);
    size_t size = at >= 0 && end >= at ? (size_t)(end - at) : 0;
    unsigned char *bytes = size ? malloc(size) : NULL;
    if (bytes && fread(bytes, 1, size, output) != size)
        size = 0;
    size_t reported = size >= 4
        ? (size_t)bytes[0] << 24 | (size_t)bytes[1] << 16 | (size_t)bytes[2] << 8 | (size_t)bytes[3]
        : 0;
    if (!out.ok || fgetc(output) != EOF || ferror(output) || (d.status == BOAT_PLUGIN_OK) != success
        || reported + 4 != size || reported >= capacity
        || reported >= BOAT_PLUGIN_QUERY_REPLY_MAX) {
        free(bytes);
        return -1;
    }
    if (reported)
        memcpy(text, bytes + 4, reported);
    text[reported] = 0;
    free(bytes);
    if (result)
        *result = d;
    return success ? 1 : -1;
}

int boat_plugin_query_poll(
    BoatPluginStageJob *j, char *text, size_t capacity, BoatPluginDiagnostic *result, double now)
{
    if (!j || !j->query || !text)
        return -1;
    if (!isfinite(now) || now >= j->deadline) {
        j->finished = true;
        if (result && boat_plugin_worker_traits.query_deadline_message) {
            result->status = BOAT_PLUGIN_IO;
            snprintf(result->message, sizeof(result->message), "worker deadline exceeded");
        }
        return -1;
    }
    const char *reason = NULL;
    BoatPluginWorkerState state = boat_plugin_worker_poll(j->worker, j->output, &reason);
    if (state == BOAT_PLUGIN_WORKER_RUNNING)
        return 0;
    j->finished = true;
    if (state != BOAT_PLUGIN_WORKER_SUCCEEDED && state != BOAT_PLUGIN_WORKER_FAILED)
        return -1;
    return query_parse(j->output, state == BOAT_PLUGIN_WORKER_SUCCEEDED, text, capacity, result);
}

void boat_plugin_stage_close(BoatPluginStageJob *j)
{
    if (!j)
        return;
    // The backend kills/reaps or terminates a live worker before the files it writes are closed.
    boat_plugin_worker_close(j->worker);
    if (j->input)
        fclose(j->input);
    if (j->output)
        fclose(j->output);
    free(j);
}

// Worker backends, selected by platform.
#if defined(__EMSCRIPTEN__)
// Browser staging backend: the page's Module.boatPluginStage transport (web/plugin_author.js)
// runs each request in a disposable Web Worker; the game Wasm only copies bytes in and out.

#include <emscripten.h>
#include <stdlib.h>

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
        if (w->role == BOAT_PLUGIN_WORKER_QUERY)
            w->job = 0; // A failed query's transport job is not closed.
        return BOAT_PLUGIN_WORKER_BROKEN;
    }
    *reason = "invalid browser worker response";
    unsigned char *bytes
        = malloc(w->role == BOAT_PLUGIN_WORKER_QUERY ? BOAT_PLUGIN_QUERY_REPLY_MAX + 8192
                                                     : BROWSER_REPLY_MAX);
    int size = bytes ? browser_read(w->job, bytes) : -1;
    // A staging reply always has a header; an empty query reply is left to the query parser.
    bool read = w->role == BOAT_PLUGIN_WORKER_QUERY ? size >= 0 : size > 0;
    if (read) {
        rewind(response);
        read = fwrite(bytes, 1, (size_t)size, response) == (size_t)size && !fflush(response);
    }
    free(bytes);
    if (!read) {
        if (w->role == BOAT_PLUGIN_WORKER_QUERY)
            w->job = 0; // An unreadable query's transport job is not closed.
        return BOAT_PLUGIN_WORKER_UNREADABLE;
    }
    browser_close(w->job);
    w->job = 0;
    return status == 1 ? BOAT_PLUGIN_WORKER_SUCCEEDED : BOAT_PLUGIN_WORKER_FAILED;
}

void boat_plugin_worker_close(BoatPluginWorker *w)
{
    if (!w)
        return;
    if (w->job)
        browser_close(w->job);
    free(w);
}
#elif defined(_WIN32)
// Windows staging backend: CreateProcessA of the executable with inheritable duplicates of the
// request/response temporary files as stdin/stdout, exit-code polling, and TerminateProcess plus
// a wait on close.

#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <fcntl.h>
#include <io.h>
#include <windows.h>

const BoatPluginWorkerTraits boat_plugin_worker_traits = {
    .deadline = 5.0,
    .needs_executable = true,
};

struct BoatPluginWorker {
    HANDLE process; // NULL once the exit code has been collected.
};

void boat_plugin_worker_binary_stdio(void)
{
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
}

BoatPluginWorker *boat_plugin_worker_start(const char *executable, BoatPluginWorkerRole role,
    FILE *request, size_t request_size, FILE *response, const unsigned pin[3])
{
    (void)request_size;
    (void)pin;
    BoatPluginWorker *w = calloc(1, sizeof(*w));
    if (!w)
        return NULL;
    const char *argument
        = role == BOAT_PLUGIN_WORKER_QUERY ? "--plugin-query-worker" : "--plugin-stage-worker";
    // Inheritable copies only; parent CRT handles stay private.
    HANDLE input = NULL, output = NULL, current = GetCurrentProcess();
    if (!DuplicateHandle(current, (HANDLE)_get_osfhandle(_fileno(request)), current, &input, 0,
            TRUE, DUPLICATE_SAME_ACCESS)
        || !DuplicateHandle(current, (HANDLE)_get_osfhandle(_fileno(response)), current, &output, 0,
            TRUE, DUPLICATE_SAME_ACCESS)) {
        if (input)
            CloseHandle(input);
        goto failed;
    }
    char command[4096];
    int n = snprintf(command, sizeof(command), "\"%s\" %s", executable, argument);
    STARTUPINFOA startup = { .cb = sizeof(startup),
        .dwFlags = STARTF_USESTDHANDLES,
        .hStdInput = input,
        .hStdOutput = output,
        .hStdError = output };
    PROCESS_INFORMATION process = { 0 };
    bool started = n > 0 && (size_t)n < sizeof(command) && !strchr(executable, '"')
        && CreateProcessA(executable, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL,
            &startup, &process);
    CloseHandle(input);
    CloseHandle(output);
    if (!started)
        goto failed;
    CloseHandle(process.hThread);
    w->process = process.hProcess;
    return w;
failed:
    free(w);
    return NULL;
}

BoatPluginWorkerState boat_plugin_worker_poll(
    BoatPluginWorker *w, FILE *response, const char **reason)
{
    (void)response;
    DWORD code;
    if (!GetExitCodeProcess(w->process, &code)) {
        *reason = "worker poll failed";
        return BOAT_PLUGIN_WORKER_BROKEN;
    }
    if (code == STILL_ACTIVE)
        return BOAT_PLUGIN_WORKER_RUNNING;
    CloseHandle(w->process);
    w->process = NULL;
    return code == 0 ? BOAT_PLUGIN_WORKER_SUCCEEDED : BOAT_PLUGIN_WORKER_FAILED;
}

void boat_plugin_worker_close(BoatPluginWorker *w)
{
    if (!w)
        return;
    if (w->process) {
        TerminateProcess(w->process, 1);
        WaitForSingleObject(w->process, INFINITE);
        CloseHandle(w->process);
    }
    free(w);
}
#else
// POSIX staging backend: posix_spawn of the executable with request/response temporary files as
// stdin/stdout, non-blocking waitpid polling, and SIGKILL plus reaping on close.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;

const BoatPluginWorkerTraits boat_plugin_worker_traits = {
    .deadline = 5.0,
    .needs_executable = true,
};

struct BoatPluginWorker {
    pid_t pid;
    bool running; // Spawned and not yet reaped.
};

void boat_plugin_worker_binary_stdio(void) { }

BoatPluginWorker *boat_plugin_worker_start(const char *executable, BoatPluginWorkerRole role,
    FILE *request, size_t request_size, FILE *response, const unsigned pin[3])
{
    (void)request_size;
    (void)pin;
    BoatPluginWorker *w = calloc(1, sizeof(*w));
    if (!w)
        return NULL;
    const char *argument
        = role == BOAT_PLUGIN_WORKER_QUERY ? "--plugin-query-worker" : "--plugin-stage-worker";
    if (fcntl(fileno(request), F_SETFD, FD_CLOEXEC) || fcntl(fileno(response), F_SETFD, FD_CLOEXEC))
        goto failed;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions))
        goto failed;
    int error = posix_spawn_file_actions_adddup2(&actions, fileno(request), STDIN_FILENO);
    error |= posix_spawn_file_actions_adddup2(&actions, fileno(response), STDOUT_FILENO);
    error |= posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    char *const argv[] = { (char *)executable, (char *)argument, NULL };
    if (!error)
        error = posix_spawn(&w->pid, executable, &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error)
        goto failed;
    w->running = true;
    return w;
failed:
    free(w);
    return NULL;
}

BoatPluginWorkerState boat_plugin_worker_poll(
    BoatPluginWorker *w, FILE *response, const char **reason)
{
    (void)response;
    int status;
    pid_t got = waitpid(w->pid, &status, WNOHANG);
    if (got == 0)
        return BOAT_PLUGIN_WORKER_RUNNING;
    if (got < 0) {
        *reason = "worker poll failed";
        return BOAT_PLUGIN_WORKER_BROKEN;
    }
    w->running = false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? BOAT_PLUGIN_WORKER_SUCCEEDED
                                                         : BOAT_PLUGIN_WORKER_FAILED;
}

void boat_plugin_worker_close(BoatPluginWorker *w)
{
    if (!w)
        return;
    if (w->running) {
        kill(w->pid, SIGKILL);
        while (waitpid(w->pid, NULL, 0) < 0 && errno == EINTR) { }
    }
    free(w);
}
#endif
