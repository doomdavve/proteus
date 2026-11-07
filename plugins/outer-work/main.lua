-- The cargo sandbox's work sites on Gothenburg Harbour (outer). Each site places a registered
-- native preset: "service_platform" is the temporary platform, service crane, crate pad and
-- assisted workboat berth, all fixed relative to the berth at x, z (simulation metres, x = -east,
-- z = north; the berth faces heading 0). Staging surveys every site against the map, marina,
-- retained traffic and the spawn. Crane, cargo and mooring physics stay native.
return {
  free = {
    version = 1,
    scene = {
      version = 3,
      work = {
        sites = {
          -- North end of the Eriksberg inlet, outside the marina entrance and traffic paths.
          { id = "eriksberg", preset = "service_platform", cargo = "crate", x = 10, z = 245 }
        }
      }
    }
  }
}
