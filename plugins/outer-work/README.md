# Eriksberg work site (`proteus.outer-work`)

The work-site package for the native `cargo_sandbox` on `outer`. It places a `service_platform`
site, where the crate starts, at the north end of the Eriksberg inlet, a
`receiving_platform` landing in open water about 270 m south-east, a hidden `landing_quay`
beside the landing and a hidden `jetty` receiver 120 m east of the quay. Its Lua job
(`init`/`update`, granted by `objective.hud`) counts load 1's `cargo.delivered` at the landing,
builds the quay and jetty with `work.enable`, uses the delivered crate up for load 2 on the
quay's pad with `cargo.supply`, points the helm at the jetty with `work.guide`, and counts the
jetty's delivery; see
the [job contract](../../docs/PLUGIN_API.md#work-site-job-s1c), the
[free ride sandbox plan](../../docs/FREE_RIDE_SANDBOX_PLAN.md#s1-steps)
and the [`scene.work` schema](../../docs/PLUGIN_API.md#work-sites-s1a).

Try an edit by moving `x`/`z` or turning `heading` and rebuilding: the catalog build runs the
site survey and fails with a diagnostic if the platform, approach or crane sweep is not clear;
it also probes the job's `init`. Only `cargo_sandbox` runs the job: `wake_velocity --plugin`
refuses the package, and rooms reject packages with work sites.
