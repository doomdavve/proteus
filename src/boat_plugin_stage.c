#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "boat_plugin_stage.h"
#include "boat_plugin_lua.h"
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
static double number(Wire *w, double v)
{
    uint64_t bits;
    _Static_assert(sizeof(v) == sizeof(bits), "wire requires binary64 doubles");
    memcpy(&bits, &v, sizeof(bits));
    bits = integer(w, bits, 8);
    memcpy(&v, &bits, sizeof(v));
    if (!isfinite(v))
        w->ok = false;
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
// L3.2: bounded travelling-craft routes. Counts are checked before any array access and the
// definition is revalidated after decoding, so a malformed worker cannot install a partial list.
static void traffic_wire(Wire *w, WaterfrontRouteDefinition *routes)
{
    routes->actor_count = (unsigned)integer(w, routes->actor_count, 4);
    if (routes->actor_count > WATERFRONT_ROUTE_ACTORS) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < routes->actor_count; ++i) {
        WaterfrontRouteActor *a = &routes->actors[i];
        string(w, a->id, sizeof(a->id));
        a->shape = (unsigned char)integer(w, a->shape, 1);
        a->recipe = (unsigned char)integer(w, a->recipe, 1);
        a->retain = (unsigned char)integer(w, a->retain, 1);
        if (a->retain != 0xff && a->retain > WATERFRONT_ROUTE_ACTORS)
            w->ok = false;
        unsigned bridge = (unsigned)integer(w, a->bridge ? 1u : 0u, 1);
        if (bridge > 1 || a->shape >= WATERFRONT_ROUTE_SHAPES
            || a->recipe >= WATERFRONT_CRAFT_RECIPES)
            w->ok = false;
        a->bridge = bridge != 0;
        a->speed = (float)number(w, a->speed);
        a->accel = (float)number(w, a->accel);
        a->phase = (float)number(w, a->phase);
        a->period = number(w, a->period);
        a->point_count = (unsigned)integer(w, a->point_count, 4);
        a->stop_count = (unsigned)integer(w, a->stop_count, 4);
        if (a->point_count > WATERFRONT_ROUTE_POINT_CAPACITY
            || a->stop_count > WATERFRONT_ROUTE_STOPS) {
            w->ok = false;
            return;
        }
        for (unsigned p = 0; p < a->point_count; ++p) {
            a->points[p].x = (float)number(w, a->points[p].x);
            a->points[p].z = (float)number(w, a->points[p].z);
            a->points[p].radius = (float)number(w, a->points[p].radius);
        }
        for (unsigned k = 0; k < a->stop_count; ++k) {
            a->stops[k].segment = (unsigned char)integer(w, a->stops[k].segment, 1);
            a->stops[k].dwell = (float)number(w, a->stops[k].dwell);
        }
        a->origin_x = (float)number(w, a->origin_x);
        a->origin_z = (float)number(w, a->origin_z);
        a->long_radius = (float)number(w, a->long_radius);
        a->short_radius = (float)number(w, a->short_radius);
        a->start_distance = (float)number(w, a->start_distance);
        a->turn_radius = (float)number(w, a->turn_radius);
    }
}

// The full pointer-free scene definition, so an isolated worker can return the same identity
// the in-process decoder produces. Counts are checked before any array access.
static void scene_wire(Wire *w, BoatSceneDefinition *s)
{
    s->version = (unsigned)integer(w, s->version, 4);
    unsigned flags = (unsigned)integer(w,
        (s->entrance_present ? 1u : 0u) | (s->berths_present ? 2u : 0u)
            | (s->modules_present ? 4u : 0u) | (s->traffic_present ? 8u : 0u),
        1);
    if (flags > 15 || s->version > BOAT_SCENE_VERSION) {
        w->ok = false;
        return;
    }
    s->entrance_present = flags & 1u;
    s->berths_present = flags & 2u;
    s->modules_present = flags & 4u;
    s->traffic_present = flags & 8u;
    if (s->entrance_present) {
        s->entrance_x = (float)number(w, s->entrance_x);
        s->entrance_z = (float)number(w, s->entrance_z);
        s->entrance_width = (float)number(w, s->entrance_width);
    }
    s->berth_count = (unsigned)integer(w, s->berth_count, 4);
    s->module_count = (unsigned)integer(w, s->module_count, 4);
    if (s->berth_count > BOAT_SCENE_BERTHS || s->module_count > BOAT_SCENE_MODULES) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < s->berth_count; ++i) {
        BoatSceneBerth *b = &s->berths[i];
        string(w, b->id, sizeof(b->id));
        b->recipe = (unsigned char)integer(w, b->recipe, 1);
        b->mooring = (unsigned char)integer(w, b->mooring, 1);
        if (b->recipe >= BOAT_SCENE_BERTH_RECIPES || b->mooring > BOAT_SCENE_MOORING_PONTOON)
            w->ok = false;
        b->x = (float)number(w, b->x);
        b->z = (float)number(w, b->z);
        b->heading = (float)number(w, b->heading);
    }
    for (unsigned i = 0; i < s->module_count; ++i) {
        BoatSceneModule *m = &s->modules[i];
        string(w, m->id, sizeof(m->id));
        m->kind = (unsigned char)integer(w, m->kind, 1);
        if (m->kind >= BOAT_SCENE_MODULE_KINDS)
            w->ok = false;
        m->x = (float)number(w, m->x);
        m->z = (float)number(w, m->z);
        m->heading = (float)number(w, m->heading);
        m->anchor_x = (float)number(w, m->anchor_x);
        m->anchor_z = (float)number(w, m->anchor_z);
        string(w, m->attaches, sizeof(m->attaches));
    }
    if (s->traffic_present)
        traffic_wire(w, &s->traffic);
}

