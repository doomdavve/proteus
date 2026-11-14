#define _GNU_SOURCE
#include "room/boat_lockdown.h"

#include <stdio.h>
#include <string.h>

bool boat_lockdown_parse_mode(const char *name, BoatLockdownMode *mode)
{
    if (strcmp(name, "kill") == 0)
        *mode = BOAT_LOCKDOWN_KILL;
    else if (strcmp(name, "log") == 0)
        *mode = BOAT_LOCKDOWN_LOG;
    else if (strcmp(name, "errno") == 0)
        *mode = BOAT_LOCKDOWN_ERRNO;
    else
        return false;
    return true;
}

void boat_lockdown_describe(
    BoatLockdownMode mode, const BoatLockdownReport *r, char *out, size_t size)
{
    static const char *const names[] = { "kill", "log", "errno" };
    char landlock[96] = "landlock none";
    if (r->landlock_abi)
        snprintf(landlock, sizeof(landlock), "landlock abi %d (%s%s%s)", r->landlock_abi,
            r->landlock_fs ? "filesystem" : "none", r->landlock_tcp ? ", tcp" : "", "");
    snprintf(out, size, "lockdown: %s; %s; seccomp %s (%u syscalls)", landlock,
        r->no_new_privs ? "no_new_privs" : "privileges not restricted", names[mode],
        r->seccomp_syscalls);
}

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))                            \
    && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#if defined(__x86_64__)
#define LOCK_AUDIT_ARCH AUDIT_ARCH_X86_64
#else
#define LOCK_AUDIT_ARCH AUDIT_ARCH_AARCH64
#endif

bool boat_lockdown_supported(void) { return true; }

// ---- Landlock: raw syscalls, so no header or library dependency. ----

#define LL_CREATE_RULESET_VERSION (1U << 0)
#define LL_FS_BASE 0x1FFFULL // ABI 1: execute, read/write, directory and make_* rights.
#define LL_NET_TCP 0x3ULL // ABI 4: bind and connect TCP.

typedef struct {
    uint64_t handled_access_fs;
    uint64_t handled_access_net;
    uint64_t scoped; // ABI 6: abstract UNIX sockets (1) and signals (2) outside the domain.
} LandlockAttr;

static uint64_t landlock_fs_mask(int abi)
{
    uint64_t mask = LL_FS_BASE;
    if (abi >= 2)
        mask |= 1ULL << 13; // REFER
    if (abi >= 3)
        mask |= 1ULL << 14; // TRUNCATE
    if (abi >= 5)
        mask |= 1ULL << 15; // IOCTL_DEV
    return mask;
}

static bool apply_landlock(int min_abi, BoatLockdownReport *report, char *error, size_t size)
{
#if defined(SYS_landlock_create_ruleset) && defined(SYS_landlock_restrict_self)
    long abi = syscall(SYS_landlock_create_ruleset, NULL, (size_t)0, LL_CREATE_RULESET_VERSION);
    if (abi < min_abi) {
        snprintf(error, size, "Landlock ABI %ld is below the required %d (%s)", abi < 0 ? 0 : abi,
            min_abi, abi < 0 ? strerror(errno) : "kernel too old");
        return false;
    }
    LandlockAttr attr = { .handled_access_fs = landlock_fs_mask((int)abi),
        .handled_access_net = abi >= 4 ? LL_NET_TCP : 0,
        .scoped = abi >= 6 ? 3 : 0 };
    size_t attr_size = abi >= 6 ? sizeof(attr) : abi >= 4 ? 16 : 8;
    int fd = (int)syscall(SYS_landlock_create_ruleset, &attr, attr_size, 0U);
    if (fd < 0) {
        snprintf(error, size, "Landlock ruleset: %s", strerror(errno));
        return false;
    }
    // No rules are added: everything handled is denied. Open descriptors stay usable and
    // already-bound sockets keep working; accept() is not restricted.
    bool ok = syscall(SYS_landlock_restrict_self, fd, 0U) == 0;
    if (!ok)
        snprintf(error, size, "Landlock restrict: %s", strerror(errno));
    close(fd);
    if (ok) {
        report->landlock_abi = (int)abi;
        report->landlock_fs = true;
        report->landlock_tcp = abi >= 4;
    }
    return ok;
#else
    (void)report;
    snprintf(error, size, "Landlock syscalls unavailable in this build");
    return false;
#endif
}

