-- The basin reference fixture for cargo_sandbox: native-only, never a default, no job. One
-- service_platform site at the basin origin holds the crate; only the sandbox loads it.
-- Crane, cargo, mooring and delivery physics stay native.
return {
  free = {
    version = 1,
    scene = {
      version = 3,
      work = {
        boat = "flat_deck",
        sites = {
          { id = "basin", preset = "service_platform", cargo = "crate", x = 0, z = 0,
            heading = 0 }
        }
      }
    }
  }
}
