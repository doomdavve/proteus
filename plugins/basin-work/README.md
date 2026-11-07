# Basin reference fixture (`proteus.basin-work`)

The work-site package for the native `cargo_sandbox` on the basin. It places one
`service_platform` site at x = 0, z = 0, heading 0, so the crate starts at the basin origin. It
has no job and is never a default: only `cargo_sandbox` loads it, and `wake_velocity --plugin`
and rooms reject it. Moving the site runs the basin bounds survey at build time.
