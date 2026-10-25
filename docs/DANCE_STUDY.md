# Dance study

Run the house-inspired shuffle study from the repository root:

```sh
./build/dance_study
```

It loads [house.dance](../assets/dance/house.dance): an eight-bar sequence with half-beat
shuffle bursts, one-beat side steps, and four-beat arm gestures. Footwork carries its ending
stance into the next step, with deliberate recoveries between bursts. The torso keeps its
slower groove while the arms counter-swing, sweep outward, then pull toward the ribs and
release. The second half uses a stronger groove and more expansive gestures.
The starter XM supplies the 125 BPM clock.
This is a simplified procedural interpretation, not a reconstruction of the reference video
or an authoritative implementation of house dance technique. This study uses the original
starter track.

Compare the previous relaxed shuffle with
`--choreography assets/dance/house-groove.dance`. The cross-and-reach study remains available
with `--classic`.

## Edit the choreography

Open `assets/dance/house.dance`, edit it, save, then press **L** in the study. A successful
reload restarts movement and music while preserving pause state. An invalid edit prints a
file/line diagnostic in the terminal, displays a failure message, and keeps the previous
choreography running. No rebuild is needed.

From the repository root, the default command uses the source file. From other working
directories it uses the copy under `build/assets`. To choose a file explicitly:

```sh
./build/dance_study --choreography assets/dance/house.dance
```

The first language is deliberately small. One command per line; `#` starts a comment.

```text
phrase burst
    groove jack 0.8
    # Pattern, lead, amount, delay in beats, span in beats
    arms sweep left 0.9 0.14 4
    # Move       Lead   Travel  Lift   Beats Join  (distances in metres)
    shuffle      left   0.12    0.05   0.5   flow
    shuffle      right  0.12    0.05   0.5   flow
    side_step    left   0.16    0.07   1     flow
    recover 2
end

play burst 4
```

| Command | Meaning |
|---|---|
| `phrase NAME` | Begin a reusable phrase with a unique name (up to 31 bytes, no spaces). |
| `groove jack AMOUNT` | Set torso/knee groove from 0 to 1; required once before the moves. |
| `arms counter\|sweep\|pull left\|right AMOUNT DELAY [SPAN]` | Select arm pattern, leading side, size (0–1), delay (0–0.25 beats), and gesture span (2 or 4 beats; default 2). |
| `shuffle left\|right TRAVEL LIFT [BEATS [reset\|flow]]` | Lift/place/slide pattern, with the named foot leading. Defaults to 2 beats and reset. |
| `side_step left\|right TRAVEL LIFT [BEATS [reset\|flow]]` | Step outward and transfer weight. Defaults to 2 beats and reset. |
| `recover BEATS` | Return from flow to neutral stance over 1 or 2 beats. |
| `end` | Finish the phrase. |
| `play NAME REPEATS` | Append 1–32 repetitions of a previously defined phrase. |

Travel accepts 0.05–0.30 metres; lift accepts 0.04–0.18 metres. Step duration accepts 0.5, 1,
2 or 4 beats. Fast steps (under 2 beats) require `flow`, with travel limited to `0.30 * BEATS`
and lift to `0.12 * BEATS`: half-beat steps allow at most 0.15 m travel and 0.06 m lift.
Flow side steps additionally limit travel to 0.20 m for leg reach. Each phrase must contain
1–32 moves totaling whole four-beat bars. Up to eight phrases and 256 performed moves are supported. The
maximum performance is 128 beats, matching the full starter track; the supplied file uses
32 beats (15.36 seconds). Put all phrase definitions before the `play` commands.

`reset` steps start and finish in neutral stance, preserving the older language's behavior.
`flow` steps retain the finishing foot positions and supporting side. The next flow step
must lead with the other foot; `recover` brings both feet home before a reset step. Phrases
may carry stance into the next phrase, but the complete performance must finish neutral so
the loop joins smoothly. Invalid support changes are rejected on load.

The groove continues through joins; intensity changes blend over the first quarter beat.
Fast steps use smaller weight shifts while the body roll keeps its two-beat clock. This is
authored continuity, without momentum simulation. Turns, arbitrary body poses and travel
across the stage remain future extensions.

## Upper-body phrasing

`arms` can appear before or between footwork commands. Its settings apply to subsequent
moves until the next `arms` command, and reset at each phrase definition. Older files without
it use `arms counter left 0.6 0.12`. The gesture repeats on its own two- or four-beat clock
starting at the cue. One four-beat sweep can span eight half-beat steps. Gestures return to
the counter-swing at each span boundary; a new cue must wait for a sweep or pull to finish.
The final gesture must also finish at the performance loop. Arms add no beats and do not
alter the foot paths.

- **counter:** arms respond to the weight transfer, with elbows bending and releasing. The
  leading side selects which arm swings forward first.
- **sweep:** the selected arm opens outward as the chest turns; the other folds toward the ribs.
- **pull:** the selected arm first reaches forward, pulls back toward the ribs, then releases.
  The other arm echoes the movement more softly.

