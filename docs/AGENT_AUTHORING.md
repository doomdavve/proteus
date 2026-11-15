# Authoring plugins with an external agent

Status, 2026-11-15: **implemented, not yet re-tried.** This is the replacement for the in-game agent
of [P6](PLUGIN_PLAN.md#p6--external-agent-authoring-instead-of-an-in-game-agent): no provider code in
the game, a coding agent such as Claude Code drives the P4 tools. Three Claude Code sessions ran an
earlier, manual version of the loop (see the [trial](#trial)); the workspace below was built from
their friction and has been checked by automated tests only. No human has played an agent-made change,
so nothing is accepted. [The plugin API](PLUGIN_API.md#agent-workspaces) owns the commands and files.

## Quick start

Native builds only; from the repository root with a built `./build/wake_velocity`:

```sh
python3 tools/plugin_author.py init ~/my-voyage --from langedrag  # a voyage plugin plus the agent's workspace
cd ~/my-voyage && ./wv play                         # terminal 1: the game, with the live bridge
cd ~/my-voyage && claude                            # terminal 2: the agent, scoped to this directory
```

`init` copies the package named by the required `--from NAME` (a shipped example such as `langedrag`) or `--from DIRECTORY`, the agent's instructions
(`AGENTS.md`, imported by `CLAUDE.md`), a Claude Code permission file, a copy of the API reference and
the `./wv` wrapper. A release package has the same tools in its `authoring/` directory. The agent
opens in that directory and sees the plugin, not the game's source.

Check that `ls ~/.wake-velocity-bridge` lists `session` before you start the agent. The game writes it
at launch and deletes it on exit, so a second game on one bridge directory, or an older one closing
later, leaves a running game with no bridge; `./wv play` refuses to start while a live game holds it and
the agent's commands answer `no_game`.

Suggested first prompt:

> Move one of the four shelters to a nearby spot. Run `./wv check`, then `./wv stage`, tell me what
> the game reported, then `./wv undo` and confirm the previous digest is back.

(`langedrag` has the maximum of four shelters, so asking for a fifth is a good test of the gap report.)

## What changed from the manual trial

The manual loop needed an agent to edit a copy, take pins from `live-inspect` and remember not to read
the bridge. Each item became part of the workspace:

- **Saves are held while a bridge is attached** (a game option, not a convention). The agent edits
  `main.lua` in place and nothing happens until `./wv stage`; before, the game applied saved edits on its
  own and `live-stage` found nothing to do.
- **Pins come from the game.** `./wv stage`, `apply`, `undo`, `discard` and `console` inspect first and
  pin to what they read, so there is no guessed revision, epoch or held revision.
- **The bridge is out of the agent's reach.** The permission file denies reading the bridge directory, so
  the token stays out of the transcript; `./wv` reads it.
- **One scope.** The agent is launched in the plugin directory with its instructions and reference
  there, so it needs no access to the game's source tree, and release-package users need none either.

## The loop

The agent's own instructions are the workspace's `AGENTS.md`; in short: `./wv check` (validate, preview,
optional capture), `./wv stage`, then `./wv apply` for a held change or `./wv undo`, with `./wv status`
for the revision, epoch, digest and boat/objective state. A change that moves the water or needs a
migration is held, and `stage` says so in `next`; a moved mark or route goes live at once. Every
mutation goes through the same staging, revision and Undo a human uses, so the agent cannot move the
boat, run Lua in the tick or write to the live world.

## Limits

- Voyage plugins in `wake_velocity` only: no race trial, browser or room editing, one bridge client at
  a time.
- The permission rules and instructions are hygiene, not a sandbox; plugin Lua is trusted and the agent
  can do what the user's session allows. The permission syntax (`Read(//abs/**)`, `Bash(./wv stage:*)`)
  has not been checked against a running Claude Code.
- A still cannot show a small move of a mark, route or timer; digests and `./wv status` do.
- There is no in-game transcript; the agent's own session and the revision history are the record.
- Windows runs the wrapper as `py wv COMMAND`; that path has not been run.

## Trial

Results 1-3 below used the manual loop (copy the plugin, `plugin_author.py live-*` with explicit
pins, `tools/agent_trial.sh`, which no longer exists). They explain the workspace; they do not show it
works.

**Results, 2026-11-15, native Debug on macOS, Claude Code, `langedrag`.** All figures are the
agent's own report of its commands; none was independently checked and no one played the result.

- Session 1 (add a fifth shelter): the agent found the contract unaided, validated a scratch copy,
  quoted `voyage.shelters: must list 2 to 4 shelters, not 5`, named the C limit as the work item and
  staged nothing. Item 5 passed; the request was impossible because `langedrag` is at the cap.
- Session 2 (move a shelter, editing the watched directory): polling applied the edit before the
  agent staged it, so `live-stage` reported `unchanged`; a guessed `--held-revision` returned
  `stale`; undo restored digest `e8509272172cb7c4`. This produced the working-copy rule above.
- Session 3 (move `bridge` from (650, -850) to (620, -820), working copy): `validate`, `preview` and
  `capture` returned ok; `live-inspect` showed R1, epoch 0; `live-stage` with pins (1, 0, bundle)
  returned outcome `applied` ("R2 ACTIVE, live"), digest `aa419f3d6700b4de`, epoch 1;
  `live-apply` was correctly skipped (`held_revision` 0); `live-undo` with pins (2, 1) applied
  at once and a final `live-inspect` showed R1, digest `e8509272172cb7c4`, epoch 2. The watched
  directory stayed untouched. Before it, a game with no bridge session (see Setup) made
  `live-inspect` report `input_error`; the agent stopped and asked.
- Friction: a still cannot show a 30-40 m mark move; auto-applied saves in the watched directory;
  one bridge directory serves one game. Not tried: a change that is held (migration or restart),
  a stale pin from a concurrent edit, and the poll delay was not measured.
- Owed from the user: play R2 (the moved shelter) and judge it, then run Undo in the window.

The checks a trial covers; items 1-5 passed on the manual loop, item 6 is the friction list.

1. From a copy of `langedrag`, ask for a small change (for example a second objective or a moved
   mark; the [quick start](#quick-start) has a prompt). Record whether the agent finds the contract unaided or needed hints.
2. The change passes `validate`, `preview` and `capture` before it is staged.
3. `live-stage` and `live-apply` succeed on the first try with correct pins, or the failure mode
   (stale pin, busy, rejected) is recorded.
4. `live-undo` restores the previous behaviour.
5. A request outside the API produces a gap report, not an invented field.
6. Record friction: missing tool output, awkward pins, polling delay, anything the agent could not
   do that an in-game agent could.

Only a trial result decides whether a skill or wrapper for these commands is worth adding.

To re-run with the workspace: `init`, `./wv play`, `claude`, and walk the same checks. In addition to
what is owed above, record whether a held change (a water edit) goes through `./wv apply`, whether a
stale pin (edit the game with F5 in between) is reported, and whether the agent stays inside the
workspace.
