# R2b historical weighting and divergence proposal

Status: private p=1 CPU basin trial implemented; curved handling preservation failed.
Owner: [wake research plan](WAKE_RESEARCH_PLAN.md#r2--turning-geometry-experiments).
Evidence: [R2a rejection](evidence/wakes/r2/README.md), [R2b algebra audit](evidence/wakes/r2b/README.md).
Open: changed encounter contract/player feedback, full-frame/relative CPU and wider integration.
Next: experimental basin comparison and mechanical review; no geometry selected.

## Scope and decision

The proposed transform spreads each existing finite source on its outer side and transports
its original signed contribution. It keeps the original record's longitudinal window, birth,
rise, fade, phase convention and signed crest/trough. It uses no connected strip or added cap.
It makes an explicit arcade choice: omit additional per-record inner focusing rather than
attempt singular curved-ray transport. It does not reconstruct a curved physical wavefront.

The first proposed private experiment uses signed-density transport (`p=1` below), unchanged
60-tick births, 32 slots/emitter, 16-second lifetime and source strength. Its illustrative fixed
transition width is `epsilon=.25`, dimensionless; this is a provisional experiment parameter,
not a measured optimum or shipping setting. There is no amplitude or exponent sweep.

This proposal is not a C join, shader integration, capacity increase, physical promotion of
serviced boats, protocol change or shipping-budget decision. Existing R0/R1 HDR tails, R1
human review, browser/slower-GPU qualification and other areas on the Open loops board remain
with their owners. R3 persistence remains dependent on a geometry selection.

## Preserve the finite source, including its endpoints

For record `i`, use its fixed position `P`, unit tangent `T`, normal `N=(-T.y,T.x)`, original
support length `L`, inverse speed `k`, birth time `b` in seconds, amplitude `A`, and lifetime.
The unchanged scalar field in material coordinates is

$$
 h(s,v,t)=A W(s)E(s,t)S(q),\qquad
 q=\sqrt{v^2+0.25}-.9-c(t-b-ks),\quad c=2.4.
$$

`W(s)=cos²(pi*s/(2L))` for `|s|<L`, zero otherwise. `E` is the complete original birth/rise/fall
product, including `fade_start=12-L*k` for the 16-second recipe. The phase uses the original
birth, not the historical source midpoint. `L` remains the existing possibly padded support
length; it is not silently replaced by a reconstructed chord or arc length.

The material coordinate carries the *existing recipe's support*, including its longitudinal
tails. It does not assert that unobserved future source motion followed that support.
Each record retains its own `k`, birth and amplitude. Unequal speeds or lengths need not share
a single phase function across an overlap.

Do not normalize neighboring windows into a partition of unity. For two equal straight records
at centers `0,L`, their windows sum to one in the common interior, but the finite train's first
tail has weight `.5` at `s=-L/2`. Normalizing that tail doubles it. Unequal lengths, amplitudes,
minimum-length padding and birth gates add further counterexamples. Exact interior partition
identities cannot replace the original endpoint/envelope behavior.

## Why a curved normal-ray map is not the first experiment

For an arc-length curve `gamma(s)` with `T_s=kappa*N`, `N_s=-kappa*T`, write

$$
 r=.9+c(t-b-ks)+q,\quad v=\sigma\sqrt{r^2-.25},\quad
 X(s,q,t)=\gamma(s)+vN(s).
$$

For real `r>=.5` and away from `v=0`,

$$
 v_s=-ckr/v,\quad v_q=r/v,\quad v_t=cr/v,
$$
$$
 X_s=(1-\kappa v)T-(ckr/v)N,\quad X_q=(r/v)N,
$$
$$
 \det(X_s,X_q)=(1-\kappa v)r/v.
$$

The relative crest-length stretch is

$$
 \Lambda=\frac{\sqrt{(1-\kappa v)^2+(ckr/v)^2}}
 {\sqrt{1+(ckr/v)^2}},
$$

but the area-Jacobian ratio is `1-kappa*v`. At the inner focus `kappa*v=1`, the crest tangent
can remain nonzero because of emission-age tilt while the complete phase band folds. Dividing
by crest-length stretch does not make area transport invertible. Distinct `(s,q)` values can
map to the same focus. Positive local determinant elsewhere also does not prove global
injectivity. Every surviving branch would need explicit accounting.

The `v=0` singularity in this phase chart is separate: baseline `(s,v)` coordinates remain
regular there. The square-root branch is not an all-plane inverse for a surface evaluator.

A true curved model would need a declared pre-caustic fade, finite-width regularization or
branch treatment. Fading removes transported contribution; blending back to baseline can add
separated/doubled fronts. These are material gameplay choices and remain deferred.

## Proposed outward-only map

Freeze a signed divergence estimate `K` in each new record. For `epsilon>0`, define

$$
 f(x)=\begin{cases}0&x\le0,\\x^3/(\epsilon^2+x^2)&x>0,\end{cases}
 \qquad D(v)=1+f(-Kv).
$$

`f` is C2 at zero. Its positive-side derivative is

$$
 f'(x)=\frac{x^2(3\epsilon^2+x^2)}{(\epsilon^2+x^2)^2},\quad
 D_v=-Kf'(-Kv),\quad 0\le f'\le9/8.
$$

The forward map and direct inverse are

$$
 F(s,v)=P+D(v)sT+vN,\qquad
 u=(x-P)\cdot T,\quad v=(x-P)\cdot N,\quad s=u/D(v).
$$
$$
 DF=\begin{pmatrix}D&sD_v\\0&1\end{pmatrix},\qquad\det DF=D\ge1.
$$

The Cartesian inverse is unique for every point, including `v=0`. `K=0` gives exact identity;
`K*v>=0` gives exact identity on that record's inner half-plane. Outer support widens smoothly.
A future floating-point implementation must return the unchanged baseline per-record result
directly when `K*v>=0` (including `K=0`), avoiding a coordinate round trip or omitted gradient
terms caused by testing a rounded `D==1`. The normal coordinate stays in the fixed historical
frame; expanding longitudinal support introduces tangential drift and changes crest orientation.
The map transports the old finite window rather than adding an independent circular cap.

Emission-age tilt still matters in the moving phase chart. With the same `v(s,q,t)` as above,

$$
 F_s=(D+sD_vv_s)T+v_sN,\quad F_q=sD_vv_qT+v_qN,
$$
$$
 F_t=sD_vv_tT+v_tN,\qquad \det(F_s,F_q)=D r/v.
$$

The phase gradient is `(c*k/D)*T + (v/r-c*k*s*D_v/D)*N`; its orientation generally changes.
Omitting the `s*D_v*v_s` term gives the wrong crest-stretch calculation. Cartesian inversion
remains regular even where the phase chart's `v=0` parametrization fails.

## What is transported

For exponent `p`, define `H(x,t)=D(v)^(-p)*h(s,v,t)`.

| Choice | Invariant for an independently transported record |
| --- | --- |
| `p=1` | Integrated signed height and integrated absolute height |
| `p=.5` | Integrated squared height |
| `p=0` | Material-point height; integrated height magnitude grows with support |

These statements follow from `du*dv=D*ds*dv`, assuming the reference is integrable. At fixed
`v`, the signed and absolute slice integrals scale by `D^(1-p)`, and the squared slice integral
by `D^(1-2p)`. Signed integrals also add across records; absolute/squared norms of the **summed**
field generally do not, because overlap/cancellation changes.

Use `p=1` for the first private screen. It literally spreads the existing contribution, rather
than adding cap strength. It attenuates expanded outer fronts; a doubled width at fixed `v`
halves transported material-point height. That may erase the hoped-for continuity benefit.
Failure is an allowed research result, not grounds for silently changing the exponent.

These invariants describe the arcade reference scalar. They are not validated conservation
of water volume, mechanical energy, momentum or encounter impulse. Squared height is only an
energy proxy and omits slope/velocity and coherent cross terms.

## Complete derivatives and support

For frozen `D(v)`, let `h_s,h_v,h_t` be the original full derivatives. Then

$$
 H_u=D^{-p-1}h_s,
$$
$$
 H_v=D^{-p}\left[h_v-\frac{D_v}{D}(s h_s+p h)\right],\qquad
 H_t=D^{-p}h_t,
$$

and world gradient is `T*H_u+N*H_v`. Original window, envelope and phase derivatives all remain
inside `h_s` and `h_t`. The shear/gain derivative cannot be omitted from the normal slope.
For `p=1`, a finite per-record bound is

$$
 |H_v|\le |h_v|+\tfrac98 |K|(L|h_s|+|h|).
$$

Thus no focusing gain does **not** mean no increased slope. End-window/shear effects still
need mechanical screening. If a later design introduces `D_t`, it must also include
`-D^(-p)*(D_t/D)*(s*h_s+p*h)` in vertical surface velocity. No such time-dependent descriptor
or blend is selected here.

Original support becomes `|u|<D(v)*L`; normal/profile support still uses the original `q`.
A conservative bound at clock `t-b>=0` is

$$
 R_{max}=.9+c\max(0,t-b+Lk)+2,\quad |v|<R_{max},
$$
$$
 D_{max}=1+f(|K|R_{max}),\quad |u|<D_{max}L.
$$

The old longitudinal stamp bound `|u|<L` is insufficient. A surviving candidate would need
new stamp placement/outside detection, cache keys and all CPU/VS/FS/fallback recipes audited
together. A finite bound is not an affordable bound. There is no GPU implementation or cost
measurement in this design checkpoint.

The original field and its first derivatives vanish before hard expiry throughout material
support. Static transport preserves that property, without a lifetime change.

## Historical capture: successor only

The following three goals cannot all hold: immediate baseline emission, immutable already
emitted fields, and symmetric adjustment using a pair that finishes later. The proposed
choice preserves the first two. Only the successor captures the pair's historical estimate;
its predecessor is never rewritten. The first eligible record after a restart has `K=0`.
Offline fixtures must retain that startup asymmetry. This rules out rewriting an older descriptor;
a separately born pair-correction field would be a different, separately reviewed C-like design.
The argument does not prove every possible pair-correction construction impossible.

Proposed capture for the first private experiment:

1. Require consecutive valid spans in the same emitter identity/session and uninterrupted
   source epoch. Their end/start ticks and captured shared endpoint must agree. Use captured
   source endpoints and continuity state, not approximate endpoints reconstructed from padded
   support, current ring slots or spatial proximity.
2. Let actual chord lengths be `a_prev,a_cur`, headings differ by signed `theta`, and
   `ell=(a_prev+a_cur)/2`, the polyline distance through their shared endpoint between midpoints.
   Set `K=2*sin(theta/2)/ell`. For equal consecutive chords on a circle this returns `1/R`;
   for general motion it is a discrete divergence proxy, not a trajectory reconstruction.
3. Require finite nonzero chords and a declared turn-domain gate. The provisional first-screen
   gate is `|theta|<pi/2`; sharper turns/reversals use `K=0`. This is a model-domain choice,
   not proof that its worst-case support is affordable. It is a hard gate, not a smooth
   change across neighboring source histories. Near-threshold spans can alternate spreading
   and baseline descriptors even though each frozen record remains temporally continuous.
4. Break eligibility on missed tracking, explicit reset/teleport, dry/slow restart, an omitted
   invalid emission span, emitter/session generation change or unknown provenance. Retained
   old records keep their descriptors; the existing policy may still explicitly clear the whole
   pool on a world/session reset. Source-epoch restart alone does not rewrite retained waves.
5. Freeze the result for the successor's full lifetime. Later turns, stops, parent expiry,
   reload that retains the record, and slot reuse must not recompute it. Ineligible capture
   history freezes `K=0` at birth. Missing/corrupt descriptors after capture or restore instead
   need validation/rejection; a receiver must not silently choose a different local geometry.
   Do not rebuild lineage from surviving neighbors or drop a physical record to hide an error.

`K=0` identity therefore covers a complete finite straight history, including unequal amplitudes,
speeds/lengths, starts/stops and temporal gates. A record carrying nonzero *historical* `K` does
not become straight merely because its live neighbors now appear collinear.

The existing raw-teleport tracker failure is not solved by this descriptor. Integration must
supply the restart signal; geometry fallback cannot prove the hull was continuously wet or
turn an unannounced teleport into valid motion.

## Inner-focus guarantee and its limit

For fixed geometry, `p=1` gives `|H_i|<=|A_i|` for the current bounded profile/envelope, and each
record's inner half-plane is baseline exactly. On the intersection of all participating inner
half-planes of a simple-circle fixture, the entire sum remains baseline. This restricted
observation does not cover a figure-eight, reversal, independent crossing or arbitrary history.

A point can be inner to one historical front and outer to another. Widening the latter can
increase the sum there. The finite-pool bound `|sum H_i|<=sum |A_i|` is conservative and far too
loose to qualify gameplay. Total-field concentration must remain an empirical acceptance gate
over declared histories. A global height clamp or emitter-wide normalization would change
the independence/signed-superposition contract and is outside this proposal.

The algebra audit includes a synthetic regional counterexample: the second baseline record is
absent in `x>L`, but its outer support reaches a region on the first record's inner side. The
new sum can exceed that region's baseline amplitude bound. This is a mathematical counterexample,
not an observed boat trajectory or capsize result.

## Metadata, compatibility and experiment gate

No source/query can safely recover frozen `K` merely from the currently live predecessor: the
parent can expire, and the oldest record in a checkpoint can lack its predecessor. A future
private prototype uses an immutable descriptor sidecar plus explicit source-epoch capture state
(`tools/wake_capture.[ch]`), with copied predecessor witnesses for same-build snapshot validation.
Debug lineage keys are additional to the curvature scalar.

A 32-bit scalar sidecar alone would require 512 logical bytes across 128 slots. This is not an
actual structure-size measurement or an approved production allocation. ABI padding might allow
a different in-structure layout; capture/provenance state still needs separate accounting.
Current [wire records](MULTIPLAYER_PROTOCOL.md#wake-record-36-bytes) have 36 explicit bytes and
no `K`. A naive extra float would add four bytes per record/birth and 512 bytes to a full record
set, before any provenance or headers. No such wire change is selected. Digest, checkpoints,
prediction/replay, births, capability/version handling and shader packing remain future audits.

Before a private candidate is retained, require:

- Baseline-relative straight interior **and endpoints**, 2% RMS / 5% pointwise peak-error / two-tick
  arrival screens with explicit nonzero scales; preserve actual source clocks and gates.
- Finite outputs; exact same-build replay; spatial/time derivative refinement across the dilation
  seam, support, birth/rise/fade/expiry; signed independent and same-emitter nonadjacent crossings.
- Capture cases just below, exactly at and just above the turn-domain threshold, including
  consecutive near-threshold spans; flag alternating-field behavior rather than calling the gate smooth.
- Exact frozen-descriptor behavior through later motion/reset, parent expiry, checkpoint restore
  and slot reuse, using a private snapshot/restore contract. Capture/lifecycle behavior is not
  tested by the algebra audit; production wire restoration remains a later compatibility gate.
- Source-off paired turn/straight probes with baseline amplitude references, local attribution,
  separate ridge displacement and no opposite-front selection in broad inner searches.
- Tight circles, reversals, figure-eights and crossing full-domain concentration. Flag all increases
  above baseline for review; no per-record argument waives the coupled encounter ±10% screen or
  individual capsize/state review if the candidate later reaches that stage.
- The same fixed-cadence saturated one/four-source CPU comparisons, plus exact sidecar/tracker
  resource accounting. GPU/frame/network costs remain unavailable before reviewed integration.

If the next private field screen fails, retain the rejection. Do not quietly switch to `p=.5`,
retune source height, symmetrize old records or add joins to force an apparent continuity win.


## Private capture implementation

The capture-only harness retains the baseline field evaluator and physical records. It freezes
successor descriptors using captured endpoints, source identity and epochs; it never reconstructs
lineage from surviving ring neighbors. The strict turn gate uses the chord dot product, rejecting
an exact right angle without a rounded `pi/2` comparison. A sharp but valid span remains eligible
as the next span's predecessor.

Dry/slow and missed observations break capture history. The raw baseline tracker can begin a
wet span from its last dry sample: the harness preserves that birth/strength timing for parity,
marks the carry-in descriptor ineligible, and gives it zero curvature. It cannot seed a pair.
This is a retained baseline source-policy limitation, not proof of continuously wet emission.
Explicit teleport/session resets reset the private tracker and break the epoch while retaining
old records. Callers must supply these reset signals; no teleport detector is added.

Snapshots are versioned, checksum-protected same-build memory copies, validated before committing
the destination. They carry full witnesses even after predecessor expiry; they are neither a
portable file format nor a production checkpoint/wire proposal. The witness-heavy audit allocation
is measured in [the capture evidence](evidence/wakes/r2b/capture/README.md). A smaller representation,
quantization, GPU packing, protocol changes and production allocation still require review.


## Private field-screen decision

The fixed p=1 prototype uses the captured descriptors and the original baseline scalar in local
coordinates, with exact direct baseline returns for zero curvature and each record's inner side.
Its gradient includes shear/gain terms. Conservative bounds cover the widened support; foam is
an explicitly cosmetic policy. No production target links the evaluator.

[The field evidence](evidence/wakes/r2b/field/README.md) records useful outer coverage, exact
sampled straight identity and passing derivative/superposition screens. It also retains a
figure-eight trough increase, substantial relative CPU costs and a per-run .257 ms p99 failure
against the provisional .15 ms fixture ceiling. These results support further private research,
not geometry selection or performance/gameplay acceptance. CPU profiling and coupled-hull tests
precede any integration decision. No exponent, height, curvature threshold or lifetime retune
is selected to overcome the unresolved findings.


## CPU basin trial

[The coupled evidence](evidence/wakes/r2b/basin/README.md) records exact field-preserving
optimization, production-hull sampling and the experimental CPU trial. A borrowed object-only
sampler on `BoatWater` replaces the player-object contribution and keeps current/normal composition
and arcade recovery. The CPU view shares that sampler; the GPU refuses it. Context copies are
borrowed and must be rebound by their owner after copying/resetting trial storage.

The targeted curved encounter screen fails its provisional preservation criteria despite useful
coverage and improved CPU measurements. The trial permits user comparison of changed gameplay;
it is not geometry selection. Production baseline, lifetime, capacities and protocol remain.
