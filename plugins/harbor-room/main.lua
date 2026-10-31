-- The Långedrag marina as a free outer room. The scene is shared with Harbor life, so a
-- room and the solo voyage place the same hulls. No voyage route applies in a room.
return {
  free = {
    version = 1,
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
}
