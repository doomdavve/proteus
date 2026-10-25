#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L // posix_spawn, pipes and poll under strict C17.
#endif
#include "wake_velocity_host.h"

#ifdef _WIN32
// Exclude the GDI/user names raylib also declares (headers below may include raylib.h).
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char **environ;
#endif

#include <stdio.h>
#include <string.h>

enum { READY_MS = 5000, GOODBYE_MS = 2000, WAIT_MS = 10 };

// Collect complete lines, echo them and keep the newest; true when it was the listening line.
static bool take(NetHost *host, const char *bytes, size_t count)
{
    bool listening = false;
    for (size_t i = 0; i < count; ++i) {
        char c = bytes[i];
        if (c == '\r')
            continue;
        if (c != '\n') {
            if (host->partial_length + 1 < sizeof(host->partial))
                host->partial[host->partial_length++] = c;
            continue;
        }
        host->partial[host->partial_length] = 0;
        host->partial_length = 0;
        if (!host->partial[0])
            continue;
        snprintf(host->last, sizeof(host->last), "%s", host->partial);
        printf("[room] %s\n", host->last);
        listening |= strncmp(host->last, "boat_server listening on", 24) == 0;
    }
    fflush(stdout);
    return listening;
}

static void exited(const NetHost *host, char *error, size_t size)
{
    // boat_server prefixes its errors with its name; the title needs only the reason.
    const char *reason = host->last;
    if (strncmp(reason, "boat_server: ", 13) == 0)
        reason += 13;
    snprintf(error, size, "The room stopped: %s", reason[0] ? reason : "no message.");
}

static void arguments(const NetHostOptions *o, char port[8], const char *out[13])
{
    snprintf(port, 8, "%u", (unsigned)o->port);
    unsigned n = 0;
    out[n++] = "--serve";
    out[n++] = "--bind";
    out[n++] = "0.0.0.0";
    out[n++] = "--port";
    out[n++] = port;
    out[n++] = "--mode";
    out[n++] = o->mode;
    if (strcmp(o->mode, "race") != 0) {
        out[n++] = "--location";
        out[n++] = o->location;
    }
    if (o->waves)
        out[n++] = "--waves";
    out[n++] = "--exit-with-stdin";
    out[n] = NULL;
}

#ifdef _WIN32

// Read what is waiting; false once the pipe is broken (the child exited).
static bool drain(NetHost *host, bool *listening)
{
    for (;;) {
        DWORD waiting = 0, got = 0;
        if (!PeekNamedPipe(host->output, NULL, 0, NULL, &waiting, NULL))
            return false;
        if (!waiting)
            return true;
        char bytes[512];
        if (!ReadFile(
                host->output, bytes, waiting < sizeof(bytes) ? waiting : sizeof(bytes), &got, NULL))
            return false;
        bool ready = take(host, bytes, got);
        if (listening)
            *listening |= ready;
    }
}

static bool alive(const NetHost *host)
{
    return WaitForSingleObject(host->process, 0) != WAIT_OBJECT_0;
}

static void pause_briefly(void) { Sleep(WAIT_MS); }

static void release(NetHost *host)
{
    if (host->input)
        CloseHandle(host->input);
    if (host->output)
        CloseHandle(host->output);
    if (host->process)
        CloseHandle(host->process);
    host->input = host->output = host->process = NULL;
    host->running = false;
}

static void end_input(NetHost *host)
{
    if (host->input)
        CloseHandle(host->input);
    host->input = NULL;
}

static void kill_child(NetHost *host) { TerminateProcess(host->process, 1); }

