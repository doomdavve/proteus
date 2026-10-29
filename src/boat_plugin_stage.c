#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "boat_plugin_stage.h"
#include "boat_plugin_lua.h"
#include "boat_world.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
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
static void data_wire(Wire *w, BoatPluginVoyageData *v)
{
    unsigned callbacks = (unsigned)integer(w, v->callbacks, 1);
    unsigned migrates = (unsigned)integer(w, v->migrates, 1);
    if (callbacks > 1 || migrates > 1)
        w->ok = false;
    v->callbacks = callbacks != 0;
    v->migrates = migrates != 0;
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
        goto done;
    }
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
    integer(&out, 0x50535432, 4); // PST2: never accept another private wire version.
    diagnostic(&out, &d);
    if (d.status == BOAT_PLUGIN_OK)
        data_wire(&out, &data);
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
#ifdef _WIN32
    HANDLE process;
#else
    pid_t pid;
#endif
};

_Static_assert(sizeof(BoatPluginStageJob) <= (80u << 10), "staging request exceeds 80 KiB");

size_t boat_plugin_stage_bytes(const BoatPluginStageJob *job) { return job ? sizeof(*job) : 0; }

static int failure(BoatPluginStageJob *j, BoatPluginHost *host, const char *reason)
{
    if (host->epoch == j->epoch && boat_plugin_host_active(host)->number == j->base)
        boat_plugin_host_discard(host);
    host->diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_IO };
    snprintf(host->diagnostic.message, sizeof(host->diagnostic.message), "%s", reason);
    j->finished = true;
    return -1;
}

BoatPluginStageJob *boat_plugin_stage_start(
    const char *executable, const BoatPluginHost *host, const BoatPluginFiles *files, double now)
{
    if (!executable || !isfinite(now) || !host->depth || host->racing)
        return NULL;
    BoatPluginStageJob *j = calloc(1, sizeof(*j));
    if (!j)
        return NULL;
    j->files = *files;
    j->epoch = host->epoch;
    j->base = boat_plugin_host_active(host)->number;
    j->deadline = now + 5;
    j->input = tmpfile();
    j->output = tmpfile();
    if (!j->input || !j->output)
        goto failed;
    Wire in = { j->input, false, true };
    if (!files_wire(&in, &j->files) || fflush(j->input))
        goto failed;
    rewind(j->input);
#ifdef _WIN32
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
    if (host->epoch != j->epoch || boat_plugin_host_active(host)->number != j->base)
        return failure(j, host, "stale staging base revision");
    bool success;
#ifdef _WIN32
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
    if (fseek(j->output, 0, SEEK_END) || ftell(j->output) > 4096)
        return failure(j, host, "worker output exceeds limit");
    rewind(j->output);
    Wire out = { j->output, true, true };
    if (integer(&out, 0, 4) != 0x50535432)
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
    if (ok)
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
#ifdef _WIN32
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
