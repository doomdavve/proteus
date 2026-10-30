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
  -- The first editable marina slice: two moored hulls beside a small authored pontoon. The entrance
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
      },
      modules = {
        { id = "pier", kind = "pontoon_small", x = 20, z = 170, heading = 0 },
        { id = "ramp", kind = "gangway", anchor = { x = 34.1, z = 170 },
          attaches = "pier" }
      }
    },
    -- A small craft loops just north of the marina every ~27 s. A present vessel list replaces
    -- all compiled traffic, so the compiled river runabout is retained explicitly.
    traffic = {
      vessels = {
        { id = "passing", recipe = "runabout", route = "marina_pass",
          speed = 4, phase = 0, period = 24.8540 },
        -- The compiled Älvsnabben loop, with its Stenpiren dwell extended. The two later
        -- stops keep the shared remainder of the 1,200 s timetable.
        { id = "ferry", retain = "alvsnabben",
          stops = {
            { id = "stenpiren", dwell = 35 },
            { id = "lindholmspiren" },
            { id = "eriksberg" }
          } },
        { id = "river_runabout", retain = "river_runabout" },
        -- The compiled schooner keeps its Hisingsbron passage and lift dependency.
        { id = "schooner", retain = "schooner" }
      }
    },
    -- One flock wheels just north of the slips, with a feeding pass early in the first
    -- minute and a short alarm climb afterwards. Cue ticks use the 120 Hz session clock.
    gulls = {
      flocks = {
        { id = "harbor", recipe = "gull", x = -4, z = 182, count = 8,
          seed = 3, phase = 0, radius = 10, height = 6, rate = 0.3,
          cues = {
            { kind = "feeding", start = 240, stop = 600, fade = 60, amplitude = 0.8 },
            { kind = "alarm", start = 900, stop = 1200, fade = 80, amplitude = 1.0 }
          } }
      }
    }
  }
}
