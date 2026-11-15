-- M4 marina vessel services example on the outer location's Långedrag voyage, equal to the fixture in tests/support/boat_fixtures.c.
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
  -- Two slips, two vessels, one shared clock (120 ticks per second).
  --   resident "tender": moored at "home" until 5 s, sails a racetrack, moored again from 25 s.
  --   visitor "guest": absent until 2 s, arrives, docks at "slip" (dwell 14-16 s), leaves, absent from 21 s.
  -- A berth is the single source of the terminal pose: the path must start/end (resident) or
  -- the dock piece must end (visitor) at the berth pose, or validation prints the expected pose.
  scene = { version = 3, marina = {
    entrance = { x = 0, z = 170, width = 10 },
    berths = {
      { id = "home", recipe = "runabout", mooring = "none", x = -16, z = 195,
        heading = 1.5707963267948966, empty = true },
      { id = "slip", recipe = "runabout", mooring = "none", x = 12, z = 195,
        heading = 1.5707963267948966, empty = true }
    },
    modules = {
      { id = "home_dock", kind = "platform", x = -16, z = 203, heading = 0,
        length = 6, width = 6, deck_height = .74 },
      { id = "dock", kind = "platform", x = 12, z = 203, heading = 0,
        length = 6, width = 6, deck_height = .74 }
    },
    vessels = {
      { id = "tender", recipe = "runabout", appearance = { name = "TENDER", hull = {200, 60, 40}, deck = {210, 80, 60} } },
      { id = "guest", recipe = "runabout", appearance = { name = "GUEST", hull = {40, 90, 200}, deck = {60, 110, 220} } }
    },
    services = {
      { id = "outing", vessel = "tender", berth = "home", route = "racetrack",
        kind = "resident", start_tick = 600 },
      { id = "stay", vessel = "guest", berth = "slip", route = "visit",
        kind = "visitor", dock_piece = 3, start_tick = 240 }
    }
  }, traffic = { vessels = {
    { id = "racetrack", recipe = "runabout", route = "open", pieces = {
      {kind="line",x0=-16,z0=195,x1=-6,z1=195,ticks=480,dwell=0,max_speed=5,max_acceleration=5},
      {kind="arc",x=-6,z=189,radius=6,start_angle=1.5707963267948966,sweep=-3.141592653589793,ticks=720,dwell=0,max_speed=5,max_acceleration=7},
      {kind="line",x0=-6,z0=183,x1=-16,z1=183,ticks=480,dwell=0,max_speed=5,max_acceleration=7},
      {kind="arc",x=-16,z=189,radius=6,start_angle=-1.5707963267948966,sweep=-3.141592653589793,ticks=720,dwell=0,max_speed=5,max_acceleration=7}
    } },
    { id = "visit", recipe = "runabout", route = "open", pieces = {
      {kind="line",x0=0,z0=170,x1=0,z1=185,ticks=600,dwell=0,max_speed=5,max_acceleration=5},
      {kind="arc",x=10,z=185,radius=10,start_angle=3.141592653589793,sweep=-1.570796326794897,ticks=600,dwell=0,max_speed=5,max_acceleration=7},
      {kind="line",x0=10,z0=195,x1=12,z1=195,ticks=240,dwell=240,max_speed=5,max_acceleration=7},
      {kind="arc",x=12,z=185,radius=10,start_angle=1.570796326794897,sweep=-1.570796326794897,ticks=600,dwell=0,max_speed=5,max_acceleration=7}
    } }
  } } }
}
