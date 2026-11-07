# Eriksberg work site (`proteus.outer-work`)

The work-site package for the native `cargo_sandbox` on `outer`. It places one registered
`service_platform` site at the north end of the Eriksberg inlet; see the
[free ride sandbox plan](../../docs/FREE_RIDE_SANDBOX_PLAN.md#s1a-the-existing-site-as-package-data)
and the [`scene.work` schema](../../docs/PLUGIN_API.md#work-sites-s1a).

Try an edit by moving `x`/`z` and rebuilding: the catalog build runs the site survey and fails
with a diagnostic if the platform, approach or crane sweep is not clear. `wake_velocity` free
ride has no work runtime, and rooms reject packages with work sites.
