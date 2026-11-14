-- The outer location's Långedrag voyage, equal to the fixture in tools/boat_fixtures.c.
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
  scene = { version = 3, marina = { berths = {},
    modules = { { id = "dock", kind = "platform", x = 12, z = 145, heading = 0,
      length = 6, width = 6, deck_height = .74 } }
  }, traffic = { vessels = {
    { id = "visitor", recipe = "runabout", route = "open", pieces = {
      {kind="line",x0=0,z0=110,x1=0,z1=125,ticks=600,dwell=0,max_speed=5,max_acceleration=5},
      {kind="arc",x=10,z=125,radius=10,start_angle=3.141592653589793,sweep=-1.570796326794897,ticks=600,dwell=0,max_speed=5,max_acceleration=7},
      {kind="line",x0=10,z0=135,x1=12,z1=135,ticks=240,dwell=240,max_speed=5,max_acceleration=7},
      {kind="arc",x=12,z=125,radius=10,start_angle=1.570796326794897,sweep=-1.570796326794897,ticks=600,dwell=0,max_speed=5,max_acceleration=7}
    } },
    { id = "river_runabout", retain = "river_runabout" },
    { id = "river_ferry", retain = "river_ferry" },
    { id = "alvsnabben_a", retain = "alvsnabben_a" },
    { id = "alvsnabben_b", retain = "alvsnabben_b" },
    { id = "frihamnen", retain = "frihamnen" },
    { id = "schooner", retain = "schooner" }
  } } }
}
