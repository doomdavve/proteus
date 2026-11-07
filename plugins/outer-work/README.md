# Eriksberg work site (`proteus.outer-work`)

The work-site package for the native `cargo_sandbox` on `outer`. It places a `service_platform`
site, where the crate starts, at the north end of the Eriksberg inlet, and a
`receiving_platform` landing in open water about 270 m south-east; see the
[free ride sandbox plan](../../docs/FREE_RIDE_SANDBOX_PLAN.md#s1-steps)
and the [`scene.work` schema](../../docs/PLUGIN_API.md#work-sites-s1a).

Try an edit by moving `x`/`z` or turning `heading` and rebuilding: the catalog build runs the
site survey and fails with a diagnostic if the platform, approach or crane sweep is not clear. `wake_velocity` free
ride has no work runtime, and rooms reject packages with work sites.