static bool spawn(NetHost *host, const NetHostOptions *options, char *error, size_t size)
{
    char path[1024], port[8], line[2048];
    const char *argv[13];
    DWORD length = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!length || length >= sizeof(path)) {
        snprintf(error, size, "Cannot find this program's own path to start the room.");
        return false;
    }
    arguments(options, port, argv);
    int used = snprintf(line, sizeof(line), "\"%s\"", path);
    for (unsigned i = 0; argv[i] && used > 0 && (size_t)used < sizeof(line); ++i)
        used += snprintf(line + used, sizeof(line) - (size_t)used, " %s", argv[i]);
    SECURITY_ATTRIBUTES inherit = { sizeof(inherit), NULL, TRUE };
    HANDLE child_in = NULL, child_out = NULL;
    if (!CreatePipe(&child_in, (HANDLE *)&host->input, &inherit, 0)
        || !CreatePipe((HANDLE *)&host->output, &child_out, &inherit, 64 * 1024)) {
        snprintf(error, size, "Cannot create the room's pipes.");
        if (child_in)
            CloseHandle(child_in);
        release(host);
        return false;
    }
    SetHandleInformation(host->input, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(host->output, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA start = { .cb = sizeof(start), .dwFlags = STARTF_USESTDHANDLES };
    start.hStdInput = child_in;
    start.hStdOutput = start.hStdError = child_out;
    PROCESS_INFORMATION process = { 0 };
    BOOL ok = CreateProcessA(
        NULL, line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &start, &process);
    CloseHandle(child_in);
    CloseHandle(child_out);
    if (!ok) {
        snprintf(error, size, "Cannot start %s (error %lu).", path, (unsigned long)GetLastError());
        release(host);
        return false;
    }
    CloseHandle(process.hThread);
    host->process = process.hProcess;
    return true;
}

#else

static bool drain(NetHost *host, bool *listening)
{
    for (;;) {
        char bytes[512];
        ssize_t got = read(host->output, bytes, sizeof(bytes));
        if (got == 0)
            return false; // Every writer, so the child, has gone.
        if (got < 0)
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        bool ready = take(host, bytes, (size_t)got);
        if (listening)
            *listening |= ready;
    }
}

static bool alive(const NetHost *host)
{
    int status;
    return waitpid(host->pid, &status, WNOHANG) == 0;
}

static void pause_briefly(void)
{
    struct pollfd none = { .fd = -1 };
    poll(&none, 0, WAIT_MS);
}

static void end_input(NetHost *host)
{
    if (host->input >= 0)
        close(host->input);
    host->input = -1;
}

static void release(NetHost *host)
{
    end_input(host);
    if (host->output >= 0)
        close(host->output);
    host->output = -1;
    if (host->pid > 0)
        waitpid(host->pid, NULL, WNOHANG); // Reap it if it has exited.
    host->pid = 0;
    host->running = false;
}

static void kill_child(NetHost *host)
{
    kill(host->pid, SIGKILL);
    waitpid(host->pid, NULL, 0);
}

// This program's executable: the room runs as a second copy of it.
static bool own_path(char *path, size_t size)
{
#ifdef __APPLE__
    uint32_t capacity = (uint32_t)size;
    return _NSGetExecutablePath(path, &capacity) == 0;
#else
    ssize_t length = readlink("/proc/self/exe", path, size - 1);
    if (length <= 0 || (size_t)length >= size - 1)
        return false;
    path[length] = 0;
    return true;
#endif
}

static bool spawn(NetHost *host, const NetHostOptions *options, char *error, size_t size)
{
    char path[1024], port[8];
    const char *argv[14];
    host->input = host->output = -1;
    if (!own_path(path, sizeof(path))) {
        snprintf(error, size, "Cannot find this program's own path to start the room.");
        return false;
    }
    argv[0] = path;
    arguments(options, port, argv + 1);
    int in[2], out[2];
    if (pipe(in) != 0) {
        snprintf(error, size, "Cannot create the room's pipes.");
        return false;
    }
    if (pipe(out) != 0) {
        close(in[0]);
        close(in[1]);
        snprintf(error, size, "Cannot create the room's pipes.");
        return false;
    }
    host->input = in[1];
    host->output = out[0];
    // The parent's ends must not leak into the child, or its input would never end.
    fcntl(host->input, F_SETFD, FD_CLOEXEC);
    fcntl(host->output, F_SETFD, FD_CLOEXEC);
    fcntl(host->output, F_SETFL, fcntl(host->output, F_GETFL) | O_NONBLOCK);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDERR_FILENO);
    if (in[0] > STDERR_FILENO)
        posix_spawn_file_actions_addclose(&actions, in[0]);
    if (out[1] > STDERR_FILENO)
        posix_spawn_file_actions_addclose(&actions, out[1]);
    pid_t pid;
    int failed = posix_spawn(&pid, path, &actions, NULL, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(in[0]);
    close(out[1]);
    if (failed) {
        snprintf(error, size, "Cannot start %s: %s.", path, strerror(failed));
        release(host);
        return false;
    }
    host->pid = (int)pid;
    return true;
}

#endif

bool net_host_start(NetHost *host, const NetHostOptions *options, char *error, size_t size)
{
    *host = (NetHost) { .port = options->port };
    if (!spawn(host, options, error, size))
        return false;
    host->running = true;
    printf("Hosting a %s room on UDP port %u...\n", options->mode, (unsigned)options->port);
    fflush(stdout);
    for (int waited = 0; waited < READY_MS; waited += WAIT_MS) {
        bool listening = false;
        bool open = drain(host, &listening);
        if (listening)
            return true;
        if (!open || !alive(host)) {
            drain(host, NULL);
            exited(host, error, size);
            net_host_stop(host);
            return false;
        }
        pause_briefly();
    }
    snprintf(error, size, "The room did not start within %d seconds.", READY_MS / 1000);
    net_host_stop(host);
    return false;
}

bool net_host_poll(NetHost *host, char *error, size_t size)
{
    if (!host->running)
        return false;
    if (drain(host, NULL) && alive(host))
        return true;
    drain(host, NULL);
    exited(host, error, size);
    release(host);
    return false;
}

void net_host_stop(NetHost *host)
{
    if (!host->running)
        return;
    end_input(host);
    for (int waited = 0; waited < GOODBYE_MS; waited += WAIT_MS) {
        drain(host, NULL);
        if (!alive(host)) {
            drain(host, NULL);
            release(host);
            return;
        }
        pause_briefly();
    }
    fprintf(stderr, "The room did not close in time; stopping it.\n");
    kill_child(host);
    release(host);
}
