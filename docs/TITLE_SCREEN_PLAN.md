# Boating game title screen and main-program plan

Status: revised 2026-10-24. This document owns the title, menu, and promotion of the
playable boating program. The playtest title slice is integrated into `boat_study`
(see [playtest integration](#playtest-integration-2026-10-24)): a no-argument launch shows
the prototype's logo and river backdrop with SINGLE PLAYER, MULTIPLAYER (join by address),
SETTINGS (NAME, BOAT, STEERING, SOUNDTRACK; added 2026-10-24) and EXIT. The standalone visual prototype remains at
`.worktrees/title-screen-demo`. The user and his son chose
**Wake Velocity** after considering GBG/Gothenburg names. The current bowed logo is the
one to keep for now; genuine modeled 3D is deferred. Next task: user playtest of the
title, soundtrack selection and join flow with distributed binaries. The marine HUD branch
was merged on 2026-10-24. A first [in-game Escape menu](#in-game-escape-menu-2026-10-25)
(RESUME, STEERING, CONTROLS, BACK TO TITLE / LEAVE ROOM) was implemented 2026-10-25 and has its
BACK TO TITLE path user-checked; rooms, STEERING and resume recapture await play. Location
choice, display/volume settings, title music and the live boat-and-gulls scene stay deferred. Boating music implementation and current
verification are owned by the [boating plan](BOATING_PLAN.md#selectable-soundtracks--2026-10-24).

## Identity

**Wake Velocity** is the selected game title, per the user on 2026-10-23; his son likes
it. Set the logo in a broad upward bow with individually tilted chunky letters, layered
depth, a bevel-like top highlight, and restrained gloss. The user-supplied retro 3D
"LEGEND" image is a shape reference, not a color target. Keep **A PROTEUS GAME** as the
small demo-group credit. The user chose to retain the current worktree logo on
2026-10-23. Further 3D modeling is optional later, not the current next task.

Earlier comparisons included **Watercraft** and GBG/Gothenburg names. Watercraft's
“craft” ending, paired with block letters, may suggest Minecraft or building gameplay.
**BUOY!** was superseded because
[Buoy already has a Steam listing](https://store.steampowered.com/app/1687170/Buoy/).

## First screen

At 320×200 virtual resolution, use the game's river renderer as the visual source. The
supplied bridge photo identifies the landmarks and composition to seek: Älvsborgsbron,
the harbor crane, open water, and the far shore. The current prototype uses an actual
GPU-rendered sample from just past the bridge, looking back at boat-local position
`(641.115, -842.196)` with a 32 m / 4 m camera. It is a static backdrop while the menu
and logo animate. After the name/layout review, try a live title scene with a boat passing
slowly through the bay and a few gulls crossing the view. Drive this with a separate
presentation clock so the menu does not alter a player's session, replay, or course.
Keep the menu readable over the scene. The title floats above the bridge:
warm cream faces, coral or yellow side faces, navy undersides, a few pixel highlights,
and a dark offset shadow. Use the game's existing low-resolution, nearest-neighbour
presentation. The blocky 3D silhouette borrows the *idea* of Minecraft's prominent
title, while the lettering, colors, water setting, and movement are Proteus's own.

```text
          W A K E   V E L O C I T Y   <- bowed 3D logo
             A PROTEUS GAME

          (*) SINGLE PLAYER
              MULTIPLAYER
              SETTINGS
              EXIT

       game-rendered bridge / crane / river
```

The active row gets the `(*)` marker, a brighter face, and a gentle one-pixel bob.
Keep row positions fixed when selection changes. A dark translucent panel or shaded
water behind the list provides contrast at every point in the background loop. Prefer
legibility and charm over extra particles or constant camera motion. A brief, light
tracker phrase may play on the title; gameplay music starts or resumes on entering play.

## Menu behavior

| Entry | First implementation | Later integration |
| --- | --- | --- |
| Single Player | Opens a small location choice (Basin, Eriksberg, River), then starts the existing free-boating session. Preserve the direct `--course` path. | Add course selection here if the mode becomes a regular game mode. |
| Multiplayer | Visible from day one. Until the network flow is ready, show a clear “Coming soon” message and return to selection. | Hand off to the multiplayer agent's Host/Join flow; do not invent a separate networking state machine in this menu. |
| Settings | Music and effects volume, fullscreen on native, and a small controls page. Keep defaults and saved values clear. | Add display options only after checking native and browser support. |
| Exit | Close the native program. | On the web, replace with “Back to launcher” if the page launcher remains; browser tabs cannot be closed reliably by game code. |

Up/Down or W/S moves one row; Enter/Space activates; mouse hover/click also works.
Escape goes back one menu level. In play, Escape should open a pause menu with Resume,
Settings, Return to Title, and Exit on native (first slice below, 2026-10-25). This replaces the current immediate
Escape-to-close behavior only for ordinary interactive play. Focus loss continues to
pause the simulation. Do not let the title, settings, or pause menu advance the player's
120 Hz boat session or emit gameplay wakes. A later title-only scene may animate its own
boat, gulls, and visual wake independently.

Direct command-line modes (`--screenshot`, `--time`, `--frames`, `--demo`, `--replay`,
`--course`, explicit location and review flags) enter the requested session directly.
That keeps scripted visual checks, benchmarks, and developer workflows deterministic.
Add a dedicated `--screen title --screenshot ...` capture path for title review rather
than changing the meaning of existing screenshot commands. Browser startup may use the
title immediately after the current page launcher; remove duplicate controls only after
the native and web flows have been reviewed together.

## In-game Escape menu, 2026-10-25

Completed task: Escape during a `boat_study` session no longer switches mouse steering back to
the keyboard or leaves. It opens a menu in the scene's 320x200 grid (`tools/boat_study_menu.[ch]`,
styled like the title) with RESUME, STEERING `< MOUSE / KEYBOARD >`, CONTROLS (the `?` guide over
the menu; Esc returns) and a leave row: BACK TO TITLE from the title, LEAVE ROOM in a room, QUIT
for explicit-option launches, absent in browsers. Esc or RESUME closes it; the frame that closes
it ignores gameplay clicks. While open, capture is released and the session is inactive: single
player and its audio pause; a room keeps running and the authority sees neutral controls (thrust
to neutral, wheel target centred), the same as help or focus loss. M still toggles in play. The
steering mode a session ends in is written back to the title's STEERING setting and `title.txt`.
In browsers, an unlock the game did not request (Escape, focus loss) opens the menu rather than
dropping to keyboard; denied capture still falls back to keyboard.

Decision: multiplayer has no pause; the menu says THE ROOM KEEPS RUNNING; YOUR BOAT IDLES.

Fix, 2026-10-25 (user report): BACK TO TITLE by Enter/Space or click re-entered single player,
because the activating press was still pressed in the title's first frame and activated its
default row. The title now takes no input on its first frame (it already ignored Escape there).
Build and all 118 active CTests pass. The user then played BACK TO TITLE in single player on
2026-10-25: "Works!"

Verification, 2026-10-25, macOS (Apple Silicon), Debug: build warning-clean; all 118 active
CTests passed. A temporary, reverted instrumentation opened the menu at start and saved GPU
window captures of solo (mouse and keyboard rows) and a local `boat_server` room (LEAVE ROOM
selected); they were inspected, and the solo run reported 0 simulation ticks while open. Not
done: hand play of Esc/M/resume recapture, the browser build, Windows/Linux. The title hint and
help text changed but no new title capture was reviewed.

Next task: user plays the Escape menu solo and in a room (mouse mode, Esc, STEERING, RESUME,
leave), then decide whether SOUNDTRACK or other settings belong in it.

## Playtest integration, 2026-10-24

Scope was cut to what multiplayer playtesting needs (user request, 2026-10-24): choose a
nick, pick a livery where supported, and enter an address to join. Everything else in
the menu table above remains a later proposal.

- `tools/boat_title.[ch]` owns the title: the prototype's bowed logo, footer and
  `assets/title/river-backdrop.png`, drawn at 320x200 and integer-scaled. Rows: SINGLE
  PLAYER, MULTIPLAYER, SETTINGS, EXIT. SETTINGS opens NAME / BOAT / STEERING / SOUNDTRACK / BACK
  (2026-10-24; NAME and BOAT were main rows before). STEERING picks KEYBOARD or MOUSE for the
  start of each session, passed as `--mouse-steering`. SOUNDTRACK cycles A LITTLE BOAT /
  ACID WAKE / OFF with Left/Right, Enter or a click, passed as `--soundtrack` to single
  player and joined rooms. MULTIPLAYER opens LAN / ADDRESS / CONNECT / BACK.
  LAN (added 2026-10-24) lists rooms found by
  [LAN discovery](MULTIPLAYER_PROTOCOL.md#lan-discovery). The search runs while the panel is
  open; Left/Right pick among several rooms and Enter joins one, copying its address into
  ADDRESS. Selection moves from CONNECT to LAN when the first room appears, unless the player
  has already moved it.
  Enter on a text row starts typing (printable ASCII; Enter keeps, Esc cancels, empty
  restores). Mouse hover/click works. Settings persist as `title.txt` in the
  personal-best data directory (`name=`, `address=`, `skin=`, `mouse_steering=on|off`,
  `soundtrack=a-little-boat|acid-wake|off`), saved
  when leaving SETTINGS, choosing a session or exiting. The default address is
  `127.0.0.1:24680` and the default name `player`.
- `tools/boat_study.c`: the former `main` is `study(argc, argv)`; it reuses an open window and
  leaves it open. The new `main` shows the title only for no arguments or `--title`, runs
  SINGLE PLAYER as the former macOS app arguments plus `--skin`, and MULTIPLAYER as
  `--connect ADDRESS --name NAME`. Escape in a session returns to the title; the title
  ignores the just-pressed Escape event on its first frame so it cannot immediately close
  the app. Explicit options behave exactly as before. The web build bypasses the title.
- Skins: protocol v1 (and v2) has no livery field and every client colours slots 1–4 Sweden,
  Finland, Denmark, Norway, so BOAT applies to single player only and the title says so.
  A per-player livery would need a protocol revision; not done.
- Nicks were already sent in the hello but never shown; `net_study_draw_roster` now lists
  the room's players and countries at the top right. `NetStudy.failure` keeps the
  connect/disconnect reason, which the title shows under the menu.
- The macOS app and Windows `launch.bat` now open the title (Windows previously started
  Eriksberg directly). Single player from the title is the river on both.

Verification, 2026-10-24, macOS / Apple M1 Max, Debug: build warning-free and all 104
active CTests passed (retired wake-field cases disabled); the title
capture (`boat_study --title --screenshot`) inspected at 1x/3x. A temporary scripted
build of `main` (reverted, not committed) ran single player twice in the shared window,
joined a local `boat_server` as `tester` (server roster confirmed, graceful leave),
then failed against an unused port with the message returned to the title.
`boat_study --connect` beside `boat_bot` showed the roster ("kalle SWEDEN", "davve
FINLAND (YOU)"). `boat_server --bind 0.0.0.0` accepted a bot. Not verified: interactive
keyboard/mouse use of the title (synthetic key events did not reach the window),
packaged macOS/Windows launches, Windows/Linux builds, two-machine play, and user
acceptance. The marine HUD branch is not merged; it also edits `tools/boat_study.c`
and `tools/boat_study_net.[ch]` (input/draw paths), so expect small textual conflicts.

Release naming, 2026-10-24: native packages now present **Wake Velocity** with
`wake_velocity` executables and versioned archive/Actions artifact names. This ships
package naming independently of promotion step 3 below; the internal `boat_study`
target and developer commands remain. [Releases](RELEASES.md) owns verification and
the next packaging checks. Main-program promotion remains deferred.

## Promotion sequence and ownership

1. **Title slice:** Put menu input, screen state, and rendering behind a small interface
   used by `tools/boat_study.c`. Keep the 3D letter shapes and animation data owned by
   the title module. Reuse the existing 320×200 render target and integer scaling.
   Use the worktree prototype and its real river sample as the review baseline. Capture
   idle and selected frames and inspect them at native size and 3× scale.
2. **Flow:** Add location and settings submenus, pause/return behavior, audio transitions,
   and browser-safe Exit behavior. Check keyboard and mouse paths, focus changes, and
   returning to a fresh session after a capsize. Wire Multiplayer to the network agent's
   public entry point once it exists; until then keep its unavailable state honest.
3. **Make boating the main program:** After multiplayer changes settle, make the boating
   game the default build/run and package entry. Prefer `proteus` as the stable project
   target for the game and `proteus_demo` for the four-minute audiovisual piece, while
   retaining a temporary `boat_study` alias or compatibility command for existing tests
   and scripts. Name the window and application **Wake Velocity — Proteus**. Update CMake,
   CTest commands, README launch instructions, web packager,
   macOS/Windows workflows, release artifacts, and asset paths in one reviewable change.
   Check for name assumptions with `rg` before renaming. Do not rework the multiplayer
   agent's files while it is actively changing the boating loop or build graph.

The current `proteus` demo remains available as a separate executable. Its 120 BPM
four-minute timeline and soundtrack are independent of the game menu. The game keeps
its 120 Hz physics and existing gameplay defaults. Any title animation uses presentation
time and has no effect on deterministic sessions or replay compatibility.

## Acceptance and verification

- Logo and all four rows read clearly in a 320×200 capture; selected state is clear in
  still frames and motion, with no menu text hidden by water or window scaling.
- A fresh normal launch reaches the title. Single Player reaches each location; returning
  to title and starting again produces a fresh session. Multiplayer explains its status
  until Host/Join is wired. Settings and native Exit perform their stated actions.
- Existing explicit CLI screenshot/replay/course and benchmark paths produce their prior
  outcomes. Native and web input, audio, focus, and resize behavior receive manual checks.
- Run Debug configure/build and CTest; inspect CPU and GPU title captures where supported;
  smoke the packaged macOS/Windows/web launch paths when the executable promotion lands.

Initial planning verification, 2026-10-23, macOS, before the prototype: inspected `CMakeLists.txt`,
`tools/boat_study.c`, `cmake/Web.cmake`, the project README, and the multiplayer and
foundations plans. The existing app has a direct gameplay loop, Escape closes its native
window, CLI modes enter gameplay, and web packaging targets `boat_study`. No art, audio,
runtime, or user acceptance check is claimed for this proposal. The document links and
`git diff --check` passed. Debug configure passed; the full build failed at link time while
another agent's in-progress pose/presentation extraction had new source files absent from
the CMake target lists. The requested CTest command was started, then stopped after it
reported missing executables from that incomplete build. A second configure/build after
CMake changed got farther but failed while validators still referenced presentation fields
being moved by the other agent. No passing suite is claimed for this concurrent working-tree
state. These are historical main-checkout failures during concurrent work, not the
prototype's current verification result.

The isolated worktree prototype subsequently captured a real GPU river frame from
`(641.115, -842.196)` at 73.350 s on the deterministic route, and used it behind both
title treatments. The user liked that backdrop on 2026-10-23. Its full Debug build and
all 67 active CTests passed on macOS; 13 retired field cases were disabled. Both title
captures were inspected and a 120-frame native window smoke run completed. The worktree's
`docs/TITLE_SCREEN_DEMO.md` owns commands, evidence, and remaining menu-review limits.
