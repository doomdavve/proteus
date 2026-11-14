# Empty-room server CPU

Current state: done locally; behavior moved into the scheduling and command-line specs.
Open items: Winterfell CPU and native/browser join qualification after release.
Done when: idle waits sleep, empty rooms pause and resume without catch-up, full suite passes.
Next task: review/release, then measure Winterfell CPU and check native/browser joins.

## Scope

Fix the fractional-millisecond ENet busy poll, then suspend empty ordinary standalone rooms
following a ten-second grace. Keep transport handshakes, discovery and package reload alive.
Timed/scripted/recording runs advance continuously. Pending revisions defer suspension.
The existing Open loops board's multiplayer impairment/soak, platform and security qualification
remain with their owners; this track does not close them.

## Steps

1. Implement rounded-up ENet waits. Complete locally.
2. Implement empty-room pause, maintenance and rebased resume; add virtual-clock regression
   coverage for grace, long pause, join, leave and explicit tick limits. Complete locally.
3. Close into the scheduling contract in MULTIPLAYER_PROTOCOL.md and operator behavior in
   COMMAND_LINE.md. Complete; automated and local process evidence below.

## Evidence — 2026-11-15

macOS/AppleClang Debug build passed, with no compiler warnings. Full CTest passed all 571
registered tests in 52.03 seconds with local socket access. Memory and real-ENet virtual-clock
checks cover the grace, an hour-long frozen interval, join without catch-up, leave and re-pause,
and tick-limit exemption. The initial sandboxed run could not open sockets; the unrestricted
local-socket run above passed.

Actual process smoke check: after 12 seconds empty, a bot joined at frozen tick 1199, drove
18.2 m over three seconds, left, and finished consistent. Server output reported no faults,
timeouts or catch-up backlog. This is automated process verification, not human gameplay.

Single 25-second empty-room process comparison against exact parent bb4d589, both Debug,
basin/calm/free on macOS: parent 0.1687 CPU seconds and 2964 ticks; fixed 0.1069 CPU seconds
and 1199 ticks. This includes startup and the ten-second grace, and ran alongside other local
verification; it is indicative rather than a Linux performance claim. The fixed process was
stopped by SIGINT and exited successfully. The test loop itself uses virtual time, not sleeps.

No Winterfell deployment or human native/browser gameplay check was performed. Linux CPU
savings, WebRTC wake latency, and hosted package-reload behavior remain deployment checks.
The server remains resident and schedules paused maintenance at 20 Hz.
