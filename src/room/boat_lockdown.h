#ifndef BOAT_LOCKDOWN_H
#define BOAT_LOCKDOWN_H

#include <stdbool.h>
#include <stddef.h>

// H5: in-process lockdown for the room server (Linux only; elsewhere it reports unsupported).
// Applied once, after assets, package, plugin load and socket binding. Landlock removes
// filesystem access (and TCP bind/connect where the kernel has it), no_new_privs blocks
// privilege gain, and a hand-written seccomp-bpf allowlist limits the steady-state syscalls.
// See docs/MULTIPLAYER_HOSTING.md for the policy and its limits.

typedef enum {
    BOAT_LOCKDOWN_KILL, // A syscall outside the allowlist kills the process (production).
    BOAT_LOCKDOWN_LOG, // Allowed but logged by the kernel (audit/journal): tune the list.
    BOAT_LOCKDOWN_ERRNO // Fails with EPERM (tests).
} BoatLockdownMode;

typedef struct {
    int landlock_abi; // 0 when Landlock is unavailable.
    bool landlock_fs; // All filesystem access removed.
    bool landlock_tcp; // TCP bind and connect removed (ABI 4 and later).
    bool no_new_privs;
    unsigned seccomp_syscalls; // Allowlist entries; 0 when seccomp was not installed.
} BoatLockdownReport;

bool boat_lockdown_supported(void);
bool boat_lockdown_parse_mode(const char *name, BoatLockdownMode *mode);
// Fails closed, with nothing silently weakened: Landlock below min_landlock_abi (or failing),
// no_new_privs or the seccomp filter failing all return false. Landlock applies before seccomp,
// so a refusal leaves no seccomp filter installed. Landlock ABI 6 is what Linux 6.12 provides.
#define BOAT_LOCKDOWN_MIN_LANDLOCK_ABI 6
bool boat_lockdown_apply(BoatLockdownMode mode, int min_landlock_abi, BoatLockdownReport *report,
    char *error, size_t error_size);
// One line such as "lockdown: landlock abi 6 (filesystem, tcp); no_new_privs; seccomp kill (74)".
void boat_lockdown_describe(
    BoatLockdownMode mode, const BoatLockdownReport *report, char *out, size_t size);

#endif
