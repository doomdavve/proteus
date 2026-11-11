// Windows staging backend: CreateProcessA of the executable with inheritable duplicates of the
// request/response temporary files as stdin/stdout, exit-code polling, and TerminateProcess plus
// a wait on close.
#include "plugins/boat_plugin_stage_backend.h"

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
