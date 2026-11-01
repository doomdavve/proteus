-- Current C river sprint, in simulation metres (x = -east, z = north).
-- Directions are approximately unit length, exactly as in the compiled fixture.
return {
  race = {
    start = { x = -180, z = -250, dx = .786, dz = -.618, half_width = 40 },
    gates = {
      { x = -72.3, z = -376.6, dx = .830, dz = -.558, half_width = 18 },
      { x = 99.7, z = -371.8, dx = .830, dz = -.558, half_width = 18 },
      { x = 146.2, z = -535.5, dx = .830, dz = -.558, half_width = 16 },
      { x = 315.3, z = -516.9, dx = .830, dz = -.558, half_width = 16 },
      { x = 375.9, z = -678.0, dx = .830, dz = -.558, half_width = 18 },
      { x = 520.1, z = -678.5, dx = .830, dz = -.558, half_width = 20 },
      { x = 588.5, z = -767.2, dx = .767, dz = -.641, half_width = 158 }
    },
    grid = {
      { x = -202.4, z = -259.1 }, { x = -193.8, z = -248.1 },
      { x = -185.1, z = -237.1 }, { x = -176.5, z = -226.1 }
    },
    source = {
      x = -118.966, z = -498.230, dx = .617822, dz = .786318,
      started = -12, speed = 12, height = 3, duration = 24, lifetime = 40,
      wavelength = 8, spread = 3, half_width = 14
    },
    storm = {
      x = 46.2, z = -399.12, vx = 6.64, vz = -4.464,
      radius = 140, height = 1.6, started = 0, build = 0
    },
    -- Integer 120 Hz ticks. No script runs during the race.
    rules = {
      grid_ticks = 120, countdown_ticks = 360, results_ticks = 960,
      after_winner_ticks = 1800, cap_ticks = 10800, recovery_ticks = 240,
      skip_ticks = 60, park_ticks = 240, min_players = 2
    },
    -- A moored obstacle behind the start line, clear of every gate and recovery point. A room
    -- with this scene must carry it to clients as data.
    scene = {
      version = 2,
      marina = {
        entrance = { x = -117, z = -192, width = 8 },
        berths = {
          { id = "obstacle", recipe = "workboat", x = -117, z = -199, heading = 0,
            mooring = "pontoon" }
        },
        modules = {
          { id = "pier", kind = "platform", length = 14, width = 2.35, deck_height = .74, x = -110, z = -199, heading = 0 }
        }
      }
    }
  }
}