// ---- seccomp-bpf: a hand-built allowlist, one comparison per syscall. ----

#define MAX_FILTER 512
typedef struct {
    struct sock_filter code[MAX_FILTER];
    unsigned length, syscalls;
    bool overflow;
} Filter;

static void emit_insn(Filter *f, struct sock_filter insn)
{
    if (f->length < MAX_FILTER)
        f->code[f->length++] = insn;
    else
        f->overflow = true;
}

// BPF_STMT/BPF_JUMP expand to braced initializers: make them compound literals.
#define emit(f, insn) emit_insn((f), (struct sock_filter)insn)
#define LOAD(offset) BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (uint32_t)(offset))
#define ARG(n) (offsetof(struct seccomp_data, args) + (n) * sizeof(uint64_t))

// nr == NR ? continue to the next instruction (the action) : skip it.
static void allow(Filter *f, long nr)
{
    emit(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint32_t)nr, 0, 1));
    emit(f, BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    ++f->syscalls;
}

static void allow_if_arg_in(Filter *f, long nr, unsigned arg, const uint32_t *values, unsigned n)
{
    // JEQ nr else skip the block; LD arg; JEQ v -> ALLOW ...; RET deny falls to the next entry.
    emit(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint32_t)nr, 0, (uint8_t)(n + 3)));
    emit(f, LOAD(ARG(arg)));
    for (unsigned i = 0; i < n; ++i)
        emit(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, values[i], (uint8_t)(n - i), 0));
    emit(f, BPF_STMT(BPF_RET | BPF_K, 0)); // patched to the default action by the caller
    emit(f, BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    // The block ended in a return, so the accumulator no longer holds nr: reload it.
    emit(f, LOAD(offsetof(struct seccomp_data, nr)));
    ++f->syscalls;
}

#define A(x) allow(f, x)
static void build_allowlist(Filter *f)
{
    // Console, files already open, time, memory, threads, polling and sockets.
    static const long plain[] = {
        SYS_read,
        SYS_write,
        SYS_readv,
        SYS_writev,
        SYS_close,
        SYS_lseek,
        SYS_fstat,
        SYS_newfstatat,
        SYS_statx,
        SYS_mmap,
        SYS_munmap,
        SYS_mremap,
        SYS_mprotect,
        SYS_madvise,
        SYS_brk,
        SYS_rt_sigaction,
        SYS_rt_sigprocmask,
        SYS_rt_sigreturn,
        SYS_sigaltstack,
        SYS_futex,
        SYS_clock_gettime,
        SYS_clock_nanosleep,
        SYS_nanosleep,
        SYS_gettimeofday,
        SYS_sched_yield,
        SYS_getpid,
        SYS_gettid,
        SYS_tgkill,
        SYS_exit,
        SYS_exit_group,
        SYS_set_tid_address,
        SYS_set_robust_list,
        SYS_rseq,
        SYS_getrandom,
        SYS_ppoll,
        SYS_pselect6,
        SYS_epoll_create1,
        SYS_epoll_ctl,
        SYS_epoll_pwait,
        SYS_eventfd2,
        SYS_pipe2,
        SYS_sendto,
        SYS_recvfrom,
        SYS_sendmsg,
        SYS_recvmsg,
        SYS_sendmmsg,
        SYS_recvmmsg,
        SYS_bind,
        SYS_connect,
        SYS_listen,
        SYS_accept4,
        SYS_getsockname,
        SYS_getpeername,
        SYS_setsockopt,
        SYS_getsockopt,
        SYS_shutdown,
        SYS_restart_syscall,
        SYS_sched_getaffinity,
        SYS_getrusage,
        SYS_uname,
#ifdef SYS_poll
        SYS_poll,
#endif
#ifdef SYS_select
        SYS_select,
#endif
#ifdef SYS_time
        SYS_time,
#endif
#ifdef SYS_epoll_wait
        SYS_epoll_wait,
#endif
#ifdef SYS_accept
        SYS_accept,
#endif
#ifdef SYS_fstat64
        SYS_fstat64,
#endif
    };
    for (size_t i = 0; i < sizeof(plain) / sizeof(plain[0]); ++i)
        A(plain[i]);
    // Only IP sockets (ICE and signalling threads create them); no AF_UNIX/NETLINK/PACKET.
    static const uint32_t domains[] = { AF_INET, AF_INET6 };
    allow_if_arg_in(f, SYS_socket, 0, domains, 2);
    static const uint32_t ioctls[] = { 0x5421 /* FIONBIO */, 0x541B /* FIONREAD */ };
    allow_if_arg_in(f, SYS_ioctl, 1, ioctls, 2);
    static const uint32_t fcntls[] = { 1 /* GETFD */, 2 /* SETFD */, 3 /* GETFL */, 4 /* SETFL */ };
    allow_if_arg_in(f, SYS_fcntl, 1, fcntls, 4);
    static const uint32_t prctls[] = { 15 /* PR_SET_NAME */ };
    allow_if_arg_in(f, SYS_prctl, 0, prctls, 1);
    // Threads only: clone needs CLONE_THREAD; clone3 reports ENOSYS so glibc falls back.
    emit(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint32_t)SYS_clone, 0, 5));
    emit(f, LOAD(ARG(0)));
    emit(f, BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x10000 /* CLONE_THREAD */, 0, 1));
    emit(f, BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    emit(f, BPF_STMT(BPF_RET | BPF_K, 0)); // default action
    emit(f, LOAD(offsetof(struct seccomp_data, nr)));
    ++f->syscalls;
    emit(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint32_t)SYS_clone3, 0, 1));
    emit(f, BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS));
}
#undef A

