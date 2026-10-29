# Långedrag voyage plugin

The reference plugin for [Lua plugin P0](../../docs/PLUGIN_PLAN.md): the outer location's
current voyage expressed as data. `validate_boat_plugin --case reference` checks that it
decodes to exactly the compiled `boat_voyage_outer`. Play it, and edit it while playing, with
`./build/wake_velocity --plugin plugins/langedrag` (native builds; Lua plugin P1). Fields, units,
limits, keys and reload rules are described in [the plugin API](../../docs/PLUGIN_API.md).
