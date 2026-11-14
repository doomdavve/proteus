// H5: after boat_lockdown_apply, forbidden syscalls fail (errno mode) or kill (kill mode), the
// steady-state ones still work, and an unreachable Landlock ABI refuses without a filter.
// Each case runs in a forked child; Linux only (the test is not registered elsewhere).

#define _GNU_SOURCE
#include "room/boat_lockdown.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef int (*Case)(void);

static int expect_errno(long result, int expected)
{
    return result == -1 && errno == expected ? 0 : 1;
}

// Returns the child's exit status, or 128 + signal when it was killed.
static int run_child(Case body)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0)
        _exit(body());
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status);
}

static bool lock(BoatLockdownMode mode)
{
    BoatLockdownReport report;
    char error[160];
    if (!boat_lockdown_apply(mode, BOAT_LOCKDOWN_MIN_LANDLOCK_ABI, &report, error, sizeof(error))) {
        fprintf(stderr, "lockdown refused: %s\n", error);
        return false;
    }
    return true;
}

static int denied(void)
{
    // Open a UDP socket first: descriptors made before the lockdown keep working.
    int before = socket(AF_INET, SOCK_DGRAM, 0);
    if (!lock(BOAT_LOCKDOWN_ERRNO))
        return 90;
    int bad = 0;
    bad += expect_errno(open("/etc/hostname", O_RDONLY), EPERM);
    bad += expect_errno(open("/tmp/h5-new-file", O_WRONLY | O_CREAT, 0600), EPERM);
    char *const argv[] = { "/bin/true", NULL };
    bad += expect_errno(execve("/bin/true", argv, NULL), EPERM);
    bad += expect_errno(socket(AF_UNIX, SOCK_STREAM, 0), EPERM);
    bad += expect_errno(socket(AF_PACKET, SOCK_RAW, 0), EPERM);
    bad += before < 0 || close(before) != 0;
    return bad;
}

static int steady_state(void)
{
    int a = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0); // Before: bound at startup.
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    if (a < 0 || bind(a, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        return 1;
    socklen_t len = sizeof(addr);
    getsockname(a, (struct sockaddr *)&addr, &len);
    if (!lock(BOAT_LOCKDOWN_KILL))
        return 90;
    int b = socket(AF_INET, SOCK_DGRAM, 0); // IP sockets stay allowed for ICE threads.
    char ping[4] = "ping", pong[8] = { 0 };
    void *memory = malloc(1 << 20);
    struct timespec now;
    if (b < 0 || !memory || clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 2;
    if (sendto(b, ping, sizeof(ping), 0, (struct sockaddr *)&addr, sizeof(addr)) != 4)
        return 3;
    struct pollfd pfd = { .fd = a, .events = POLLIN };
    if (poll(&pfd, 1, 5000) != 1 || recv(a, pong, sizeof(pong), 0) != 4 || memcmp(pong, ping, 4))
        return 4;
    free(memory);
    if (write(STDOUT_FILENO, "", 0) != 0)
        return 5;
    return 0;
}

// glibc's first localtime and address-sort use read files and probe sockets. The lockdown warms
// them up first, so WebRTC's lookups and log timestamps after it do not hit the kill filter.
static int libc_after_lock(void)
{
    if (!lock(BOAT_LOCKDOWN_KILL))
        return 90;
    time_t now = time(NULL);
    struct tm local;
    if (!localtime_r(&now, &local))
        return 1;
    struct addrinfo hints = { .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = IPPROTO_UDP,
        .ai_flags = AI_PASSIVE | AI_ADDRCONFIG };
    struct addrinfo *result = NULL;
    if (getaddrinfo(NULL, "0", &hints, &result) != 0)
        return 2;
    freeaddrinfo(result);
    hints.ai_flags = AI_ADDRCONFIG;
    if (getaddrinfo("127.0.0.1", "3478", &hints, &result) != 0)
        return 3;
    freeaddrinfo(result);
    return 0;
}

static int killed_execve(void)
{
    if (!lock(BOAT_LOCKDOWN_KILL))
        return 90;
    char *const argv[] = { "/bin/true", NULL };
    execve("/bin/true", argv, NULL);
    return 0; // Not reached when the filter kills.
}

static int killed_open(void)
{
    if (!lock(BOAT_LOCKDOWN_KILL))
        return 90;
    open("/etc/hostname", O_RDONLY);
    return 0;
}

static int refusal(void)
{
    BoatLockdownReport report;
    char error[160];
    if (boat_lockdown_apply(BOAT_LOCKDOWN_ERRNO, 99, &report, error, sizeof(error)))
        return 1;
    // A refusal installs nothing: open() still works (it would be EPERM under the filter).
    int fd = open("/etc/hostname", O_RDONLY);
    return fd >= 0 && strstr(error, "Landlock") ? 0 : 2;
}

static int landlock_files(void)
{
    // Landlock alone, without seccomp: ENOENT/EACCES, not EPERM. Reached via a fresh child
    // that applies only the full lockdown in log mode (nothing is blocked by seccomp there).
    if (!lock(BOAT_LOCKDOWN_LOG))
        return 90;
    int fd = open("/etc/hostname", O_RDONLY);
    if (fd >= 0 || errno != EACCES)
        return 1;
    int tcp = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    return tcp >= 0 && bind(tcp, (struct sockaddr *)&addr, sizeof(addr)) == -1 && errno == EACCES
        ? 0
        : 2;
}

int main(void)
{
    BoatLockdownMode mode;
    if (!boat_lockdown_supported() || !boat_lockdown_parse_mode("kill", &mode) || mode != 0)
        return 1;
    int failures = 0;
    struct {
        const char *name;
        Case body;
        int expect;
    } cases[] = { { "denied calls fail", denied, 0 }, { "steady state works", steady_state, 0 },
        { "libc lookups survive the lockdown", libc_after_lock, 0 },
        { "execve kills", killed_execve, 128 + SIGSYS },
        { "open kills", killed_open, 128 + SIGSYS }, { "refusal installs nothing", refusal, 0 },
        { "landlock denies files and tcp bind", landlock_files, 0 } };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int got = run_child(cases[i].body);
        printf("%s: %s (%d)\n", cases[i].name, got == cases[i].expect ? "ok" : "FAIL", got);
        failures += got != cases[i].expect;
    }
    printf("validate_boat_lockdown=%s\n", failures ? "fail" : "ok");
    return failures != 0;
}
