-- The cargo sandbox's work sites on Gothenburg Harbour (outer). Each site places a registered
-- native preset: "service_platform" is the temporary platform, service crane, crate pad and
-- assisted workboat berth, all fixed relative to the berth at x, z (simulation metres, x = -east,
-- z = north) and turned with the moored boat's heading (radians; forward is (sin, -cos)).
-- "receiving_platform" is the same layout starting empty, its pad marked for delivery. Staging
-- surveys every site against the map, marina, retained traffic, the spawn and the other sites.
-- Crane, cargo and mooring physics stay native.
return {
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
            heading = -0.45 }
        }
      }
    }
  }
}
