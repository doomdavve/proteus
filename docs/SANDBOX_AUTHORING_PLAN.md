# Sandbox authoring: an agent edits the working harbour

Status, 2026-11-17: **Done; this plan is history.** SA1-SA4 are implemented on
`feat/sandbox-authoring` (not pushed; one pull request awaits the user's approval). A working
harbour is a plugin run (`--plugin DIR` or `--package ID`; `--work` and the two review options are
gone, the latter as `/work` console requests); a package change restarts the session keeping an
under-way boat; the workspace tools accept free packages. The contract now lives in
[COMMAND_LINE](COMMAND_LINE.md), [PLUGIN_API](PLUGIN_API.md#agent-workspaces) and
[AGENT_AUTHORING](AGENT_AUTHORING.md). The user played a staged work-site change and Undo (accepted).
Open items: see [Leftovers](#leftovers-sa4-2026-11-17). Done when: met 2026-11-17. Next task: none.

This plan owns the authoring loop for work packages. The
[sandbox plan](FREE_RIDE_SANDBOX_PLAN.md) still owns the gameplay and the saves, the
[Plugin API](PLUGIN_API.md#work-sites-s1a) the `scene.work` contract, and
[agent authoring](AGENT_AUTHORING.md) the workspace.

## Why it does not work today

Read from the code on 2026-11-15, not run:

- `--work` is a separate mode, not a plugin run: it cannot be combined with `--plugin` or
  `--plugin-bridge` ([command line](COMMAND_LINE.md#working-harbour-in-the-game---work-native)).
  `wake_work_open` (`apps/wake_velocity/wake_velocity_work.c`) takes the *compiled-in* catalog
  package for the location, so a package on disk is never read.
- `wake_velocity --plugin DIR` refuses a free package with work sites; its objective runs only
  in `cargo_sandbox` or the work mode.
- The live bridge, the capture and `plugin_author.py capabilities` list `kind: "voyage"` only,
  and the workspace's instructions and `./wv play` assume a voyage.
- `init` can copy `plugins/outer-work` into a workspace, but nothing could then run the copy live.

Offline `check`, `validate` and `preview` already accept `free` kinds. The catalog build's
site survey and `init` probe are the validation work packages get today; whether the offline
tools run them is open (SA2).

## Decisions for the user (SA0)

1. **What a staged change does to the running session.** The
   [working-harbour contract](PLUGIN_PLAN.md#working-harbour-contract-proposed-2026-11-07)
   already says a changed work-site definition restarts the cargo session and Undo restores
   through the same restart. Proposed: restart the cargo session at the new definition, keep
   the boat and the clock, and never write the player's WORKING HARBOUR save from an authoring
   run (it uses `--data-dir`, or no autosave). The alternative is to hold every change until
   the next restart, which loses the point of live authoring.
2. **Command line (decided by the user, 2026-11-15: "`--work` should just be a plugin").** A
   working harbour is a package like any other: `--plugin DIR` runs a `free` package with
   `scene.work` as the working-harbour session, and `--work` is removed rather than kept as an
   alias. The save options (`--new`, `--save`, `--data-dir`) are player
   state, not tooling, and stay, applying when the running package has work sites. The two
   developer review options (`--work-review-pose`, `--work-clock-report`) are tooling, so they
   leave the command line and are exposed through the plugin interface instead
   (user, 2026-11-15; see SA1). The bundled package is chosen with the existing catalog selector, `--package proteus.outer-work` (decided by the user, 2026-11-15); the title's START and scripted runs pass it. A bundled package names its own location, as a plugin directory does, so `--location` is unnecessary and must agree if given (user, 2026-11-15). The bridge continues to need a window.
3. **Scope of the first pass.** Proposed: native, solo `outer-work` and `basin-work` shaped
   packages; screenshots through `--plugin ... --screenshot`, no new capture kind;
   no browser and no room.

## Steps

One branch (`feat/sandbox-authoring`), one commit per step, one pull request after approval.

- **SA1. A work package is just a plugin.** `--plugin DIR` (and the catalog selector from
  SA0.2) accepts a `free` package with work sites and runs the working-harbour session,
  loading and watching it through the existing plugin host and its revisions instead of the
  compiled catalog entry; the refusal in `wake_velocity_plugin.c` goes. `--work` is removed.
  So are `--work-review-pose swing|deck|approach` (prepare a real cargo pose for a capture) and
  `--work-clock-report FILE` (a CSV of GL sun/mask observations): they become requests on the
  running game through the live bridge and its console, `live-console --line TEXT` and
  `./wv console`, so an agent or a scripted test asks for a pose or a report the same way.
  Scripted runs that need them without a human use the bridge with a virtual clock. The exact
  line names are chosen in SA1 and recorded in the API. The users of `--work` move: the title's START, `docs/COMMAND_LINE.md`, `PLAYING.md`, `TESTING.md`,
  `WEB_BUILD.md`, the sandbox and other plans' commands, `tests/app/tests.cmake` and
  `tests/render/tests.cmake`. Existing saves and the autosave rules are unchanged. A stage, Undo or reload restarts the cargo session per SA0.1 and
  restages the job's `init`. `--plugin-bridge` works with it (inspect, stage, apply, undo,
  discard, console). Unknown until read: how far `WakeWork`'s static revision and
  `wake_plugin_*` can share one host; if the work session needs its own host, say so and
  size the step then. Tests: a virtual-clock bridge case beside `wake_velocity_bridge`, a
  restart case in the work-session tests, and the retargeted `--work` tests and captures (byte-identical frames expected); the six
  `tests/app/tests.cmake` cases on the two review options move to the bridge requests.
- **SA2. The tools accept work packages.** `capabilities` reports `live.kinds` and the
  workspace kind; `init --from plugins/outer-work` writes instructions about sites, the job
  and the survey diagnostics instead of voyage shelters; `./wv play` starts `--plugin DIR` as for a voyage;
  `./wv check` runs the site survey and the job's `init` probe, or reports why it cannot.
  Tests in `tests/tools/test_plugin_author.py`.
- **SA3. Trial.** Re-run the [agent trial](AGENT_AUTHORING.md#trial) on a work package:
  move the landing, stage it, play, undo; try a request the survey rejects (a site on land).
  Record the agent's report and the user's play separately.
- **SA4. Closing step.** Move the contract (command-line combination, restart rule, tool
  kinds) into `COMMAND_LINE.md`, `PLUGIN_API.md#agent-workspaces`, `AGENT_AUTHORING.md` and
  the sandbox plan's status; turn leftovers into open items; mark this plan done or move it
  to history.

## Defers

- Payouts, fuel and progression (sandbox S3+), the structured HUD, T0/S2.
- Capture of work sites as a new `plugin_author.py capture` kind, browser authoring and
  rooms with work sites.
- The user's own list of Plugin API changes from using the workspace; collect it before SA1
  and fold in whatever touches work sites, otherwise plan it separately.
- Windows and Linux runs of the new loop, as for the voyage workspace.

## Verification limits

Nothing here is run. SA1's size is an estimate from reading `wake_velocity_work.c` and
`wake_velocity.c` only. No human acceptance exists for authoring a work package.

## SA1 result (2026-11-15)

Implemented: `--plugin DIR` and `--package ID` run a free package with work sites as the
working-harbour session (the revision comes from the plugin host); a changed revision restarts
the cargo session at once; the title starts WORKING HARBOUR with `--package proteus.outer-work`;
`--work` is gone and the tests and specs use the new spelling.

Automated (macOS arm64, Debug, 2026-11-15): the full CTest preset passed (601 of 601) before
the location change and the 31 work and title-start tests after it. CPU frames of the berth
drive at 14 s are byte-identical between the old `--work`, `--package` and `--plugin DIR`.

Windowed, agent-run, bounded `--frames` run of `--plugin DIR --plugin-bridge DIR` on a
workspace made by `init --from plugins/outer-work`: `./wv status` answered; a stage that moved
the landing onto the quay and one that put it where the survey rejects it were refused with
`scene.work.sites[N]` diagnostics and the running session untouched; a stage of the landing's
heading applied as R2 (tick back to 25, nothing held) and `./wv undo` returned to R1 with the
same restart; the run's bounded exit wrote its autosave. Not checked: how the restarted site
looks (nobody watched the window), `./wv play`, held/stale pins, a staged edit during a carried
crate, and any other platform.

Found: `./wv check` fails for a free package (`preview is not available for free or course
packages`); SA2 must make it run the survey without the preview. The restart is the session's ordinary reset (`boat_work_session_reset`, read from the code, not
measured beyond the tick returning to 25): the boat returns to the service berth, the job
starts over and the tick resets; the session clock's hour is kept. That is a fresh job rather
than SA0.1's proposed "keep the boat", so SA0.1 still needed the user's decision (taken 2026-11-17, below).

## SA1 completion (2026-11-17)

The user accepted the proposed SA0.1 and SA0.3. Implemented: `boat_work_session_restart`, used for
a package change's restart: cargo, sites and job start over at the new definition; a boat under
way (not moored, mooring or capsized) keeps its pose and motion and the clock keeps its hour; a
moored boat returns to the berth. The menu's RESTART JOB and the reset key still use the full
reset. A `--plugin DIR` run does not use the game's default data directory unasked (`--save` or
`--data-dir` still do). `--work-review-pose` and `--work-clock-report` are removed; the console
requests `/work pose swing|deck|approach` and `/work clock-report FILE|stop` replace them (wake
command layer `WakeCommandWork`; the pose applies to the authority and restarts the local
client). The six rejection cases on the options are gone; `wake_command` covers the routing and
`boat_work_local.restart_keeps_boat` the restart.

Automated (macOS arm64, Debug, 2026-11-17): the work-local, wake_command, bridge and CLI cases
pass; the full preset then passed (596 of 596). Windowed, agent-run: with `--plugin`
on an `init --from plugins/outer-work` workspace and the bridge, `./wv console --line "/work pose
deck"` posed the boat, an unreachable pose and an unknown name were refused with their reasons,
and `/work clock-report FILE` wrote a CSV when the bounded run ended (45 KB, rows from the live GL
observations). Not checked: a live stage with the boat under way (only the session unit case),
the posed frames by eye, the virtual-clock bridge case in the plan's test list, and other
platforms. `ARCHITECTURE_PLAN.md` still names `--work-review-pose` as written when it landed.

## SA2 result (2026-11-17)

Implemented in `tools/plugin_author.py` and `tools/plugin_workspace.py`: `capabilities` reports
`live.kinds` `["voyage","free"]` (a free package needs `scene.work`) and `workspace.check` per
kind; `init` writes work-site instructions (sites, job, survey diagnostics, the restart rule,
`/work` requests, no player save) for a `free` source and `init`'s result names the `kind`;
`./wv check` on a free package runs `validate` only and says in `note` that it covers the survey
and the job's `init` probe (there is no preview), and refuses `--anchor`. `./wv play` already
starts `--plugin DIR`, so it needed no change. Finding: `validate` already ran the survey and the
`init` probe, so no new worker code was needed; only the preview step was the failure. Tests in
`tests/tools/test_plugin_author.py`: `test_work_package_workspace` (a site moved to z = 9000 is
refused at `scene.work.sites[2]`; an `error()` in `init` is refused) and
`test_capabilities_name_workspace_kinds`; the `boat_plugin_author` case passed (55 tests, 3
skipped as before). Not run: `./wv stage` of a free package against a live game through the
test bridge server (the SA1 windowed run covered it by hand); `capture` stays voyage-only.

## SA3 trial, agent half (2026-11-17)

Everything below is the agent's own report of its commands (a Claude subagent confined to
`/private/tmp/proteus-sa3/ws`, `outer-work` copy, native Debug on macOS, a windowed game started
with `./wv play`); I did not re-run or independently check it, and nobody has played the result.

- Request 1, move the landing 25 m north. The agent found that north is +z from `AGENTS.md`, the
  comment in `main.lua` and the API reference, unaided. The survey rejected (x -105, z 25)
  (`scene.work.sites[2]`: "platform, approach or crane sweep is not clear of the outer map,
  berths, modules or traffic"). By scanning (about 35 `./wv check` runs) it found only z 0-3
  accepted at x -105, and chose (x -135, z 25), which delivers the 25 m north but also moves
  30 m west. `./wv stage`: `applied`, R2, epoch 1, "WORK SESSION RESTARTED WITH THE NEW PACKAGE".
- Rejection probe: the jetty at (15000, 15000) was refused by `check` and `stage` at
  `scene.work.sites[4]`; `./wv status` still showed R2 epoch 1, nothing held. The agent reverted
  the file, and `check` then gave the same `bundle_id` as the staged bundle.
- Not verified: motion, feel, whether the boat still arrives straight in, the stale comment above
  the landing line, and the staged digest (`status` and `stage` report `0000000000000000` for a
  free package; only `check`'s `gameplay_digest` 935553c06c380e35 identifies the content).
- Friction (to fold into SA4 or plan separately): the survey rejection names no obstacle and no
  nearest clear position, so the agent had to scan by hand; the zero `digest` field is confusing
  for a free package; `undo` has not been tried.
- Owed from the user: play R2 (landing at -135, 25) and judge it, then run Undo; record that
  separately from this report.

## SA3 trial, user's play (2026-11-17)

The user played the staged R2 (landing at x -135, z 25) and Undo in the window and reported
"Works well!", without further detail. Not recorded, so not claimed: whether the delivery was
completed, how the approach felt, and whether the boat stayed in place on Undo while under way
(the user was asked to check it; the answer was not itemised). Native Debug on macOS only.

## SA3 correction (2026-11-17)

The user retried SA0.1's check (Undo with the boat under way) and reported that the boat snapped
back. Cause found by reading, then covered by a test: the restart reset the session's view to the
crane view (`boat_work_session_reset_world`), which takes the helm and shows the berth's crane, so
a boat the player had taken to the helm view with Tab was frozen and shown at the berth although
its position was kept. `boat_work_session_restart` now also keeps the helm view for an under-way
boat; `boat_work_local.restart_keeps_boat_local` drives the running local session (depart, helm
view, thrust, restart, new generation) and checks the mirror's boat and view. The earlier "works
well" does not cover this step; the retry is owed again. Whether the boat's position itself had
been kept in the user's run was not measured.

## SA3 retry, user's play (2026-11-17)

On the rebuilt game (`4ef23fc`) the user staged-and-undone R2 with the boat under way (helm view,
open water): the boat did not return to the berth and stayed roughly where it was; the user noticed
a "blip" at the Undo. Not diagnosed: the blip is probably the restart's presentation reset (player
wake trail and camera) or the new generation's checkpoint, but that is a guess. Accepted as
working; the blip is an open item for SA4. Native Debug on macOS, one run.

## Leftovers (SA4, 2026-11-17)

Open items, not gates; nothing here is scheduled.

- The site survey's rejection names no obstacle and no nearest clear position, so an agent scans
  by hand (about 35 `./wv check` runs in the trial).
- `status` and `stage` report `digest` `0000000000000000` for a free package; only `check`'s
  `gameplay_digest` identifies the content.
- A small "blip" at the restart with the boat under way (unexplained; probably the presentation
  reset or the new generation's first frame).
- Not checked: the user's own Plugin API change list (collect it and fold in what touches work
  sites), a held change or a stale pin on a work package, the posed `/work pose` frames by eye,
  the virtual-clock bridge case named in SA1, a staged edit during a carried crate, browser
  authoring and rooms with work sites, and Windows and Linux.
- `ARCHITECTURE_PLAN.md` and `SUNSET_PLAN.md` still describe `--work-review-pose` and
  `--work-clock-report` as written when they landed (the latter is marked superseded).
