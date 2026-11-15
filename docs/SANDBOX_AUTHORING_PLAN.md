# Sandbox authoring: an agent edits the working harbour

Status, 2026-11-15: **proposed; nothing implemented.** Goal: the
[external agent workspace](AGENT_AUTHORING.md) works on a working-harbour package
(`kind: "free"` with `scene.work`, for example `plugins/outer-work`) as it does on a voyage:
the agent edits sites and the job, stages the change into the running game and the user plays
it. Open items: decisions SA0, steps SA1-SA4. Done when: the user plays a work-site change
that an agent staged through `./wv` in `wake_velocity --work`, and the contract is in the
specs (SA4). Next task: SA0, the user's decisions.

This plan owns the authoring loop for work packages. The
[sandbox plan](FREE_RIDE_SANDBOX_PLAN.md) still owns the gameplay and the saves, the
[Plugin API](PLUGIN_API.md#work-sites-s1a) the `scene.work` contract, and
[agent authoring](AGENT_AUTHORING.md) the workspace.

## Why it does not work today

Read from the code on 2026-11-15, not run:

- `wake_velocity --work` cannot be combined with `--plugin` or `--plugin-bridge`
  ([command line](COMMAND_LINE.md#working-harbour-in-the-game---work-native)).
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
2. **Command line.** Proposed: `--work` combines with `--plugin DIR` (run that package instead
   of the bundled one) and with `--plugin-bridge DIR`; the bridge continues to need a window.
   Plain `--work` is unchanged.
3. **Scope of the first pass.** Proposed: native, solo `outer-work` and `basin-work` shaped
   packages; screenshots through the existing `--work --screenshot`, no new capture kind;
   no browser and no room.

## Steps

One branch (`feat/sandbox-authoring`), one commit per step, one pull request after approval.

- **SA1. The game runs a work package from a directory.** `--work --plugin DIR` loads and
  watches the package through the existing plugin host and its revisions instead of the
  compiled catalog entry; a stage, Undo or reload restarts the cargo session per SA0.1 and
  restages the job's `init`. `--plugin-bridge` works with it (inspect, stage, apply, undo,
  discard, console). Unknown until read: how far `WakeWork`'s static revision and
  `wake_plugin_*` can share one host; if the work session needs its own host, say so and
  size the step then. Tests: a virtual-clock bridge case beside `wake_velocity_bridge`, a
  restart case in the work-session tests, and an unchanged-behaviour check of plain `--work`.
- **SA2. The tools accept work packages.** `capabilities` reports `live.kinds` and the
  workspace kind; `init --from plugins/outer-work` writes instructions about sites, the job
  and the survey diagnostics instead of voyage shelters; `./wv play` starts `--work`;
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
