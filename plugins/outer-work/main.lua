-- The cargo sandbox's work sites on Gothenburg Harbour (outer), and their job. Each site places a
-- registered native preset: "service_platform" is the temporary platform, service crane, crate
-- pad and assisted workboat berth, all fixed relative to the berth at x, z (simulation metres,
-- x = -east, z = north) and turned with the moored boat's heading (radians; forward is
-- (sin, -cos)). "receiving_platform" is the same layout starting empty, its pad marked for
-- delivery. A `hidden` site is absent until the job builds it. Staging surveys every site,
-- hidden or not, against the map, marina, retained traffic, the spawn and the other sites.
-- Crane, cargo, mooring and delivery physics stay native.
local function job(c, s)
  if s.built then
    return {{kind="hud.set", generation=c.generation, id="job", label="LANDING BUILT", value=1}}
  end
  return {{kind="hud.set", generation=c.generation, id="job", label="CRATES TO LANDING / 1",
           value=s.delivered}}
end
return {
  init = function(c)
    local s = {delivered=0, built=false}
    return s, job(c, s)
  end,
  -- Native events: cargo.secured, cargo.delivered, cargo.lost and cargo.reset, each with the
  -- site's id. The sandbox commits a delivery once per session, but the job counts only the
  -- first one anyway.
  update = function(c, s)
    local commands = {}
    for _,e in ipairs(c.events) do
      if e.kind == "cargo.delivered" and e.id == "landing" and s.delivered == 0 then
        s.delivered = 1
      end
    end
    if s.delivered == 1 and not s.built then
      s.built = true
      -- The host builds the quay before committing this batch, or retries it later.
      commands[1] = {kind="work.enable", generation=c.generation, id="landing_quay"}
    end
    for _,command in ipairs(job(c, s)) do commands[#commands + 1] = command end
    return s, commands
  end,
  free = {
    version = 1,
    scene = {
      version = 3,
      work = {
        sites = {
          -- North end of the Eriksberg inlet, outside the marina entrance and traffic paths.
          { id = "eriksberg", preset = "service_platform", cargo = "crate", x = 10, z = 245,
            heading = 0 },
          -- Open water about 270 m south-east, at the first surveyed receiving candidate. Its bow
          -- points along the crossing from Eriksberg, so the arriving boat drives straight in.
          { id = "landing", preset = "receiving_platform", cargo = "crate", x = -105, z = 0,
            heading = -0.45 },
          -- Built by the job once a crate reaches the landing: a working quay beside it.
          { id = "landing_quay", preset = "service_platform", cargo = "crate", x = -125, z = 10,
            heading = -0.45, hidden = true }
        }
      }
    }
  }
}