static bool build_filter(Filter *f, BoatLockdownMode mode)
{
    static const uint32_t actions[]
        = { SECCOMP_RET_KILL_PROCESS, SECCOMP_RET_LOG, SECCOMP_RET_ERRNO | EPERM };
    uint32_t deflt = actions[mode];
    memset(f, 0, sizeof(*f));
    emit(f, LOAD(offsetof(struct seccomp_data, arch)));
    emit(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, LOCK_AUDIT_ARCH, 1, 0));
    emit(f, BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS)); // wrong ABI, never negotiable
    emit(f, LOAD(offsetof(struct seccomp_data, nr)));
    build_allowlist(f);
    emit(f, BPF_STMT(BPF_RET | BPF_K, deflt));
    // Patch the placeholder "default action" returns (a RET with k == 0) in argument blocks.
    for (unsigned i = 0; i < f->length; ++i)
        if (f->code[i].code == (BPF_RET | BPF_K) && f->code[i].k == 0)
            f->code[i].k = deflt;
    return !f->overflow;
}

bool boat_lockdown_apply(BoatLockdownMode mode, int min_landlock_abi, BoatLockdownReport *report,
    char *error, size_t error_size)
{
    memset(report, 0, sizeof(*report));
    Filter filter;
    if (!build_filter(&filter, mode)) {
        snprintf(error, error_size, "syscall filter too large");
        return false;
    }
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        snprintf(error, error_size, "no_new_privs: %s", strerror(errno));
        return false;
    }
    report->no_new_privs = true;
    if (!apply_landlock(min_landlock_abi, report, error, error_size))
        return false; // Fail closed: no silent fallback to a weaker sandbox.
    struct sock_fprog prog = { .len = (unsigned short)filter.length, .filter = filter.code };
    if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &prog) != 0
        && prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) != 0) {
        snprintf(error, error_size, "seccomp: %s", strerror(errno));
        return false;
    }
    report->seccomp_syscalls = filter.syscalls;
    return true;
}

#else

bool boat_lockdown_supported(void) { return false; }

bool boat_lockdown_apply(BoatLockdownMode mode, int min_landlock_abi, BoatLockdownReport *report,
    char *error, size_t error_size)
{
    (void)mode;
    (void)min_landlock_abi;
    memset(report, 0, sizeof(*report));
    snprintf(error, error_size, "lockdown is only available on Linux x86-64 and aarch64");
    return false;
}

#endif
