# Sandbox authoring: an agent edits the working harbour

Status, 2026-11-17: **SA1 implemented locally** on `feat/sandbox-authoring` (not pushed).
Done: SA0.2 (a working harbour is a plugin run; `--work` removed; `--package` implies its
location), SA0.1 and SA0.3 as proposed (the user accepted the defaults, 2026-11-17), live
stage/Undo with the boat kept, no default-directory save for a `--plugin DIR` run, and the review
options as `/work` console requests (see the [SA1 results](#sa1-result-2026-11-15)). Open: SA2-SA4.
Done when: the user plays a work-site change that an agent staged through `./wv` in
`wake_velocity --plugin`, and the contract is in the specs (SA4). Next task: SA2 (`./wv check`
fails on free packages today).

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
