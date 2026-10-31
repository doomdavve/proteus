#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "boat_plugin_stage.h"
#include "boat_plugin_lua.h"
#include "boat_plugin_wire.h"
#include "boat_scene_definition.h"
#include "boat_world.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif
#include <stdio.h>

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
#if !defined(__EMSCRIPTEN__)
static void query_write(Wire *w, const BoatPluginFiles *f, const char *expression)
{
    integer(w, f->manifest_size, 4);
    integer(w, f->source_size, 4);
    integer(w, strlen(expression), 4);
    w->ok &= fwrite(f->manifest, 1, f->manifest_size, w->file) == f->manifest_size;
    w->ok &= fwrite(f->source, 1, f->source_size, w->file) == f->source_size;
    w->ok &= fwrite(expression, 1, strlen(expression), w->file) == strlen(expression);
}
#endif // !__EMSCRIPTEN__

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
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
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
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
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
#ifdef __EMSCRIPTEN__
    // Rooms carry their own definition as data. Browser solo has no race trial or match.
    if (kind == BOAT_PLUGIN_KIND_RACE || kind == BOAT_PLUGIN_KIND_MATCH) {
        d.status = BOAT_PLUGIN_MANIFEST;
        snprintf(d.file, sizeof(d.file), "plugin.json");
        snprintf(d.field, sizeof(d.field), "kind");
        snprintf(d.message, sizeof(d.message),
            "%s packages are room content; join a room instead of importing one", m.kind);
        goto done;
    }
#endif
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
        status = boat_plugin_free_check_location(&revision->free, resources.definition, &d);
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
    if (kind == BOAT_PLUGIN_KIND_VOYAGE && m.objective) {
        BoatPluginCost cost = d.cost;
        BoatPluginObjective probe = { .generation = 1,
            .rng = 12345,
            .timers_enabled = m.timers,
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
    const char *worker_argument;
    FILE *input, *output;
    uint64_t epoch;
    unsigned base;
    double deadline;
    bool running, finished, query;
#if defined(__EMSCRIPTEN__)
    unsigned browser_job;
#elif defined(_WIN32)
    HANDLE process;
#else
    pid_t pid;
#endif
};

#ifdef __EMSCRIPTEN__
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
#endif

_Static_assert(sizeof(BoatPluginStageJob) <= (80u << 10), "staging request exceeds 80 KiB");

size_t boat_plugin_stage_bytes(const BoatPluginStageJob *job) { return job ? sizeof(*job) : 0; }

static BoatPluginStageJob *launch(BoatPluginStageJob *j, const char *executable);

static int failure(BoatPluginStageJob *j, BoatPluginHost *host, const char *reason)
{
    if (host->depth && host->epoch == j->epoch && boat_plugin_host_active(host)->number == j->base)
        boat_plugin_host_discard(host);
    host->diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_IO };
    snprintf(host->diagnostic.message, sizeof(host->diagnostic.message), "%s", reason);
    j->finished = true;
    return -1;
}

BoatPluginStageJob *boat_plugin_stage_start(
    const char *executable, const BoatPluginHost *host, const BoatPluginFiles *files, double now)
{
#ifdef __EMSCRIPTEN__
    (void)executable;
    if (!isfinite(now))
#else
    if (!executable || !isfinite(now) || !host->depth)
#endif
        return NULL;
    BoatPluginStageJob *j = calloc(1, sizeof(*j));
    if (!j)
        return NULL;
    j->files = *files;
    j->worker_argument = "--plugin-stage-worker";
    j->epoch = host->epoch;
    j->base = host->depth ? boat_plugin_host_active(host)->number : 0;
    j->deadline = now + 5;
    j->input = tmpfile();
    j->output = tmpfile();
    if (!j->input || !j->output)
        goto failed;
    Wire in = { j->input, false, true };
    if (!files_wire(&in, &j->files) || fflush(j->input))
        goto failed;
    rewind(j->input);
    return launch(j, executable);
failed:
    boat_plugin_stage_close(j);
    return NULL;
}