static void data_wire(Wire *w, BoatPluginVoyageData *v)
{
    unsigned callbacks = (unsigned)integer(w, v->callbacks, 1);
    unsigned migrates = (unsigned)integer(w, v->migrates, 1);
    if (callbacks > 1 || migrates > 1)
        w->ok = false;
    v->callbacks = callbacks != 0;
    v->migrates = migrates != 0;
    unsigned has_scene = (unsigned)integer(w, v->has_scene, 1);
    if (has_scene > 1)
        w->ok = false;
    v->has_scene = has_scene != 0;
    if (v->has_scene)
        scene_wire(w, &v->scene);
    v->shelter_count = (unsigned)integer(w, v->shelter_count, 4);
    if (v->shelter_count < 2 || v->shelter_count > BOAT_VOYAGE_SHELTERS) {
        w->ok = false;
        return;
    }
    for (unsigned i = 0; i < v->shelter_count; ++i) {
        BoatPluginShelterData *s = &v->shelters[i];
        string(w, s->id, sizeof(s->id));
        string(w, s->name, sizeof(s->name));
        s->x = number(w, s->x);
        s->z = number(w, s->z);
        s->heading = number(w, s->heading);
        s->radius = number(w, s->radius);
    }
    v->arrival.x = number(w, v->arrival.x);
    v->arrival.z = number(w, v->arrival.z);
    v->arrival.radius = number(w, v->arrival.radius);
    v->arrival.max_speed = number(w, v->arrival.max_speed);
    v->exposure.x = number(w, v->exposure.x);
    v->exposure.z = number(w, v->exposure.z);
    v->exposure.radius = number(w, v->exposure.radius);
    v->exposure.height = number(w, v->exposure.height);
    v->exposure.build = number(w, v->exposure.build);
    v->exposed_fraction = number(w, v->exposed_fraction);
    v->weather_range = number(w, v->weather_range);
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

int boat_plugin_stage_worker(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    BoatPluginFiles *f = calloc(1, sizeof(*f));
    BoatPluginVoyageData data = { 0 };
    BoatPluginVoyage voyage;
    BoatPluginDiagnostic d = { .status = BOAT_PLUGIN_IO };
    BoatWorldResources resources = { 0 };
    Wire in = { stdin, true, true }, out = { stdout, false, true };
    if (!f || !files_wire(&in, f))
        goto done;
    BoatPluginBundle bundle = boat_plugin_bundle(f);
    BoatPluginManifest m;
    if (boat_plugin_parse_manifest(f->manifest, f->manifest_size, NULL, &m, &d) != BOAT_PLUGIN_OK)
        goto done;
    if (strcmp(m.kind, "voyage")) {
        d.status = BOAT_PLUGIN_MANIFEST;
        snprintf(d.file, sizeof(d.file), "plugin.json");
        snprintf(d.field, sizeof(d.field), "kind");
        snprintf(d.message, sizeof(d.message), "this host supports voyage descriptors only");
        goto done;
    }
#ifdef __EMSCRIPTEN__
    if (m.scene) {
        d.status = BOAT_PLUGIN_MANIFEST;
        snprintf(d.file, sizeof(d.file), "plugin.json");
        snprintf(d.field, sizeof(d.field), "capabilities[2]");
        snprintf(d.message, sizeof(d.message),
            "scene.define is native-only; browser scene imports are unsupported");
        goto done;
    }
#endif
    if (boat_plugin_lua_voyage(
            bundle.source, bundle.source_size, m.entry, &boat_plugin_default_limits, &data, &d)
            != BOAT_PLUGIN_OK
        || boat_plugin_decode_voyage(&m, &data, &voyage, &d) != BOAT_PLUGIN_OK)
        goto done;
    if (!boat_world_resources_init(&resources, boat_location_find(m.location))) {
        d.status = BOAT_PLUGIN_MEMORY;
        goto done;
    }
    if (boat_plugin_check_location(&voyage, resources.definition, &resources.map, &d)
        != BOAT_PLUGIN_OK)
        goto done;
    if (m.objective) {
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
    integer(&out, 0x50535434, 4); // PST4: traffic routes extend PST3; other versions are refused.
    diagnostic(&out, &d);
    if (d.status == BOAT_PLUGIN_OK)
        data_wire(&out, &data);
    if (fflush(stdout))
        out.ok = false;
    boat_world_resources_destroy(&resources);
    free(f);
    return out.ok && d.status == BOAT_PLUGIN_OK ? 0 : 1;
}

struct BoatPluginStageJob {
    BoatPluginFiles files;
    FILE *input, *output;
    uint64_t epoch;
    unsigned base;
    double deadline;
    bool running, finished;
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
    if (!bytes || bytes.length > 4096)
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
    if (!isfinite(now) || host->racing)
#else
    if (!executable || !isfinite(now) || !host->depth || host->racing)
#endif
        return NULL;
    BoatPluginStageJob *j = calloc(1, sizeof(*j));
    if (!j)
        return NULL;
    j->files = *files;
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
    int n = snprintf(command, sizeof(command), "\"%s\" --plugin-stage-worker", executable);
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
    char *const argv[] = { (char *)executable, "--plugin-stage-worker", NULL };
    if (!error)
        error = posix_spawn(&j->pid, executable, &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error)
        goto failed;
#endif
    j->running = true;
    return j;
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
    bool success;
#if defined(__EMSCRIPTEN__)
    int status = browser_status(j->browser_job);
    if (!status)
        return 0;
    if (status < 0)
        return failure(j, host, "browser worker failed, canceled or stale");
    unsigned char bytes[32768];
    int size = browser_read(j->browser_job, bytes);
    if (size <= 0 || fwrite(bytes, 1, (size_t)size, j->output) != (size_t)size)
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
    if (fseek(j->output, 0, SEEK_END) || ftell(j->output) > 32768)
        return failure(j, host, "worker output exceeds limit");
    rewind(j->output);
    Wire out = { j->output, true, true };
    if (integer(&out, 0, 4) != 0x50535434)
        out.ok = false;
    BoatPluginDiagnostic d = { 0 };
    diagnostic(&out, &d);
    BoatPluginVoyageData data = { 0 };
    if (d.status == BOAT_PLUGIN_OK && success)
        data_wire(&out, &data);
    if (!out.ok || fgetc(j->output) != EOF || ferror(j->output)
        || (d.status == BOAT_PLUGIN_OK) != success)
        return failure(j, host, "invalid isolated worker response");
    host->diagnostic = d;
    if (!success) {
        boat_plugin_host_discard(host);
        return -1;
    }
    BoatPluginManifest m;
    BoatPluginRevision *r = calloc(1, sizeof(*r));
    if (!r)
        return failure(j, host, "cannot allocate staged revision");
    bool ok = boat_plugin_parse_manifest(j->files.manifest, j->files.manifest_size, NULL, &m, &d)
            == BOAT_PLUGIN_OK
        && !strcmp(m.kind, "voyage")
        && boat_plugin_decode_voyage(&m, &data, &r->voyage, &d) == BOAT_PLUGIN_OK
        && boat_plugin_check_location(&r->voyage, host->location, host->map, &d) == BOAT_PLUGIN_OK;
    if (ok && m.objective) {
        r->source_size = j->files.source_size;
        memcpy(r->source, j->files.source, r->source_size);
    }
    // A receipt belongs only to this request/host epoch; the public host never accepts a digest
    // or caller-supplied revision as evidence. No Lua runs in this completion path.
    if (ok && !host->depth && !j->base && !j->epoch) {
        // Browser launch consumes a prepared worker receipt; there is no launch-time Lua VM.
        boat_plugin_voyage_copy(&host->slots[0].voyage, &r->voyage);
        host->slots[0].source_size = r->source_size;
        memcpy(host->slots[0].source, r->source, r->source_size);
        host->slots[0].number = host->numbered = host->depth = 1;
    } else if (ok)
        ok = boat_plugin_host_stage_decoded(host, r, j->base, j->epoch);
    else {
        boat_plugin_host_discard(host);
        host->diagnostic = d;
    }
    free(r);
    return ok ? 1 : -1;
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
