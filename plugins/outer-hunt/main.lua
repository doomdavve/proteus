-- A shelter hunt for a Gothenburg Harbour (outer) room. Every crew collects each mark by driving
-- into it; the first crew with all of them is shown, and when every crew present has them all a
-- new hunt starts. Simulation metres: x = -east, z = north.
-- Events: mark.reached {id, crew}, crew.joined {crew}, crew.left {crew}. Crews are numbered 1-4.
local MARKS = 5 -- The length of free.marks below.

local function count(found)
  local n = 0
  for _ in pairs(found) do n = n + 1 end
  return n
end

local function hud(c, s)
  local out = {}
  for crew = 1, 4 do
    local key = "c" .. crew
    if s.crews[key] then
      out[#out + 1] = {kind="hud.set", generation=c.generation, id=key,
        label="CREW " .. crew, value=count(s.crews[key])}
    else
      out[#out + 1] = {kind="hud.remove", generation=c.generation, id=key}
    end
  end
  out[#out + 1] = {kind="hud.set", generation=c.generation, id="hunt", label="HUNT",
    value=s.hunt}
  if s.first > 0 then
    out[#out + 1] = {kind="hud.set", generation=c.generation, id="first", label="FIRST CREW",
      value=s.first}
  end
  return out
end

return {
  init = function(c)
    -- first: the latest hunt's first finisher (kept into the next hunt until someone finishes
    -- it); claimed: whether the current hunt has a first finisher yet.
    local s = {crews={}, hunt=1, first=0, claimed=false}
    return s, hud(c, s)
  end,
  update = function(c, s)
    for _, e in ipairs(c.events) do
      local key = e.crew and ("c" .. e.crew)
      if e.kind == "crew.joined" then
        s.crews[key] = s.crews[key] or {}
      elseif e.kind == "crew.left" then
        s.crews[key] = nil
      elseif e.kind == "mark.reached" then
        local found = s.crews[key] or {}
        s.crews[key] = found
        found[e.id] = true
        if not s.claimed and count(found) == MARKS then
          s.first, s.claimed = e.crew, true
        end
      end
    end
    local present, done = 0, 0
    for _, found in pairs(s.crews) do
      present = present + 1
      if count(found) == MARKS then done = done + 1 end
    end
    if present > 0 and done == present then
      for key in pairs(s.crews) do s.crews[key] = {} end
      s.hunt, s.claimed = s.hunt + 1, false
    end
    return s, hud(c, s)
  end,
  free = {
    version = 1,
    marks = {
      { id = "north_reach", name = "NORTH REACH", x = -500, z = 300, radius = 25 },
      { id = "upper_bend", name = "UPPER BEND", x = -350, z = 0, radius = 25 },
      { id = "midstream", name = "MIDSTREAM", x = 250, z = -500, radius = 25 },
      { id = "wide_water", name = "WIDE WATER", x = 800, z = -850, radius = 25 },
      { id = "south_channel", name = "SOUTH CHANNEL", x = 1200, z = -1200, radius = 25 }
    }
  }
}