// Start the chosen worker for a request already written to J's input. Query and staging differ
// only in the request bytes and worker argument.
static BoatPluginStageJob *spawn(BoatPluginStageJob *j, const char *executable);

static BoatPluginStageJob *launch(BoatPluginStageJob *j, const char *executable)
{
#if defined(__EMSCRIPTEN__)
    size_t size = 8 + j->files.manifest_size + j->files.source_size;
    unsigned char *payload = malloc(size);
    if (!payload)
        goto failed;
    bool read = fread(payload, 1, size, j->input) == size;
    if (read)
        j->browser_job = browser_start(
            payload, (unsigned)size, j->base, (unsigned)j->epoch, (unsigned)(j->epoch >> 32));
    free(payload);
    if (!j->browser_job)
        goto failed;
    return spawn(j, executable);
failed:
    boat_plugin_stage_close(j);
    return NULL;
#else
    return spawn(j, executable);
#endif
}

static BoatPluginStageJob *spawn(BoatPluginStageJob *j, const char *executable)
{
#if defined(__EMSCRIPTEN__)
    (void)executable;
    j->running = true;
    return j;
#elif defined(_WIN32)
    // Inheritable copies only; parent CRT handles stay private.
    HANDLE input = NULL, output = NULL, current = GetCurrentProcess();
    if (!DuplicateHandle(current, (HANDLE)_get_osfhandle(_fileno(j->input)), current, &input, 0,
            TRUE, DUPLICATE_SAME_ACCESS)
        || !DuplicateHandle(current, (HANDLE)_get_osfhandle(_fileno(j->output)), current, &output,
            0, TRUE, DUPLICATE_SAME_ACCESS)) {
        if (input)
            CloseHandle(input);
        goto failed;
    }
    char command[4096];
    int n = snprintf(command, sizeof(command), "\"%s\" %s", executable, j->worker_argument);
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
    j->process = process.hProcess;
#else
    if (fcntl(fileno(j->input), F_SETFD, FD_CLOEXEC)
        || fcntl(fileno(j->output), F_SETFD, FD_CLOEXEC))
        goto failed;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions))
        goto failed;
    int error = posix_spawn_file_actions_adddup2(&actions, fileno(j->input), STDIN_FILENO);
    error |= posix_spawn_file_actions_adddup2(&actions, fileno(j->output), STDOUT_FILENO);
    error |= posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    char *const argv[] = { (char *)executable, (char *)j->worker_argument, NULL };
    if (!error)
        error = posix_spawn(&j->pid, executable, &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error)
        goto failed;
#endif
    j->running = true;
    return j;
#if !defined(__EMSCRIPTEN__)
failed:
    boat_plugin_stage_close(j);
    return NULL;
#endif
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
    bool success;
#if defined(__EMSCRIPTEN__)
    int status = browser_status(j->browser_job);
    if (!status)
        return 0;
    if (status < 0)
        return failure(j, host, "browser worker failed, canceled or stale");
    unsigned char *bytes = malloc(163840);
    int size = bytes ? browser_read(j->browser_job, bytes) : -1;
    if (size <= 0 || fwrite(bytes, 1, (size_t)size, j->output) != (size_t)size)
        size = 0;
    free(bytes);
    if (!size)
        return failure(j, host, "invalid browser worker response");
    success = status == 1;
    browser_close(j->browser_job);
    j->browser_job = 0;
#elif defined(_WIN32)
    DWORD code;
    if (!GetExitCodeProcess(j->process, &code))
        return failure(j, host, "worker poll failed");
    if (code == STILL_ACTIVE)
        return 0;
    success = code == 0;
    CloseHandle(j->process);
    j->process = NULL;
#else
    int status;
    pid_t got = waitpid(j->pid, &status, WNOHANG);
    if (got == 0)
        return 0;
    if (got < 0)
        return failure(j, host, "worker poll failed");
    success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
    j->running = false;
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
            ok = boat_plugin_free_check_location(&r->free, host->location, &d) == BOAT_PLUGIN_OK;
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
#if defined(__EMSCRIPTEN__)
    (void)executable;
    (void)manifest;
    (void)manifest_size;
    (void)source;
    (void)source_size;
    (void)expression;
    (void)now;
    return NULL;
