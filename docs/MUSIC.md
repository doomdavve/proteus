# Working on the music

The demo now plays the working [Below the surface XM](../soundtrack/alternate/fasttracker/v01/below-the-surface.xm)
at 120 BPM, speed 6. See its [tracker guide](../soundtrack/alternate/fasttracker/README.md).
Rebuild after editing it to refresh the bundled runtime copy. If a tracker revision uses
a new filename, update CMakeLists.txt's `proteus_assets` source path too; the runtime copy
keeps the name `below-the-surface.xm`. The four-minute waterfront
and flock camera hold at 4:00; the XM has a silent end hold. If the arrangement length or
section cues change, update `WATERFRONT_DEMO_DURATION` and the flock route cue times in
`src/waterfront_scene.c` / `src/waterfront_scene.h` alongside the audio.

The playback helper reconstructs the XM decoder from its source before starting or
seeking. Raylib's duration scan leaves a pending jump to the silent ending which its
partial decoder reset does not clear; reusing that state would play only the first
sound. The XM validation covers that loader sequence and backward seeking as well as
full-song decoding. Call the helper only while the audio stream is stopped.

## Boating background loop

The separate `boat_study` defaults to [A little boat](../assets/music/a-little-boat.xm): an
original, quiet C-major piece at 84 BPM / speed 6, 32 bars (91.43 seconds), repeating
from the beginning. Soft rounded keys, a humming melody, and a sine-led bass use simple
major/sixth/seventh harmony and sparse call-and-answer phrases. There are no drums,
intensity layers, or boat-dependent changes. Playback volume is 0.35; engine/water
remain independently mixed. Focus loss and paused replays pause the music.
The main demo's timed soundtrack and the dance study's starter remain separate.

Regenerate only this asset with `python3 tools/generate_boating_music.py`, then rebuild
to refresh the bundled copy. This reuses the starter generator's XM encoding helpers
without overwriting its samples or module. Edit the new module in FastTracker, or change
the dedicated generator (regeneration replaces tracker edits). CMake bundles it next
to the other music and `boating_xm_is_valid` checks decoder loading/reset, seeking,
finite unclipped samples, and continued sound across two loop boundaries.

### Acid Wake and soundtrack selection

Choose SETTINGS → SOUNDTRACK on the native title, or Soundtrack in the browser launcher's
Settings before PLAY. The choices are A LITTLE BOAT, ACID WAKE and OFF. Native settings
persist in `title.txt` and apply to both single player and multiplayer; the choice is local
to each listener. Direct command-line sessions accept `--soundtrack a-little-boat|acid-wake|off`
and default to A little boat. Off leaves the engine and water mix active.

[Acid Wake](../assets/music/acid-wake.xm) is an original 64-bar D-minor acid techno piece
at 132 BPM / speed 6, with a 116.36-second loop. A resonant saw/square bass uses sixteenth-note
phrases, slides, accents and changing cutoff brightness, with a four-on-the-floor kick,
clap, closed/open hats, a minor-ninth stab and a quiet dotted-eighth echo. Intro, groove,
breakdown, build and return are authored into the arrangement. This is TB-303-inspired
sound design using synthesized PCM, not a recording or a circuit-accurate emulator.

```sh
python3 tools/generate_acid_music.py
cmake --build build -j
./build/validate_xm assets/music/acid-wake.xm --acid
./build/boat_study --soundtrack acid-wake
```

