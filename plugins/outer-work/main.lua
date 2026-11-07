-- The cargo sandbox's work sites on Gothenburg Harbour (outer), and their job. Each site places a
-- registered native preset: "service_platform" is the temporary platform, service crane, crate
-- pad and assisted workboat berth, all fixed relative to the berth at x, z (simulation metres,
-- x = -east, z = north) and turned with the moored boat's heading (radians; forward is
-- (sin, -cos)). "receiving_platform" is the same layout starting empty, its pad marked for
-- delivery. A `hidden` site is absent until the job builds it. Staging surveys every site,
-- hidden or not, against the map, marina, retained traffic, the spawn and the other sites.
-- The optional `boat` key picks the assisted berth's registered workboat preset ("flat_deck"
-- today); its hull, deck and capture envelope apply to every site. The optional `notices` table
-- holds the job's feedback text; `{site}` becomes the site name.
-- Crane, cargo, mooring and delivery physics stay native.
local function job(c, s)
  if not s.built then
    return {{kind="hud.set", generation=c.generation, id="job", label="CRATES TO LANDING / 1",
             value=s.delivered}}
  end
  local jetty = s.jetty == 1 and "JETTY SUPPLIED" or "SUPPLIES TO JETTY / 1"
  return {{kind="hud.set", generation=c.generation, id="job", label="LANDING BUILT", value=1},
          {kind="hud.set", generation=c.generation, id="jetty", label=jetty, value=s.jetty}}
end
return {
  init = function(c)
    local s = {delivered=0, built=false, jetty=0}
    return s, job(c, s)
  end,
  -- Native events: cargo.secured, cargo.delivered, cargo.lost and cargo.reset, each with the
  -- site's id and the crate's load number (1 until a supply starts load 2). The sandbox commits
  -- a delivery once per load at each receiver; the job counts the first one it wants.
  update = function(c, s)
    local commands = {}
    for _,e in ipairs(c.events) do
      if e.kind == "cargo.delivered" and e.id == "landing" and e.load == 1 then
        s.delivered = 1
      elseif e.kind == "cargo.delivered" and e.id == "jetty" and s.built then
        s.jetty = 1
      end
    end
    if s.delivered == 1 and not s.built then
      s.built = true
      -- The landing's supplies build the quay, whose pad holds the next load for the jetty, a
      -- short run east along the river. The host applies the whole batch, or waits and retries.
      commands = {
        {kind="work.enable", generation=c.generation, id="landing_quay"},
        {kind="work.enable", generation=c.generation, id="jetty"},
        {kind="cargo.supply", generation=c.generation, id="landing_quay"},
        {kind="work.guide", generation=c.generation, id="jetty"}
      }
    end
    for _,command in ipairs(job(c, s)) do commands[#commands + 1] = command end
    return s, commands
  end,
  free = {
    version = 1,
    scene = {
      version = 3,
      work = {
        boat = "flat_deck",
        notices = {
          delivered = "Crate delivered. It stays counted, even if moved again.",
          built = "{site} built.",
          supplied = "Supplies used. Next crate waits on the {site} pad.",
          returned = "Crate returned to the {site} pad."
        },
        sites = {
          -- North end of the Eriksberg inlet, outside the marina entrance and traffic paths.
          { id = "eriksberg", preset = "service_platform", cargo = "crate", x = 10, z = 245,
            heading = 0 },
          -- Open water about 270 m south-east, at the first surveyed receiving candidate. Its bow
          -- points along the crossing from Eriksberg, so the arriving boat drives straight in.
          { id = "landing", preset = "receiving_platform", cargo = "crate", x = -105, z = 0,
            heading = -0.45 },
          -- Built by the job once a crate reaches the landing: a working quay beside it, with
          -- the next load on its pad.
          { id = "landing_quay", preset = "service_platform", cargo = "crate", x = -125, z = 10,
            heading = -0.45, hidden = true },
          -- Built with the quay: open water about 120 m east of it, up the river. Its bow points
          -- along the river, so the second leg turns away from the first crossing.
          { id = "jetty", preset = "receiving_platform", cargo = "crate", x = -245, z = 10,
            heading = -1.57, hidden = true }
        }
      }
    }
  }
}
