Wake Velocity plugin authoring tools

These are the same command-line tools the source checkout uses to author Lua voyage plugins:
plugin_author.py (Python 3.10 or newer, not included), the boat_plugin_worker helper it runs for
isolated validation and previews, and the plugin API reference, PLUGIN_API.md. They work with
this package's own wake_velocity; plugin_tools.json records where it and the helper are.

Copy an example somewhere writable before editing it. The examples are in the package's
assets/plugins directory: langedrag and shelter-tour are voyages you can play with --plugin;
river-sprint is a river race description on the outer map that these tools can validate and preview only.

Replace AUTHORING below with this directory and EXAMPLES with assets/plugins.
On Windows use "py" or "python" instead of python3, and wake_velocity.exe.

  python3 AUTHORING/plugin_author.py capabilities
  python3 AUTHORING/plugin_author.py validate EXAMPLES/langedrag
  python3 AUTHORING/plugin_author.py preview MY-PLUGIN --ticks 360
  python3 AUTHORING/plugin_author.py capture MY-PLUGIN --anchor outer.dock --output frame.png

Play and live-edit a copied voyage with the game: wake_velocity --plugin MY-PLUGIN
Saving the files reloads it; see PLUGIN_API.md for F5/F6/F7, the live tool bridge
(--plugin-bridge DIR with the live-* operations) and every other operation.

Agent workspace: to let a coding agent (for example Claude Code) author a voyage, scaffold a directory
and open both the game and the agent in it:

  python3 AUTHORING/plugin_author.py init MY-WORKSPACE [--example NAME] [--bridge DIR]
  cd MY-WORKSPACE && ./wv play          (the game, with the live bridge)
  cd MY-WORKSPACE && claude             (the agent, in a second terminal)

The workspace carries instructions for the agent, a permission file and the ./wv wrapper
(check, stage, apply, undo, discard, status, console); see PLUGIN_API.md, "Agent workspaces".

A package may split its Lua into modules: list them in plugin.json ("modules": ["route",
"route.arrival"]) and put them under MY-PLUGIN/modules (route.arrival is modules/route/arrival.lua).
validate, preview, capture and workspace-read/write all carry them, and the entry and modules
share the 64 KiB source limit. To move a package to or from the browser editor:

  python3 AUTHORING/plugin_author.py export MY-PLUGIN > my.wake-plugin.json    (version 2 bundle)
  python3 AUTHORING/plugin_author.py import NEW-DIRECTORY < my.wake-plugin.json

Version 1 bundles are not supported; export them again from a current build.

In a source checkout, packages that share a module (harbor-life and harbor-room carry
modules/eriksberg_marina.lua) keep a byte-identical copy of plugins/_shared/<name>.lua:

  python3 AUTHORING/plugin_author.py sync-shared [PLUGINS-DIRECTORY]          (rewrite the copies)
  python3 AUTHORING/plugin_author.py sync-shared --check [PLUGINS-DIRECTORY]  (report drift, exit 1)

Plugins run trusted Lua: the helper and the game give it no file, OS or network access, but
this is not a security sandbox. Only run plugins you trust.