#else
    if (!executable || !manifest || !source || !expression || !isfinite(now)
        || manifest_size > BOAT_PLUGIN_MANIFEST_MAX || source_size > BOAT_PLUGIN_SOURCE_MAX
        || strlen(expression) >= BOAT_PLUGIN_QUERY_MAX)
        return NULL;
    BoatPluginStageJob *j = calloc(1, sizeof(*j));
    if (!j)
        return NULL;
    j->query = true;
    j->worker_argument = "--plugin-query-worker";
    j->files.manifest_size = manifest_size;
    j->files.source_size = source_size;
    memcpy(j->files.manifest, manifest, manifest_size);
    memcpy(j->files.source, source, source_size);
    snprintf(j->expression, sizeof(j->expression), "%s", expression);
    j->deadline = now + 5;
    j->input = tmpfile();
    j->output = tmpfile();
    if (!j->input || !j->output)
        goto failed;
    Wire in = { j->input, false, true };
    query_write(&in, &j->files, j->expression);
    if (!in.ok || fflush(j->input))
        goto failed;
    rewind(j->input);
    return spawn(j, executable);
failed:
    boat_plugin_stage_close(j);
    return NULL;
#endif
}

int boat_plugin_query_poll(
    BoatPluginStageJob *j, char *text, size_t capacity, BoatPluginDiagnostic *result, double now)
{
#if defined(__EMSCRIPTEN__)
    (void)j;
    (void)text;
    (void)capacity;
    (void)result;
    (void)now;
    return -1;
#else
    if (!j || !j->query || !text)
        return -1;
    if (!isfinite(now) || now >= j->deadline) {
        j->finished = true;
        return -1;
    }
    bool success;
#if defined(_WIN32)
    DWORD code;
    if (!GetExitCodeProcess(j->process, &code))
        return -1;
    if (code == STILL_ACTIVE)
        return 0;
    success = code == 0;
    CloseHandle(j->process);
    j->process = NULL;
#else
    int status;
    pid_t got = waitpid(j->pid, &status, WNOHANG);
    if (got == 0)
        return 0;
    if (got < 0)
        return -1;
    success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
    j->running = false;
    j->finished = true;
    if (fseek(j->output, 0, SEEK_END)
        || ftell(j->output) > (long)BOAT_PLUGIN_QUERY_REPLY_MAX + 8192)
        return -1;
    long end = ftell(j->output);
    rewind(j->output);
    Wire out = { j->output, true, true };
    if (integer(&out, 0, 4) != BOAT_PLUGIN_WIRE_VERSION)
        out.ok = false;
    BoatPluginDiagnostic d = { 0 };
    diagnostic(&out, &d);
    long at = ftell(j->output);
    size_t size = at >= 0 && end >= at ? (size_t)(end - at) : 0;
    unsigned char *bytes = size ? malloc(size) : NULL;
    if (bytes && fread(bytes, 1, size, j->output) != size)
        size = 0;
    size_t reported = size >= 4
        ? (size_t)bytes[0] << 24 | (size_t)bytes[1] << 16 | (size_t)bytes[2] << 8 | (size_t)bytes[3]
        : 0;
    if (!out.ok || fgetc(j->output) != EOF || ferror(j->output)
        || (d.status == BOAT_PLUGIN_OK) != success || reported + 4 != size || reported >= capacity
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
#endif
}

void boat_plugin_stage_close(BoatPluginStageJob *j)
{
    if (!j)
        return;
    if (j->running) {
#if defined(__EMSCRIPTEN__)
        browser_close(j->browser_job);
#elif defined(_WIN32)
        TerminateProcess(j->process, 1);
        WaitForSingleObject(j->process, INFINITE);
        CloseHandle(j->process);
#else
        kill(j->pid, SIGKILL);
        while (waitpid(j->pid, NULL, 0) < 0 && errno == EINTR) { }
#endif
    }
    if (j->input)
        fclose(j->input);
    if (j->output)
        fclose(j->output);
    free(j);
}
