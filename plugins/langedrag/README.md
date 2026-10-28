# Långedrag voyage plugin

The reference plugin for [Lua plugin P0](../../docs/PLUGIN_PLAN.md): the outer location's
current voyage expressed as data. `validate_boat_plugin --case reference` checks that it
decodes to exactly the compiled `boat_voyage_outer`. The game does not load plugins yet; that
is P1. Fields, units and limits are described in [the plugin API](../../docs/PLUGIN_API.md).