The generator writes only `acid-wake.xm`; existing accepted music and sample WAVs are
untouched. Eight bar-long bass phrase samples preserve continuous slide/filter gestures;
edit their notes and synthesis in the generator, or rearrange phrases, drums and levels in
FastTracker. Regeneration replaces edits to this XM. `acid_xm_is_valid` checks fresh-load
start, seeking, finite unclipped PCM and two loop boundaries over 235 seconds. Native
packaging copies the asset directory; the browser build explicitly preloads both tracks.
Implementation and verification status belong to the
[boating plan](BOATING_PLAN.md#selectable-soundtracks--2026-10-24).

## Archived starter sketch

The notes below describe the original starter XM, retained for the separate dance study.

For the new composition's brief, milestones, and listening criteria, see the
[alternate soundtrack plan](SOUNDTRACK_PLAN.md).

`starter.xm` is a sketch, not precious source material. It is meant to give us
something audible and synchronized on day one.

## Arrangement

At 125 BPM and speed 6, four rows make one beat and 16 rows make one 4/4 bar.
Every pattern is four bars and lasts 7.68 seconds.

| Pattern | Role | Layers |
|---|---|---|
| 00 | Intro | Kick and sparse hat |
| 01 | Rhythm enters | Drums and bass |
| 02 | Harmony enters | Drums, bass, minor chord stabs |
| 03 | First peak | Full arrangement and lead |
| 04 | Breakdown | Drums and bass; no chords or lead |
| 05 | Rebuild | Full arrangement and lead |
| 06 | Lift | New root, full arrangement and lead |
| 07 | Ending/loop | Full arrangement and snare fill |

## Channels

| Channel | Purpose |
|---|---|
| 1 | Kick |
| 2 | Snare |
| 3 | Closed hi-hat |
| 4 | Bass |
| 5–7 | Three-note chord |
| 8 | Lead |

## First useful edits in FastTracker II Clone

1. Save the generated module under a new name before experimenting.
2. Solo channels to hear what each layer contributes.
3. Change the bass notes in channel 4, then move the chord notes by the same
   number of semitones.
4. Rewrite channel 8. A short, memorable motif is more valuable than many notes.
5. Replace instruments one at a time by loading WAV samples from
   `assets/samples` into their existing instrument slots.

The generator is deterministic. Running it again overwrites `starter.xm` and the
five generated WAV files, so keep the evolving composition under another name,
for example `proteus-theme.xm`. To select it for the dance study, update that tool's
`starter.xm` load/seek paths in `tools/dance_study.c` and ensure CMake bundles the file.
The main demo's separate working soundtrack is selected by the CMake copy described above.

## Making new samples without a sample library

Run `python3 tools/generate_audio.py` from the repository root. It uses only the
Python standard library. Check sample encoding with
`python3 -m unittest discover -s tools -p 'test_generate_audio.py'`, then run
`ctest --test-dir build --output-on-failure` to check playback through the XM decoder.

The drums in `make_drums()` are synthesized at 44.1 kHz and stored as 16-bit mono
PCM in both the WAVs and XM. Each has its own fixed noise seed:

- Kick: an exponential pitch sweep from 155 Hz to 49 Hz, a short second harmonic,
  soft saturation, and a filtered noise beater transient. Sweep time controls the
  punch; body decay controls the weight and length.
- Snare: two decaying drum-body modes plus bandpass-filtered wire noise and a short
  snap. Body/noise balance controls the tone; wire decay controls the rattle.
- Closed hi-hat: six inharmonic sine partials mixed with filtered noise, with a
  fast attack and two short decays. Filter frequency controls brightness and the
  slower decay controls how tightly the hat closes.

`finish_sample()` removes subsonic DC, fades the sample boundaries, and sets a
separate peak level for each instrument with headroom. XM tuning preserves the
drums' intended playback rate at C-4; its finite tuning resolution introduces a
small rounding error.

The bass in `make_bass()` also uses 44.1 kHz/16-bit mono PCM. A finite mix of 24
saw/pulse-like harmonics gives it a bright pluck, with a brightness cutoff falling
from 1800 Hz towards 260 Hz over a 65 ms decay. A separate fundamental decays more
slowly to retain low-end weight. The 2.5 ms attack and 45 ms tail fade soften the
sample boundaries. Change the harmonic weights for the tone, cutoff and its decay
for the pluck, or body decay for a longer bass. These times describe the source
sample; lower tracker notes stretch it during playback.

The lead/chord instrument in `make_synth()` is a 44.1 kHz/16-bit pluck with three
saw-like voices: a strong centre voice and two quieter voices detuned by ±5 cents.
Twenty harmonics per voice keep the spectrum controlled when the melody plays
high notes. Brightness falls from 2600 Hz towards 650 Hz with an 85 ms decay;
the amplitude decays over 160 ms. A 4 ms attack and 60 ms tail fade smooth the
boundaries of the 440 ms source sample. Detuning and voice gains control thickness;
the cutoff and harmonic weights control how sharply chords and lead cut through.
Higher tracker notes shorten the source envelope during playback.

Both melodic fundamentals compensate for the old samples' approximate XM tuning
so the existing bass line, chords, and lead keep their audible pitch. All five
instruments now use 44.1 kHz/16-bit PCM. Instrument slots, patterns, volumes, and
125 BPM/speed-6 timing are unchanged.

Audition `assets/samples/kick.wav`, `snare.wav`, and `hat.wav` individually, then
open `assets/music/starter.xm` in FastTracker and listen from 7.68 seconds for the
drums with bass and the later full arrangement. The main demo plays a different XM.
To compare revisions, keep a copy of the previous XM before regenerating, and solo
channels 1–3 in FastTracker II Clone.

For the bass, audition `assets/samples/bass.wav`, then solo channel 4 in the
tracker and listen with the drums at 7.68 seconds and the chords at 15.36 seconds.

For the synth, audition `assets/samples/synth.wav`, then listen to the starter XM
in FastTracker from 15.36 seconds for chords and 23.04 seconds for the full arrangement
with melody. Solo channels 5–7 for the chord voicing or
channel 8 for the lead in FastTracker II Clone.
