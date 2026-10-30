-- The outer location's Långedrag voyage, as compiled into boat_voyage_outer (src/boat_voyage.c).
-- Simulation metres: x = -east, z = north. Headings are radians; forward is (sin, -cos).
-- The first shelter is the start and the last is the destination's, reached only by arriving.
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
    -- Arrive inside this area at or below max_speed (m/s).
    arrival = { x = 3595, z = -3370, radius = 75, max_speed = 6 },
    -- Stationary rough water over the open fjord. It grows over `build` seconds once the boat
    -- comes within weather_range metres; inside exposed_fraction of its radius is exposed.
    exposure = { x = 4300, z = -2650, radius = 700, height = 1.6, build = 60 },
    exposed_fraction = 0.8,
    weather_range = 2500
  },
  -- The first editable marina slice: two moored hulls beside the existing pontoon. The entrance
  -- stays clear of both hulls and the approach lane runs north along x = 6.
  scene = {
    version = 1,
    marina = {
      entrance = { x = 6, z = 168, width = 8 },
      berths = {
        { id = "skiff", recipe = "skiff", x = 13, z = 167, heading = -1.5707964,
          mooring = "pontoon" },
        { id = "runabout", recipe = "runabout", x = 13, z = 173, heading = -1.5707964,
          mooring = "pontoon" }
      }
    }
  }
}
