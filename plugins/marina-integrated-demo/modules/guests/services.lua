-- Two vessels and their one-shot services on the shared 120 ticks per second clock.
--   resident "tender": moored at "home" until 5 s, sails the racetrack, moored again from 25 s.
--   visitor "guest": absent until 2 s, arrives, docks at "slip" (dwell 14-16 s), leaves, absent from 21 s.
-- Deck colours differ because the hull colour is the side, hidden from the elevated camera.
local routes = require("guests.routes")

return {
  vessels = {
    { id = "tender", recipe = "runabout",
      appearance = { name = "TENDER", hull = {200, 60, 40}, deck = {210, 80, 60} } },
    { id = "guest", recipe = "runabout",
      appearance = { name = "GUEST", hull = {40, 90, 200}, deck = {60, 110, 220} } }
  },
  services = {
    { id = "outing", vessel = "tender", berth = "home", route = routes.resident,
      kind = "resident", start_tick = 600 },
    { id = "stay", vessel = "guest", berth = "slip", route = routes.visitor,
      kind = "visitor", dock_piece = 3, start_tick = 240 }
  }
}
