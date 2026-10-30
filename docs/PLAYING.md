# Playing Wake Velocity

[Project introduction](../README.md) · [Command-line reference](COMMAND_LINE.md) ·
[All documentation](README.md)

## Start playing

[Play in your browser](https://doomdavve.github.io/proteus/) or download a native build
from [Releases](https://github.com/doomdavve/proteus/releases/latest).
Native first-launch details: [macOS](MACOS_BUILD.md), [Windows](WINDOWS_BUILD.md).
For a source build, run `./build/wake_velocity` from the repository root so assets resolve.

The title offers SINGLE PLAYER, MULTIPLAYER, SETTINGS and CREDITS. SETTINGS remembers your
name, boat livery, steering mode and soundtrack. Choose A LITTLE BOAT, ACID WAKE or OFF;
Off leaves engine and water sounds enabled. The Nordic liveries share handling. The
cabin boat reaches about 25 knots on calm water, and the explorer has a stronger engine during
solo free boating; timed courses use the standard engine and multiplayer assigns Nordic liveries by player slot.

## Controls

| Input | Action |
| --- | --- |
| Hold W / Up | Forward thrust; release for neutral |
| Hold S / Down | Reverse thrust, also braking forward motion |
| Hold Shift | Precision power for docking |
| A/D or Left/Right | Turn the wheel; release to let it centre |
| M | Switch keyboard/mouse steering |
| Hold Backspace for one second | Reset, restart or recover, depending on mode |
| P or Escape | Open the menu |
| ? | Show controls |
| N | Cycle corner minimap, full chart and off on mapped locations |
| + / − | Move camera closer / farther |
| PageUp / PageDown | Raise / lower camera |
| C | Restore default camera |
| F3 | Toggle profiler |

Holding both thrust directions selects neutral. Reverse power is weaker than forward;
Shift selects 5% power in either direction. Scrolling is unassigned and R does nothing.

Mouse steering captures the pointer. Move horizontally to turn the wheel, then stop
moving to hold its setting; move back to straighten it. The wheel needle shows the actual
steering angle. Help, menus and loss of focus release capture. In a browser, click the
canvas after resuming to recapture; use keyboard steering if capture is unavailable.

Solo play pauses in help/menu or on focus loss. Multiplayer continues and your boat
idles with neutral thrust and centred steering. Use P for the browser menu because
Escape may also leave fullscreen. Return to the title or leave a room through the menu.

A brief Backspace tap does nothing. Releasing the key, opening help/menu or losing focus
cancels the reset hold. A reset centres steering and selects neutral. Bad landings can
capsize the boat and stop propulsion; hold Backspace or choose the menu's explicit
reset/recovery action to get moving again.

## Explore

The river starts in the Eriksberg dock, with moored craft, moving traffic and gulls.
Leave the dock and follow the river southwest towards Älvsborgsbron. The basin provides
an open practice area. Player wakes are rideable; crossing your own trail or another
boat's wake can launch you into the air. Steering still provides modest yaw in flight,
while automatic stabilization helps the boat land upright.

For the larger map and solo voyage, launch with `--location outer` (see
[launch options](COMMAND_LINE.md#locations-and-voyages)). It extends upstream past
Lindholmen, Lilla Bommen and Hisingsbron, and out towards Långedrag. Follow the destination
arrow and arrive in Långedrag's old harbour at 6 m/s or less. Passing a shelter sets your
recovery point. Recovery returns you there while voyage time and water keep running.

A squall builds offshore as you approach the outer fjord. The eastern shore offers
calmer water. The chart shows shelters, the destination and the exposed area; its blue
shore tints show distance from land, not depth. This is a game map, not a navigation aid.
The longer adventure to Vinga remains [planned](FREE_RIDING_ADVENTURE_PLAN.md).

## Basin time trial and replays

Choose Basin trial in single-player setup, or launch with `--course`. Cross the start
line northbound, pass the two highlighted buoy gates in order, then cross the chequered
finish. Yellow marks the next gate. Missed gates can be approached again while the timer
continues. Hold Backspace or choose RESTART RUN to retry.

Completed attempts are recorded automatically. Your fastest eligible run becomes a
cyan ghost on the next attempt, with split-time comparisons. Resetting abandons an
unfinished attempt but preserves your last completed run and personal best.

| Input | Course / replay action |
| --- | --- |
| G | Toggle the ghost |
| L / B | Watch last run / personal best before starting or after finishing |
| Space during replay | Pause/resume; resume from finish plays again from the beginning |
| Enter during replay | Restart at the selected speed |
| Left/Right during replay | Seek one second; Shift seeks five seconds |
| Home/End during replay | Jump to start/finish |
| Click/drag replay timeline | Seek and pause |
| [ / ] or , / . | Slower/faster solo simulation or replay, from ¼× to 8× (`,` `.` and the typed brackets work on any layout, e.g. Swedish AltGr+8/9) |
| \ or 0 | Restore 1× |
| F11 | Toggle borderless fullscreen (also in the Escape menu); not in the browser |
| Backslash | Restore normal speed |

Use the menu's RESTART RUN to leave replay; Backspace reset is inactive during playback.
An attempt that uses altered simulation speed becomes PRACTICE even after returning to
normal speed. It remains available as the last replay but cannot replace a best or an
exported run. Return to 1× and restart to regain eligibility. Multiplayer uses the server
clock and ignores these speed shortcuts.

### Saved runs

Calm and wave-course bests are stored separately. Native storage uses:

| Platform | Default directory |
| --- | --- |
| macOS | `~/Library/Application Support/Proteus/` |
| Windows | `%LOCALAPPDATA%/Proteus/` |
| Linux | `$XDG_DATA_HOME/proteus/` or `~/.local/share/proteus/` |

The current files are `course-calm-physics9.run` and `course-waves-physics9.run`.
Title settings live alongside them in `title.txt`. Use `--data-dir DIR` to override the
native directory. Bests from older physics revisions stay on disk and are not overwritten;
incompatible or damaged runs are ignored with a message. A failed save preserves the best
in memory. Recording is limited to ten minutes; longer runs can finish but cannot replace
recorded bests. See [import/export commands](COMMAND_LINE.md#courses-and-recordings).

Browser saves are local to the browser and site through IndexedDB. Clearing site data
removes them, and they do not sync with native saves. Play still works if durable storage
is unavailable. The browser page does not expose local recording import/export.
Development browser builds also offer an opt-in [PLUGIN VOYAGE source-bundle panel](PLUGIN_API.md#browser-source-bundles-p43)
for solo outer-map descriptors: import/edit source, stage it, then play; live route/water edits
wait for Apply, with Undo available. Opening the panel pauses solo input. Export preserves
source; storage denial is reported, and restored source requires validation before play.

## Multiplayer

Rooms support up to four players. Native and browser clients must match the room's
protocol and physics versions; use matching releases when playing together.

- **Free boating:** share the water, collide and jump each other's wakes.
- **River race:** with two or more players, line up for a countdown, pass six gates in
  order, cross ferry wakes and a moving storm, then finish for a confirmed place.
- **Deathmatch:** capsize or sink opponents to score. Rounds start with two players;
  defeated boats respawn and the scoreboard shows the standings.

In deathmatch, hold Space for the water cannon, press E to drop a stern wave grenade,
Q to launch a torpedo, and V to toggle the rearview mirror. Cannon pressure can capsize
boats and sustained fire overheats it; grenades can also capsize their thrower. Torpedo
ammunition is limited per life. Hold Tab for the scoreboard. See the
[deathmatch rules](DEATHMATCH_PLAN.md#arsenal-milestone--2026-10-27) for timings and limits.

### Join a room

Choose MULTIPLAYER and select a published SERVER. The
[Winterfell guide](MULTIPLAYER_HOSTING.md#join) gives the hosted room's current release
and joining instructions. Native clients can also select a discovered LAN room or use
ADDRESS and CONNECT with `HOST:24680`. Left/Right cycles available rooms. The title has
no token field; use `--token` from the terminal for token-protected rooms.

Browsers join WebRTC-enabled rooms; they cannot discover LAN rooms or host an authority.
See [browser multiplayer](WEB_BUILD.md#webrtc-rooms) for setup and limits.

### Hosting from the game

On native builds, choose MULTIPLAYER → HOST A ROOM, select MODE, PLACE and WATER, then
START ROOM. The game starts an authority in the background and joins it. Allow Wake
Velocity through the firewall. The room closes when you leave, quit or crash; CLOSE ROOM
ends it for everyone. Friends on the same network choose its LAN entry or enter the
host's LAN address with UDP port 24680. Guest-network isolation, firewalls and VPNs can
prevent discovery.

Internet hosting needs UDP port forwarding or a VPN for native clients. Browser access
needs the optional WebRTC endpoint. Native transport has no automatic NAT traversal,
transport encryption or player accounts. See [server commands](COMMAND_LINE.md#rooms-and-scripted-clients)
and the [hosting guide](MULTIPLAYER_HOSTING.md) for operator setup.

## Phones and tablets

The browser provides Forward/Reverse and left/right touch controls, plus optional tilt
steering. Use Menu to recover or return to the title. Tilt requires HTTPS and permission;
recenter after rotating. On iPhone, Safari's Share → Add to Home Screen → Open as Web App
provides more screen space. Mobile weapons, replay controls and text editing remain
limited; see [mobile controls and qualification](WEB_BUILD.md#mobile-controls-and-iphone-presentation-2026-10-28).
