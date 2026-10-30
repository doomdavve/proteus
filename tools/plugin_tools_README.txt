Wake Velocity plugin authoring tools

These are the same command-line tools the source checkout uses to author Lua voyage plugins:
plugin_author.py (Python 3.10 or newer, not included), the boat_plugin_worker helper it runs for
isolated validation and previews, and the plugin API reference, PLUGIN_API.md. They work with
this package's own wake_velocity; plugin_tools.json records where it and the helper are.

Copy an example somewhere writable before editing it. The examples are in the package's
assets/plugins directory: langedrag and shelter-tour are voyages you can play with --plugin;
river-sprint is a river race description that these tools can validate and preview only.

Replace AUTHORING below with this directory and EXAMPLES with assets/plugins.
On Windows use "py" or "python" instead of python3, and wake_velocity.exe.

  python3 AUTHORING/plugin_author.py capabilities
  python3 AUTHORING/plugin_author.py validate EXAMPLES/langedrag
  python3 AUTHORING/plugin_author.py preview MY-PLUGIN --ticks 360
  python3 AUTHORING/plugin_author.py capture MY-PLUGIN --anchor outer.dock --output frame.png

Play and live-edit a copied voyage with the game: wake_velocity --plugin MY-PLUGIN
Saving the files reloads it; see PLUGIN_API.md for F5/F6/F7, the live tool bridge
(--plugin-bridge DIR with the live-* operations) and every other operation.

Plugins run trusted Lua: the helper and the game give it no file, OS or network access, but
this is not a security sandbox. Only run plugins you trust.