The pelvis position still comes from footwork. A waist joint divides the lower torso into
18 cm and 21 cm spine segments, followed by the existing 20 cm chest-to-neck segment. The
lower back starts a slow two-beat body roll; the ribcage bends in the opposite direction with
a small delay. Side bends and ribcage turns add shape to the silhouette while the head follows
with a smaller tilt. Shoulders and arms are carried by the ribcage orientation. All three
spine segments keep their lengths; the torso changes shape rather than stretching.

The roll uses the existing `groove jack` amount and does not require another file command.
The classic study keeps its original straight torso by placing the waist on the old spine line.
Weight transfers use wider timing windows and ease in/out with
zero acceleration at the ends; a restrained vertical bob keeps the upper body from bouncing
sharply at every beat. Gesture preparation starts in the torso, the upper arm follows by
`DELAY` beats, and the forearm follows another 0.12 beats later. Gesture timing compresses
slightly to finish within the gesture span, keeping joins smooth. Counter-swing setting changes blend
over 0.3 beats, including at phrase and loop boundaries.

`AMOUNT` controls the arm movement and its additional torso accent; zero leaves the underlying
jack and weight transfer running. `DELAY` changes upper-body timing without moving musical
steps. Neither setting affects the feet, pelvis trajectory, or contact states. Evaluation uses
time-offset samples of the score, not physics or random motion, so scrubbing stays repeatable.

## Foot contact and rendering

The 19-joint figure uses fixed limb lengths and analytic two-bone leg IK. Each foot is
explicitly **planted**, **sliding**, or **airborne**. A planted foot must remain stationary;
a sliding foot stays on the floor while moving along an authored path. There is no physics
or friction simulation. The floor markers show these states: grey for planted, orange for
sliding, dark for airborne. This makes intentional slides distinguishable from accidental
foot drift.

The existing CPU renderer draws octagonal sticks at 320x200 with nearest-neighbour 3x
presentation. Cyan/orange limbs distinguish the two sides. The default camera is fixed
at three-quarter view; use the side view to inspect the forward/back shuffle and torso groove.

## Controls and captures

- `Space`: pause/resume movement and music.
- `R`: restart, preserving pause state.
- `L`: reload the selected choreography file and restart; unused in classic mode.
- `V`: toggle three-quarter/side view.
- `S`: normal/half speed; half speed also lowers music pitch.
- `Left` / `Right`: pause and step backward/forward one beat, wrapping at the ends.
- `Esc`: close.

The display shows the active phrase, move, bar, and beat within the bar.

```sh
./build/dance_study --slow --side
./build/dance_study --classic
./build/dance_study --choreography assets/dance/house-groove.dance
./build/dance_study --time 0.28 --screenshot build/house-shuffle.png
./build/dance_study --time 8.40 --screenshot build/house-side-step.png
./build/dance_study --time 0.70 --side --screenshot build/house-side-view.png
./build/dance_study --time 2.45 --screenshot build/arms-sweep.png
./build/dance_study --time 10.30 --screenshot build/arms-pull.png
./build/dance_study --time 0.24 --side --screenshot build/waist-forward.png
./build/dance_study --time 0.72 --side --screenshot build/waist-back.png
```

Screenshots use the same deterministic evaluator without a window or audio device. `--time`
is seconds and wraps within the loaded performance. `--silent` skips audio. `--frames 1020`
provides a bounded interactive smoke check covering the default performance's loop.

Audio uses the existing XM seek helper and a monotonic animation clock, restarting the
excerpt at the loop boundary. It is not sample-accurate; output-buffer latency can offset
audible accents. Missing music/audio devices are reported and playback continues silently.

## Implementation and checks

`src/choreography.c` parses and expands the small text score. `src/dance.c` evaluates the
score's moves, solves the skeleton and renders it. `tools/dance_study.c` handles playback
and reloads. Evaluation depends only on time and the score; seeking requires no simulation
history. The original authored pose table is retained for `--classic`.

CTest checks both studies for fixed limb lengths, stationary planted feet, floor clearance,
motion continuity and repeatable seeking. Language tests check phrase composition, malformed
input, preservation of the old score after a failed reload, and the move parameter limits.
Flow checks cover compiled beat positions, carried support, fast-step limits, and continuity
across step and loop boundaries. The relaxed score remains a separate constraint test.
Arm checks cover both leads, all gestures, amount/delay limits, changing gestures at joins,
fixed arm/spine lengths, and unchanged leg positions and contact states when only arms change.
They also verify that the waist actually bends, preventing a regression to collinear spine segments.
The house study also limits sampled pelvis speed and acceleration to guard against abrupt
direction changes; these are presentation checks, not biomechanical guarantees.
The classic study also retains its held-body/head-turn checks. The screenshot test exercises
the house rendering path. These checks do not establish artistic quality or physical balance;
review the movement in motion as well.
