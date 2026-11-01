-- The shipped deathmatch rules. This package adds no scene, so the room's --location picks the
-- map. The catalog equality test keeps these values equal to tools/boat_fixtures.c.
return {
  match = {
    version = 1,
    rules = {
      round_ticks = 36000, intermission_ticks = 1200, credit_ticks = 600,
      grace_ticks = 240, respawn_ticks = 360, skip_ticks = 120,
      contact_impulse = 1.5, wake_height = .35, min_players = 2
    },
    combat = {
      health = 100, damage = 0, bullet_speed = 94, bullet_range = 70,
      grenade_distance = 7, wave_height = 4, wave_speed = 10, wave_range = 70,
      cannon_pressure = .1, pressure_recovery = .22,
      torpedo_damage = 40, torpedo_speed = 32, torpedo_range = 160,
      torpedo_ticks = 180, arming_ticks = 30, torpedo_ammo = 3,
      fire_ticks = 15, heat_ticks = 480, cool_ticks = 360,
      grenade_ticks = 960, splash_ticks = 36, fuse_ticks = 120, grace_ticks = 240
    }
  }
}
