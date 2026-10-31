# River sprint plugin

The current seven-gate river sprint as a bounded Lua descriptor. Physics, swept crossings,
places, recovery and deadlines remain in C. See the [plugin API](../../docs/PLUGIN_API.md#river-race-authoring-p2).

From the repository root after building:

```sh
./build/boat_race_trial --plugin plugins/river-sprint --play
./build/boat_race_trial --plugin plugins/river-sprint --renderer cpu --output build/race.png --time 18
./build/validate_boat_plugin_race
ctest --test-dir build -R 'boat_race_plugin|boat_plugin_race' --output-on-failure
```

Omit `--play` for scripted steering. Copy the directory to edit it. Saves reload once settled;
F5 reloads explicitly, F6 undoes or discards a held candidate, and F7 installs gameplay changes
with a new run (boat, clock, water and wakes reset). A manifest `name` edit applies immediately.
Invalid edits retain the current revision and print a file/line or field diagnostic.

The preview spawns at the first `grid` position, facing `start.dx`/`start.dz`. Edit
`grid[1].x` and `grid[1].z` to move the boat independently of the start-line buoys.
Save and press F7 to restart at the new position. Staging checks shore clearance and
separation from the other three grid slots.
Undo changes the active definition, not the files on disk; F5 stages the disk files again.

Coordinates are simulation metres (`x = -east`, `z = north`), directions use named `dx`/`dz`,
speeds are m/s, source/storm times are seconds relative to GO, and rules use integer 120 Hz
ticks. All fields are required; unknown fields and sparse lists are rejected.

This is a local course/source preview. It displays and drives the authored gates, ferry and
storm; it does not run a multiplayer round or display standings/countdowns. The headless race
fixture tests the authored rules with the native race engine. Production rooms use this package's
validated definition as data, and browser clients receive the room definition without evaluating
the package.
