# Shelter hunt (rooms)

A free Gothenburg Harbour package for a multiplayer room: five marks along the river, an objective
that counts each crew's marks, shows the first crew to find them all, and starts a new hunt once
every crew present has them. The objective runs only on the room's authority; clients receive its
HUD as data ([P5 plan](../../docs/PLUGIN_P5_PLAN.md)).

It is room-only: `--plugin` and Single Player refuse it. Host it with
`./build/boat_server --location outer --package proteus.outer-hunt`; the server prints the HUD
(`objective tick N: ...`) whenever it changes; the game draws the marks and HUD it receives.
`boat_server --package-reload` re-reads the directory every second; an edit to `main.lua` then reaches
the room's players at the next objective boundary and keeps their marks, because the script has a
`migrate` ([reload](../../docs/PLUGIN_API.md#room-objective-reload-p5e)). Moving a mark is a
definition revision that a free room commits when it has no players, and the hunt then restarts
([revisions](../../docs/PLUGIN_API.md#definition-revisions-from-reload-p5f)).
See [marks](../../docs/PLUGIN_API.md#marks-p5a) for the schema and events.
