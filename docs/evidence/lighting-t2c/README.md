# Lighting T2c evidence, 2026-11-07

Local work on `feat/lighting-t2c-qualification`, stacked on T2b. The
[lighting plan](../../SUNSET_PLAN.md#t2c-implementation-2026-11-07) owns conclusions, settings,
verification, open gates and the next task.

- [Native qualification](native-qualification.json): all fifteen Release report summaries,
  plus medians and repeat ranges. Apple M1 Max / macOS 26.6.2, waves, fixed hour 6.5 against
  sampled virtual 900 s; eight warmup draws, 721 measured GPU or 241 CPU draws per workload.
- [Hardware-browser qualification](browser-qualification.json): actual-game fixed-hour CPU
  costs, browser frame intervals, actual target dimensions and Metal backend. Chrome
  156.0.8078.81 / Emscripten 6.0.12 MinSizeRel. GPU execution timing is unavailable.
- [CPU Instruments sample summary](cpu-profile-summary.json): a separate ten-second
  diagnostic trace, excluding its first two seconds. Inclusive stacks overlap. Its time limit
  terminates the child; partial output is excluded from the qualification matrix.
- [Course sheet](course.png): CPU then GPU, columns at 0, 6.1, 12 and 19.5 hours.
- [Combat/mirror sheet](combat.png): CPU then GPU; cannon, grenade and torpedo rows at
  the same four hours. The mirror follows the main light. Night playtest acceptance is open.

Native reproduction (create the report parent directory first):

```sh
mkdir -p build/lighting-qualification
./build-release/boat_water_review --lighting-qualify build/lighting-qualification/outer \
  --renderer gpu --scenario outer --waves --lighting-hour 6.5 --lighting-samples 721 \
  --resolution high --hdr on
./build-release/boat_water_review --lighting-qualify build/lighting-qualification/stress-cpu \
  --renderer cpu --scenario stress --waves --lighting-hour 6.5 --lighting-samples 241 \
  --resolution low --hdr off
```

Full CSVs, individual captures, browser exports and the trace remain local under ignored
`build/t2c-review/`. The HTTP browser harness only injected startup arguments and a report-read
hook into its served test response; packaged artifacts on disk were unchanged. These agent
checks do not record human look acceptance, physical input or audio listening.
