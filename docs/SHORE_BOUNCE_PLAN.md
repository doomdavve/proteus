# Shore bounce trial

Current state: done; cartoon handling accepted by the user on 2026-11-22.
Open items: other-platform qualification remains with the existing platform tracks.
Done when: bow/stern bank shots, docking and scrapes pass checks and feel good to the user.
Next task: PR and CI review; the current contract is in Playing → Shore impacts.

## Scope

Try symmetric 90% shore reflection at speed, soft docking below 0.5 m/s, a smooth
transition through 3 m/s, low tangential friction, a stronger corner turn and extra rebound yaw damping.
Keep moored-craft, moving-craft and boat-pair restitution unchanged. This track defers the existing
wake handling trial, controls acceptance, race pacing, multiplayer qualification and
other Open loops in [the index](README.md#open-loops).

## Steps

1. Implement shore-specific response and integration regression checks (implemented).
2. Build, run full CTest, and perform graphics-only demo smoke checks (complete).
3. Record the contract in [Playing](PLAYING.md#shore-impacts) (written);
   accepted behavior is the current contract.

4. Cartoon pass: raise restitution from 70% to 90%, double the turn lever from
   0.35 m to 0.70 m, reduce tangential friction from 0.03 to 0.02, and settle rebound spin
   with an extra 6/s damping rate near shore (implemented and machine-verified).

## Current cartoon pass — 2026-11-22

The user found the first pass a bit better and requested an almost cartoonish trial.
This is feedback on the baseline, not final acceptance.
The turn lever remains shortened relative to the real hull. The larger impulse is still
dissipative: the same lever is used for contact speed, effective mass and yaw impulse.

Extra damping is stateless: it applies only while receding from a nearby map shore or
basin wall, ramps from zero at 0.5 m/s to full strength at 3 m/s, and stops in open water.
The proximity radius is the larger hull end plus half beam, 0.35 m clearance and a 3 m
exit margin. This approximation also damps ordinary turns when already moving away from
a nearby shore; it is not a timer attached to the last collision. It requires no network,
save or replay state changes. Moored and moving craft keep their existing contact response.

Verification: build and focused collision/damping checks passed on macOS Debug.
`ctest --preset full`: all 609 tests passed (63.98 s), with local socket access for
integration checks. Silent windowed basin and outer demos completed with the baseline
commands below (`--no-audio --demo --frames 180`). Compiler output was warning-clean.
The new damping check compares the same receding/spinning state near shore and in open
water, requiring faster settling near shore. The fast contact checks retain energy and
containment assertions while expecting the stronger rebound and angular kick.
These demo runs are runtime smoke checks. On 2026-11-22 the user accepted the cartoon
handling ("I like this a lot") and authorized a PR. The implementation and documentation
steps are complete; other-platform qualification remains with the existing platform tracks.

## Baseline verification (70%, superseded tuning)

2026-11-22, macOS / AppleClang 21, Debug:
- Build completed without compiler warnings.
- `ctest --preset full`: all 609 tests passed (60.69 s), with local socket access
  for ENet/process integration tests. The sandbox-only attempt could not open sockets.
- Silent windowed basin and outer demos completed with
  `./build/wake_velocity --no-audio --demo --frames 180` and
  `./build/wake_velocity --no-audio --location outer --demo --frames 180`.
  These are startup/runtime smoke checks, not a human assessment of collision feel.
- Integration checks cover fast and slow bow/stern impacts, tangential momentum,
  corner yaw, energy dissipation, unpowered separation and sustained powered containment.
- Existing contact checks now expect restrained spin; mapped-world checks verify
  contact and containment instead of a final position held against a bank.
- The voyage validator's coastal autopilot rounds the harbour approach with an extra
  waypoint. Its former final leg cut a bank and depended on the softer collision response;
  the navigable-water, calm-route and controlled-arrival assertions remain active.

The baseline had no user acceptance. The cartoon pass above was subsequently accepted;
other-platform qualification is still unclaimed.
The baseline used a 0.35 m turning lever; the current trial doubles it to 0.70 m.
This affects angular impulse, not the rendered hull size or collision footprint.

