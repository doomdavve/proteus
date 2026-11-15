-- M5 integrated marina example: the shared twelve-hull Eriksberg marina with a guest dock, a
-- resident's round trip and a visitor's stay, built from package-local modules.
--   marina           copies the shared marina (modules/eriksberg_marina.lua) and adds the guest dock
--   guests.routes    the two authored paths (traffic vessels) and their geometry
--   guests.services  the two vessels and their one-shot services, referring to those paths
-- Simulation metres: x = -east, z = north. Headings are radians; forward is (sin, -cos).
local scene = require("marina")
local routes = require("guests.routes")
local fleet = require("guests.services")

-- Authored paths join the marina's traffic after the passing craft, ferry, runabout and schooner.
for _, path in ipairs(routes.paths) do
    table.insert(scene.traffic.vessels, path)
end
scene.marina.vessels = fleet.vessels
scene.marina.services = fleet.services

return {
  voyage = {
    shelters = {
      { id = "dock", name = "DOCK", x = -12, z = 60, heading = 0, radius = 60 },
      { id = "bridge", name = "BRIDGE", x = 650, z = -850, heading = 1.33, radius = 150 },
      { id = "east_shore", name = "EAST SHORE", x = 3250, z = -2000,
        heading = 0.86, radius = 200 },
      { id = "langedrag", name = "LANGEDRAG", x = 3600, z = -3370,
        heading = 1.57, radius = 0 } -- Facing out of the basin.
    },
    arrival = { x = 3595, z = -3370, radius = 75, max_speed = 6 },
    exposure = { x = 4300, z = -2650, radius = 700, height = 1.6, build = 60 },
    exposed_fraction = 0.8,
    weather_range = 2500
  },
  scene = scene
}
