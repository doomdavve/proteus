# Optional shelter tour

Run from the repository root:

```sh
./build/wake_velocity --plugin plugins/shelter-tour
```

Visit BRIDGE and EAST SHORE before arriving at LÅNGEDRAG. The native panel counts unique
intermediate shelter visits and reports TOUR COMPLETE or TOUR MISSED on arrival. The native
voyage can always finish independently. Recovery does not add a visit.

Save `main.lua` or press F5 to stage an edit. Behavior source edits require F7 to restart the
voyage and objective in this example, which has no migration callback. A candidate with
`migrate` can instead preserve native progress; see the
[migration/review contract](../../docs/PLUGIN_API.md#live-migration-and-review-p33b).
F6 discards a held edit or offers Undo under the destination revision's migration/restart policy.
Manifest-only renames keep state. Callback errors suspend this optional objective while native
boating continues. An accepted voyage restart clears the state and error.

See the [P3.2 API](../../docs/PLUGIN_API.md#live-optional-voyage-objective-p32) for event,
transaction, ownership and limit details. This experimental example ships in the source tree and native authoring packages.
For the browser, export and import it through the [voyage panel](../../docs/PLUGIN_API.md#browser-source-bundles-p43):

```sh
python3 tools/plugin_author.py export plugins/shelter-tour > build/shelter-tour.wake-plugin.json
```

Inspect a deterministic native checkpoint and seek without a window:

```sh
./build/boat_plugin_review plugins/shelter-tour 120 360
```
