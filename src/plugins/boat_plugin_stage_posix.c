#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
// POSIX staging backend: posix_spawn of the executable with request/response temporary files as
// stdin/stdout, non-blocking waitpid polling, and SIGKILL plus reaping on close.
#include "boat_plugin_stage_backend.h"

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
