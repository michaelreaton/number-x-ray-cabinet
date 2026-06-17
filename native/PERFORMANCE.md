# Native Performance Decision Log

This file records measured bigint performance decisions for the native workbench.
Benchmarks are local and noisy, so a result is actionable only when it has exact
parity plus a stable same-run paired win.

## Current Rules

- `replacementReady` requires exact parity, paired-median speed within the row
  limit, no worst-pair regression above `1.0`, and at least 4 of 5 stable
  paired samples.
- `kernel-probe` rows are evidence only. They do not change production routing.
- A primitive-level win is not enough. The candidate must also survive the full
  operation shape before it can replace the current path.
- Threshold or root-size candidates must survive same-run paired medians,
  adjacent-size checks, and a product-like `/GL` build before they can become a
  production route. Independent best/best ratios are not an adoption signal.
- A single threshold/gated policy row can never advertise route readiness on
  its own. Such rows must report `thresholdSafety=requires-forced-neighbor`,
  `noAutoRoute=1`, `replacementReady=false`, and `adoption=observe-only` until a
  dedicated forced-neighbor safety row passes.
- Shallow formatter safety rows for the noisy 10^19 preinverse window family
  must report `deepConfirmation=required`, `replacementReady=false`, and
  `adoption=observe-only`. Only the explicit `format-policy-deep-safety` rows
  can be read as single-build promotion evidence, and production routing still
  requires cross-build confirmation before any default changes.
- External frontier clues such as MFastFermat sparse/transform-order work enter
  Number X-Ray only as default-off, oracle-checked probes until local paired
  benchmarks show exact parity, adjacent-size safety, and product-like build
  confirmation.

## 2026-06-16: Benchmark Progress Digest

MFastFermat `2ffb290` added a completed/open progress digest for benchmark TSV
artifacts. Number X-Ray now mirrors that measurement hygiene with
`xray_benchmark_progress_tsv_text()`, the CLI `--bench-progress` mode, and a
saved `benchmark_progress.txt` artifact in each benchmark run folder.

The digest is deliberately a reporting guardrail, not a route change. It
separates completed product/backend route candidates from open/noisy route rows,
safety rejections, baseline/current rows, and controls. Rows labeled
`duplicateControl=`, `controlSafety=noisy-control`, `policy=current-default`, or
`candidate=current-scratch-*` are excluded from route-completed totals, so a
favorable median in a control or current-default row cannot be misreported as
production progress.

MFastFermat `017736c` tightened that rule for product-shaped rows: a route can
look good against an internal/control-adjusted gate and still fail the true
Product/GMP gate. Number X-Ray now mirrors the same distinction in the progress
digest with `productGatedOpen`. Rows carrying `noAutoRoute=1`,
`thresholdSafety=requires-forced-neighbor`, `thresholdSafety=forced-neighbor`,
`forcedCandidate=yes`, or `deepConfirmation=required` are listed as
product-gated/open and cannot contribute to `routeCompleted` until a
product-like proof row removes those blockers.

## 2026-06-17: Static Formatter Leaf Expansion

MFastFermat follow-up evidence around `d942c12`, `34ba68b`, and `f479009`
keeps a strong transform signal open because duplicate controls still fail.
The local analogue is to widen benchmark evidence before moving any route:
the D&C decimal formatter now tests static power-table leaves `32` and `64`
alongside the existing `8` and `16` rows, for both recursive string assembly
and direct-buffer output. These rows are exact-parity, default-off probes for
the 8k/16k formatting gap and must not be treated as production policy until
same-run stability and product-like route checks pass.

## 2026-06-16: Formatter Window Promotion Rejected

MFastFermat `main` is at `96117fd` and its newest tail-control DIF/DIT evidence
keeps a strong 8M transform route default-off because duplicate-control noise
is still high. The same rule applies here: one clean deep-safety artifact is not
enough to promote a threshold formatter route.

Number X-Ray tested the previously tempting
`deep-preinv10e19-pairs-window768-896` formatter window as a production route
candidate, then backed it out after the repeat run failed safety:

- Previous default artifact `20260616-214150-c4b04caf`: production `format`
  remained slow at 768/896 digits (`1.947x` and `2.087x` slower than MPIR).
- First production-route test `20260616-220723-c4b04caf`: 768/896 improved to
  ratios `0.810` and `0.801`, but 768 still had a worst pair of `1.444`.
- Repeat production-route test `20260616-221514-c4b04caf`: 768/896 regressed to
  `1.272x` and `1.404x` slower than MPIR, with worst pairs `1.921` and `3.543`.
- The repeat deep-safety row for `deep-preinv10e19-pairs-window768-896` reported
  `neighbor-regression`, `neighborStable=7/9`, `gateStable=6/9`, and worst pair
  `1.796`, so the route is not promotion-safe.
- Final rollback verification `20260616-222610-c4b04caf`: production `format`
  is back on the old route at 768/896 digits (`1.868x` and `1.906x` slower
  than MPIR), while the candidate remains visible as rejected probe evidence
  (`format-policy-deep-safety` ratio `0.728`, worst pair `1.134`,
  `worst-pair-regression`).

Decision: do not route the pre-inverted 10^19 pair-writer into production yet.
Keep the probe rows and add boundary roundtrip coverage at 767/768/896/897
digits so future candidates can be tested at the exact window edges.

## 2026-06-16: Tail Control Verdicts

MFastFermat `main` advanced to `d174c1d` and added tail duplicate-base
placement to its product-prefix comparator. The transferable lesson is that
duplicate controls should bracket the candidate work and emit a verdict, not
just a raw ratio. A noisy duplicate control can reject a scout, but it cannot
promote a variant.

Number X-Ray frontier scouts now label their duplicate scratch pass as
`controlPlacement=tail` and emit `controlSafety=stable-control` or
`controlSafety=noisy-control`. The visible row `status` mirrors that verdict
while `adoption` remains `observe-only`, so an exact frontier row with a noisy
tail control is visibly blocked from promotion.

Run:

- Release: `native/build-codex-gtk-autodetect2/native-test-runs/20260616-214150-c4b04caf`
- Tests: `ctest --test-dir native\build-codex-gtk-autodetect2 -C Release --output-on-failure`
  passed 4/4 in 325.90s.

Frontier verdict rows:

- 32,768-digit multiply: status `noisy-control`, ratio `1.662`, worst `2.003`,
  control ratio `1.044`, control worst `1.107`.
- 32,768-digit square: status `noisy-control`, ratio `1.322`, worst `1.606`,
  control ratio `0.827`, control worst `1.237`.
- 65,536-digit multiply: status `noisy-control`, ratio `1.869`, worst `2.068`,
  control ratio `0.983`, control worst `1.031`.
- 65,536-digit square: status `noisy-control`, ratio `2.137`, worst `2.273`,
  control ratio `0.937`, control worst `1.113`.

## 2026-06-16: Frontier Duplicate Controls

MFastFermat `main` advanced to `5fc94b9` and exposed
`ntt16_wide61_difdit_32000` as a repeatable DIF/DIT benchmark knob, while the
README still rejected promotion without longer duplicate-default controls. The
actionable import for Number X-Ray is measurement discipline: large frontier
scout rows now time the current scratch route twice in the same sample and
record `duplicateControl=default`, `controlRatio`, `controlWorst`, and
`controlStable` before any future variant can look like a clean win.

Run:

- Release: `native/build-codex-gtk-autodetect2/native-test-runs/20260616-212243-c4b04caf`
- Tests: `ctest --test-dir native\build-codex-gtk-autodetect2 -C Release --output-on-failure`
  passed 4/4 in 334.90s.

Frontier scout rows remain exact and observe-only:

- 32,768-digit multiply: ratio `1.934`, worst `2.391`, stable `0/3`,
  duplicate-control ratio `1.231`, control worst `1.634`.
- 32,768-digit square: ratio `1.696`, worst `1.871`, stable `0/3`,
  duplicate-control ratio `0.562`, control worst `1.103`.
- 65,536-digit multiply: ratio `1.612`, worst `2.075`, stable `0/3`,
  duplicate-control ratio `1.316`, control worst `1.525`.
- 65,536-digit square: ratio `2.049`, worst `2.944`, stable `0/3`,
  duplicate-control ratio `0.688`, control worst `1.277`.

Decision: keep these frontier paths as mapping evidence only. The control
ratios show enough same-route noise at these sizes that no future large-number
candidate should be promoted without duplicate-default and adjacent-size checks.

## 2026-06-16: Large Frontier Scout Rows

MFastFermat `main` advanced to `697745b` with two lessons relevant to Number
X-Ray: frontier benchmark families now extend to 8M/16M-bit cases, and very
large Karatsuba tuning avoids the unsafe 96-limb recursive band by gating a
128-limb fallback at a much larger root floor. Number X-Ray is not ready to run
multi-million-bit product rows inside the default native test ladder, but the
old 16,384-digit ceiling also hid the first above-64k-bit behavior.

Run:

- Release: `native/build-codex-gtk-autodetect2/native-test-runs/20260616-210038-c4b04caf`

This pass adds bounded `frontier-scout` rows for 32,768 and 65,536 decimal
digits across multiply and square. They use full-width operands, exact
`mpz_mul` oracle checks through limb import, three paired samples, one warmup
pass, and `noAutoRoute=1`. These rows are evidence only; they cannot set
`replacementReady`.

Fresh local rows:

- 32,768-digit multiply: ratio `1.588`, worst `2.434`, stable `0/3`,
  `observe-only`.
- 32,768-digit square: ratio `1.559`, worst `1.580`, stable `0/3`,
  `observe-only`.
- 65,536-digit multiply: ratio `1.810`, worst `1.862`, stable `0/3`,
  `observe-only`.
- 65,536-digit square: ratio `2.069`, worst `2.098`, stable `0/3`,
  `observe-only`.

Decision: use the scout rows to make large-root gaps visible in the GUI,
frontier text, TSV, and JSON before testing any new large multiplication route.
Do not promote threshold pockets from these rows; they are a bounded map of
where to aim next.

## 2026-06-16: Karatsuba Threshold96 Root-Size Gate

Run:

- Release: `native/build-codex-gtk-autodetect2/native-test-runs/20260616-203743-c4b04caf`

The threshold tournament made `96` limbs look attractive at 8192 and 16384
digits in an isolated kernel pass, but that is exactly the kind of local win
that can disappear under product-shaped codegen or adjacent sizes. This pass
adds a portable `threshold96-ge8192` product-policy probe plus a forced-neighbor
gate over 4096, 8192, and 16384 digits. The production multiply route is
unchanged.

Policy rows:

- 1000 digits: active candidate is the current scratch multiply below the gate,
  ratio `0.699`, worst `0.844`, stable `5/5`, `observe-only`.
- 4096 digits: active candidate is the current scratch multiply below the gate,
  ratio `0.926`, worst `1.369`, stable `3/5`, `observe-only`.
- 8192 digits: active candidate is `karatsuba-threshold96`, ratio `1.053`,
  worst `1.118`, stable `2/5`, `observe-only`.
- 16384 digits: active candidate is `karatsuba-threshold96`, ratio `1.270`,
  worst `1.506`, stable `0/5`, `observe-only`.

Forced-neighbor gate:

- `threshold96-ge8192`, sizes 4096/8192/16384: `neighbor-regression`,
  `safeSizes=0/3`, max ratio `1.321`, max worst-pair ratio `2.273`,
  `replacementReady=false`, `noAutoRoute=1`, `adoption=observe-only`.

Decision: reject `threshold96-ge8192` as a production multiply route on this
laptop. The probe stays in the benchmark/report surface as a guardrail and a
reminder that threshold wins must be root-size gated, neighbor checked, and
confirmed under product-like builds before adoption.

## 2026-06-16: MFastFermat-Inspired Square Leaf Order Probe

Run:

- Release: `native/build-codex-gtk-autodetect2/native-test-runs/20260616-201650-c4b04caf`

The hourly MFastFermat watch reported a new default-off wide61 DIF/DIT NTT
probe on `michaelreaton/MFastFermat` `main` (`3242d72`) that avoids a separate
bit-reversal pass in the square-product route. Number X-Ray does not have that
NTT layer yet, so this pass translated the lesson into a safe local analogue:
`xray_bigint_square_fused_leaf_probe()` keeps the production Karatsuba square
shape but fuses the schoolbook leaf diagonal pass into the row pass. The probe
is exported, documented, oracle-checked against `mpz_mul`, and benchmarked
against current scratch square with `noAutoRoute=1`.

Fresh local rows:

- 1000 digits: ratio `0.959`, worst `1.021`, stable `3/5`,
  `candidate-no-margin`, `observe-only`.
- 4096 digits: ratio `0.998`, worst `1.163`, stable `2/5`,
  `candidate-no-margin`, `observe-only`.
- 8192 digits: ratio `1.021`, worst `1.378`, stable `1/5`, `current-best`,
  `observe-only`.
- 16384 digits: ratio `1.118`, worst `1.294`, stable `0/5`, `current-best`,
  `observe-only`.

Decision: the transform-order clue is real enough to keep testing, but this
schoolbook-leaf analogue is not production-worthy on the Windows laptop. It
does not clear worst-pair safety at 1000/4096 digits and regresses larger
square workloads. Future work should keep watching MFastFermat's true sparse
square-product/NTT path rather than routing this fused leaf into production.

## 2026-06-16: Shallow Decimal Policy Gates Need Deep Confirmation

Run:

- Release: `native/build-codex-gtk-autodetect2/native-test-runs/20260616-195258-c4b04caf`

This pass fixes a benchmark-reporting weakness exposed by the 10^19
pre-inverted decimal formatter experiments. A shallow `format-policy-safety`
row could previously label a bounded 768..1000 digit window as
`policy-ready/promotion-ready` even when deeper 9-sample safety rows rejected
the same family. The shallow rows now remain visible as evidence, but any
`preinv10e19` shallow safety row is capped at `needs-deep-safety`,
`replacementReady=false`, and `adoption=observe-only`.

Fresh evidence from the run:

- `preinv10e19-window768-1000`: shallow ratio `0.896`, worst `0.981`, stable
  `2/2`; now `needs-deep-safety`, not promotion-ready.
- `deep-preinv10e19-window768-1000`: ratio `0.796`, worst `1.126`, stable
  `2/2`; rejected by worst-pair safety.
- `deep-preinv10e19-pairs-window768-896`: ratio `0.786`, worst `0.929`, stable
  `2/2`; single-build promotion evidence only.
- `deep-preinv10e19-pairs-window768-960`: ratio `0.774`, worst `0.928`, stable
  `2/2`; single-build promotion evidence only.

Decision: keep the pair-window clue alive, but do not route it into production
formatting from this Release artifact alone. It needs the same `/GL`
cross-build and adjacent-window confirmation as other threshold/root-size
candidates before any default formatter policy changes.

## 2026-06-16: Cross-Build Benchmark Compare

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-115718-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-120216-c4b04caf`

The CLI now compares two benchmark TSV artifacts directly:

```powershell
native\build-codex-pair-route\Release\xray_cli.exe --bench-compare `
  native\build-codex-pair-route\native-test-runs\20260616-115718-c4b04caf\benchmark.tsv `
  native\build-codex-ltcg\native-test-runs\20260616-120216-c4b04caf\benchmark.tsv
```

Fresh Release versus product-like `/GL` result:

- rows: left `744`, right `744`, matched `744`
- ready in both builds: `112`
- ready in only one build: `85`
- median wins rejected by worst-pair safety: `208`

Best both-build-ready examples:

- `gcd-u32`, 150 digits: Release ratio `0.283`, worst `0.344`; `/GL` ratio
  `0.298`, worst `0.346`
- `parse-chunk chunkDigits=15`, 16384 digits: Release ratio `0.426`, worst
  `0.435`; `/GL` ratio `0.348`, worst `0.391`
- `mod-u32`, 150 digits: Release ratio `0.441`, worst `0.443`; `/GL` ratio
  `0.383`, worst `0.383`

Rows ready in only one build included `square-policy current-default` at 1000
digits and several formatter/multiply threshold probes. They remain
non-promotable because build posture changes their worst-pair behavior.

Decision: use cross-build compare before promoting threshold, static/dynamic
precompute, AVX/BMI/ADX, or root-size-gated policies. A candidate can be
interesting in one artifact, but it cannot become a production route unless the
same row key is ready in both build fingerprints and neither artifact has a
worst-pair regression.

## 2026-06-16: Benchmark Build Fingerprint

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-110823-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-111404-c4b04caf`

Benchmark JSON, TSV, and `benchmark_frontier.txt` now identify the loaded
library's compiler and build posture. This makes broad tournaments
self-describing instead of relying on artifact folder names or memory of the
build command.

Release fingerprint:

- compiler `MSVC 1929 full=192930159`
- config `Release`
- IPO/LTO `false`
- compile target `scalar` (`compileTargetAvx2=false`)

`/GL` fingerprint:

- compiler `MSVC 1929 full=192930159`
- config `Release`
- IPO/LTO `true`
- compile target `scalar` (`compileTargetAvx2=false`)

Decision: treat build fingerprint as required context for AVX/BMI/ADX,
threshold, and static/dynamic precompute tournaments. A row is not comparable
without CPU features plus compiler/build metadata.

## 2026-06-16: Frontier Report Shows Worst-Pair Rejections

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-104009-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-104558-c4b04caf`

The workspace `benchmark_frontier.txt` now includes a sorted "median wins
rejected by worst-pair safety" section and prints `Worst` beside `Ratio` in the
scratch, policy, policy-gate, and kernel-probe tables. This makes the
human-readable report match the benchmark adoption gate instead of requiring a
separate TSV inspection.

Release rows surfaced by the new frontier section:

- `gcd-u32-precompute`, 150 digits: ratio `0.942`, worst pair `4.018`, stable
  `3/5`, `observe-only`
- `square-vs-mul`, 150 digits: ratio `0.958`, worst pair `2.208`, stable
  `3/5`, `observe-only`
- `mul-threshold`, 8192 digits: ratio `0.966`, worst pair `2.002`, stable
  `3/5`, `observe-only`

`/GL` rows surfaced by the new frontier section:

- `mod-u32-precompute`, 512 digits: ratio `0.998`, worst pair `2.904`, stable
  `2/5`, `observe-only`
- `format-pair-writer`, 150 digits: ratio `0.812`, worst pair `2.516`, stable
  `3/5`, `observe-only`
- `square-karatsuba-vs-mul`, 16384 digits: ratio `0.780`, worst pair `2.508`,
  stable `3/5`, `observe-only`
- safety `preinv10e19-pairs-window768-1000`, 1000 digits: ratio `0.793`, worst
  pair `1.138`, stable `2/2`, `worst-pair-regression`

Decision: use the frontier report, not only raw TSV, when reviewing broad
tournaments. Any candidate with a median win and a worst pair above `1.0` stays
diagnostic until the paired samples are stable in both Release and product-like
`/GL`.

## 2026-06-16: Worst-Pair Readiness Gate

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-101356-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-102125-c4b04caf`

This pass hardens benchmark readiness so a candidate cannot advertise
replacement or promotion readiness when any paired sample is slower than the
baseline, even if the paired median is faster. Those rows now report
`status=worst-pair-regression`, `replacementReady=false`, and
`adoption=observe-only`.

Release row caught by the guard:

- safety `preinv10e19-window768-1000`, 1000 digits: ratio `0.925`, worst pair
  `1.288`, stable `2/2`, `worst-pair-regression`

`/GL` row caught by the guard:

- safety `preinv10e19-pairs-window768-1000`, 1000 digits: ratio `0.904`, worst
  pair `1.036`, stable `2/2`, `worst-pair-regression`

Decision: keep the candidate evidence, but reject adoption readiness whenever
the worst paired sample regresses. This avoids the threshold trap where a small
root-size or bounded-window row looks good by median while still losing in a
product-like paired run.

## 2026-06-16: Bounded Preinv 10^19 Policy Gate Rejected By `/GL`

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-094647-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-095225-c4b04caf`

This pass adds bounded `format-policy` rows for the 10^19 pre-inverted basecase
formatter. Unlike a simple `ge1000` threshold, the policy has both `minDigits`
and `maxDigits`, so it can test a narrow 768-1000 digit window without routing
4096+ digit inputs into a candidate already known to regress. The policy rows
export `activeCandidate=current-scratch-format` outside that window, and the
safety rows force the candidate at 768 and 1000 digits.

Release rows:

- `preinv10e19-window768-1000`, 1000 digits: ratio `0.880`, worst pair
  `1.002`, stable `4/5`, `needs-safety-gate`
- `preinv10e19-pairs-window768-1000`, 1000 digits: ratio `0.807`, worst pair
  `0.867`, stable `5/5`, `needs-safety-gate`
- safety `preinv10e19-window768-1000`: neighbor ratio `0.527`, gate ratio
  `0.839`, stable `2/2`, `policy-ready`
- safety `preinv10e19-pairs-window768-1000`: neighbor ratio `0.581`, gate ratio
  `0.815`, stable `2/2`, `policy-ready`

`/GL` rows:

- `preinv10e19-window768-1000`, 1000 digits: ratio `0.973`, worst pair
  `1.092`, stable `3/5`, `needs-stability`
- `preinv10e19-pairs-window768-1000`, 1000 digits: ratio `0.961`, worst pair
  `1.910`, stable `3/5`, `needs-stability`
- safety `preinv10e19-window768-1000`: neighbor ratio `0.712`, gate ratio
  `1.027`, stable `1/2`, `gate-regression`
- safety `preinv10e19-pairs-window768-1000`: neighbor ratio `0.731`, gate ratio
  `0.946`, stable `1/2`, `gate-regression`

Decision: rejected for production routing. The raw 1000-digit preinv kernel
win remains real, but the MPIR-facing bounded policy does not survive
product-like `/GL` codegen. Keep the bounded-window policy and safety machinery
as a guardrail for future small-input candidates; do not enable the 10^19 preinv
formatter by default.

## 2026-06-16: Decimal 10^19 Preinv Divide Probes Need A Size Gate

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-090851-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-091520-c4b04caf`

This pass adds two evidence-only rows that replace the per-limb hardware
division inside the repeated divide-by-10^19 basecase with the existing
pre-inverted single-limb estimator. The pair-writer variant combines that
pre-inverted divider with the earlier two-digit emission probe. The route is
exactly parity-tested against the oracle formatter, but it is not a production
route.

Release rows:

- `format-divide-1e19-preinv`, 1000 digits: ratio `0.472`, worst pair `0.571`,
  stable `5/5`, `candidate-faster`
- `format-divide-1e19-preinv`, 4096 digits: ratio `1.297`, worst pair `1.407`,
  stable `0/5`, `baseline-faster`
- `format-divide-1e19-preinv`, 8192 digits: ratio `1.688`, worst pair `1.956`,
  stable `0/5`, `baseline-faster`
- `format-divide-1e19-preinv`, 16384 digits: ratio `2.168`, worst pair
  `2.265`, stable `0/5`, `baseline-faster`
- `format-divide-1e19-preinv-pairs`, 1000 digits: ratio `0.544`, worst pair
  `0.706`, stable `5/5`, `candidate-faster`
- `format-divide-1e19-preinv-pairs`, 4096 digits: ratio `1.207`, worst pair
  `1.647`, stable `0/5`, `baseline-faster`
- `format-divide-1e19-preinv-pairs`, 8192 digits: ratio `1.707`, worst pair
  `2.026`, stable `0/5`, `baseline-faster`
- `format-divide-1e19-preinv-pairs`, 16384 digits: ratio `2.054`, worst pair
  `2.388`, stable `0/5`, `baseline-faster`

`/GL` rows:

- `format-divide-1e19-preinv`, 1000 digits: ratio `0.396`, worst pair `0.443`,
  stable `5/5`, `candidate-faster`
- `format-divide-1e19-preinv`, 4096 digits: ratio `1.162`, worst pair `1.333`,
  stable `0/5`, `baseline-faster`
- `format-divide-1e19-preinv`, 8192 digits: ratio `1.586`, worst pair `1.782`,
  stable `0/5`, `baseline-faster`
- `format-divide-1e19-preinv`, 16384 digits: ratio `1.892`, worst pair
  `2.067`, stable `0/5`, `baseline-faster`
- `format-divide-1e19-preinv-pairs`, 1000 digits: ratio `0.428`, worst pair
  `0.705`, stable `5/5`, `candidate-faster`
- `format-divide-1e19-preinv-pairs`, 4096 digits: ratio `1.149`, worst pair
  `1.302`, stable `0/5`, `baseline-faster`
- `format-divide-1e19-preinv-pairs`, 8192 digits: ratio `1.578`, worst pair
  `1.700`, stable `0/5`, `baseline-faster`
- `format-divide-1e19-preinv-pairs`, 16384 digits: ratio `2.022`, worst pair
  `2.082`, stable `0/5`, `baseline-faster`

Decision: rejected as a global replacement. The 1000-digit result is a real,
repeatable win in both Release and product-like `/GL`, but 4096 digits and above
regress. The only safe follow-up is a root-size-gated policy with a forced
neighbor row below the gate; do not infer adoption from the isolated probe rows.

## 2026-06-16: Decimal 10^19 Pair Writer Probe Rejected

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-083834-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-084522-c4b04caf`

This pass adds an evidence-only `format-divide-1e19-pairs` row. The candidate
keeps the same repeated divide-by-10^19 basecase shape as
`format-divide-1e19`, but emits 19-digit chunks through the existing two-digit
lookup table instead of one decimal digit at a time. The clue comes from the
GMP/MPIR `mpn_get_str` comments that call out digit emission inside the
basecase conversion as a separable optimization area.

Release rows:

- 1000 digits: ratio `2.001`, worst pair `2.465`, stable `0/5`,
  `baseline-faster`
- 4096 digits: ratio `3.399`, worst pair `3.679`, stable `0/5`,
  `baseline-faster`
- 8192 digits: ratio `4.828`, worst pair `5.597`, stable `0/5`,
  `baseline-faster`
- 16384 digits: ratio `6.242`, worst pair `6.692`, stable `0/5`,
  `baseline-faster`

`/GL` rows:

- 1000 digits: ratio `2.425`, worst pair `2.652`, stable `0/5`,
  `baseline-faster`
- 4096 digits: ratio `3.878`, worst pair `4.011`, stable `0/5`,
  `baseline-faster`
- 8192 digits: ratio `4.891`, worst pair `5.235`, stable `0/5`,
  `baseline-faster`
- 16384 digits: ratio `5.723`, worst pair `6.805`, stable `0/5`,
  `baseline-faster`

Decision: rejected for production routing. The pair writer sometimes lowers the
Release ratio versus the older divide-by-10^19 emitter, but `/GL` does not
confirm the improvement and both builds remain far behind the current formatter.
Keep the row as a tournament datapoint and move the next search toward the
larger basecase divide shape or normalized powtab ideas instead of digit
emission alone.

## 2026-06-16: Decimal Preinv Policy Gates Rejected

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-080431-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-081027-c4b04caf`

This pass adds MPIR-facing `format-policy` rows for the integrated decimal
workspace and pre-inverted qhat candidates. It also adds forced-neighbor
`format-policy-safety` rows so a root-size threshold cannot look good at the
active size while silently regressing the adjacent size below the gate.

Release policy rows:

- `workspace-ge4096-leaf16`, 4096 digits: ratio `1.720`, worst pair `2.186`,
  stable `0/5`, `backend-faster`
- `preinv-ge4096-leaf8`, 4096 digits: ratio `1.941`, worst pair `2.151`,
  stable `0/5`, `backend-faster`
- `preinv-ge8192-leaf16`, 8192 digits: ratio `1.744`, worst pair `1.897`,
  stable `0/5`, `backend-faster`
- `preinv-ge16384-leaf16`, 16384 digits: ratio `1.954`, worst pair `2.014`,
  stable `0/5`, `backend-faster`

Release safety rows:

- `workspace-ge4096-leaf16`, neighbor 3072 / gate 4096: ratio `1.833`, stable
  `0/2`, `neighbor-regression`
- `preinv-ge4096-leaf8`, neighbor 3072 / gate 4096: ratio `1.688`, stable
  `0/2`, `neighbor-regression`
- `preinv-ge8192-leaf16`, neighbor 6144 / gate 8192: ratio `1.718`, stable
  `0/2`, `neighbor-regression`
- `preinv-ge16384-leaf16`, neighbor 12288 / gate 16384: ratio `1.977`, stable
  `0/2`, `neighbor-regression`

`/GL` policy rows:

- `workspace-ge4096-leaf16`, 4096 digits: ratio `1.739`, worst pair `2.060`,
  stable `0/5`, `backend-faster`
- `preinv-ge4096-leaf8`, 4096 digits: ratio `1.693`, worst pair `2.112`,
  stable `0/5`, `backend-faster`
- `preinv-ge8192-leaf16`, 8192 digits: ratio `1.832`, worst pair `1.848`,
  stable `0/5`, `backend-faster`
- `preinv-ge16384-leaf16`, 16384 digits: ratio `1.829`, worst pair `2.116`,
  stable `0/5`, `backend-faster`

`/GL` safety rows:

- `workspace-ge4096-leaf16`, neighbor 3072 / gate 4096: ratio `1.828`, stable
  `0/2`, `neighbor-regression`
- `preinv-ge4096-leaf8`, neighbor 3072 / gate 4096: ratio `1.633`, stable
  `0/2`, `neighbor-regression`
- `preinv-ge8192-leaf16`, neighbor 6144 / gate 8192: ratio `1.920`, stable
  `0/2`, `neighbor-regression`
- `preinv-ge16384-leaf16`, neighbor 12288 / gate 16384: ratio `1.826`, stable
  `0/2`, `neighbor-regression`

Decision: rejected for production routing. The earlier primitive and integrated
formatter probes remain useful clues, but they do not beat MPIR when applied as
a real decimal formatting policy. Keep these rows as regression guards and
continue searching for a candidate that survives both MPIR-facing policy timing
and forced-neighbor threshold safety.

## 2026-06-16: Integrated Decimal Division Probes

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-073024-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-073937-c4b04caf`

The formatter now has two diagnostic direct-output D&C probes that integrate
division ideas into the actual decimal formatter shape:

- `format-dc-workspace`: rebuilds the per-split divisor context but reuses
  caller-owned division workspace.
- `format-dc-preinv-qhat`: uses the same workspace plus the pre-inverted qhat
  estimator inside each recursive split.

Both routes are parity-tested against MPIR output in the native tests. Their
benchmark rows use interleaved alternating timing against the current scratch
formatter, so they measure route-shape impact instead of an isolated primitive.

Release rows:

- `format-dc-preinv-qhat`, 1000 digits leaf 8: ratio `0.827`, worst pair
  `1.088`, stable `4/5`
- `format-dc-preinv-qhat`, 4096 digits leaf 8: ratio `0.930`, worst pair
  `0.978`, stable `5/5`
- `format-dc-preinv-qhat`, 8192 digits leaf 8: ratio `1.011`, worst pair
  `1.064`, stable `1/5`
- `format-dc-preinv-qhat`, 16384 digits leaf 16: ratio `1.070`, worst pair
  `1.104`, stable `1/5`
- `format-dc-workspace`, 4096 digits leaf 8: ratio `0.958`, worst pair
  `0.998`, stable `4/5`
- `format-dc-workspace`, 8192 digits leaf 16: ratio `1.019`, worst pair
  `1.102`, stable `1/5`

`/GL` rows:

- `format-dc-preinv-qhat`, 1000 digits leaf 8: ratio `0.852`, worst pair
  `0.891`, stable `5/5`
- `format-dc-preinv-qhat`, 4096 digits leaf 8: ratio `0.823`, worst pair
  `0.879`, stable `5/5`
- `format-dc-preinv-qhat`, 8192 digits leaf 16: ratio `0.925`, worst pair
  `0.984`, stable `4/5`
- `format-dc-preinv-qhat`, 16384 digits leaf 16: ratio `0.935`, worst pair
  `0.966`, stable `5/5`
- `format-dc-workspace`, 4096 digits leaf 16: ratio `0.881`, worst pair
  `0.918`, stable `5/5`
- `format-dc-workspace`, 16384 digits leaf 16: ratio `0.916`, worst pair
  `0.980`, stable `5/5`

Decision: evidence-only. `/GL` strongly suggests that pre-inverted qhat can help
inside the real formatter, but Release rejects the larger sizes and the
MPIR-facing `format-policy` rows still report backend-faster for the direct
D&C policies. Do not promote this into production formatting until the
candidate has a dedicated MPIR-facing policy gate, forced-neighbor safety row,
and agreement across Release plus `/GL`.

## 2026-06-16: Decimal Route Probe Interleaving

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-065130-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-065652-c4b04caf`

The `format-dc-route` benchmark now times the direct-output D&C candidate and
ladder baseline in alternating chunks inside each sample, and flips which side
runs first from sample to sample. The detail string records
`timing=interleaved-alternating-batchN` so future artifacts show whether a row
used old block timing or the lower-drift interleaved method.

Decision: measurement-only. This does not route direct-output D&C into
production formatting; it improves the evidence quality for any future route
proposal.

Release interleaved route rows:

- 1000 digits: ratio `0.798`, worst pair `0.861`, stable `5/5`
- 4096 digits: ratio `0.921`, worst pair `1.041`, stable `3/5`
- 8192 digits: ratio `0.921`, worst pair `1.037`, stable `3/5`
- 16384 digits: ratio `0.933`, worst pair `0.970`, stable `5/5`

`/GL` interleaved route rows:

- 1000 digits: ratio `0.924`, worst pair `0.980`, stable `4/5`
- 4096 digits: ratio `0.922`, worst pair `1.031`, stable `3/5`
- 8192 digits: ratio `0.877`, worst pair `0.908`, stable `5/5`
- 16384 digits: ratio `0.973`, worst pair `1.057`, stable `4/5`

The interleaved route probe is now more internally consistent, but the
MPIR-facing product-policy safety rows still reject promotion. Release reports
neighbor-regression for `direct-ge4096-leaf8` at ratio `1.877` and
`direct-ge8192-leaf16` at ratio `2.020`; `/GL` reports neighbor-regression at
`1.745` and `1.925`. Production formatting remains on the ladder route until
direct-output D&C also wins the MPIR-facing policy gate.

## 2026-06-16: Decimal Direct-Output Route Rejected

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-062244-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-062814-c4b04caf`

The benchmark now emits a required `format-dc-route` row that compares the
tempting direct-output divide-and-conquer formatter with the current ladder
formatter in the same run: `decimal-dc-direct-writer-leaf16` versus
`decimal-dc-pow2-ladder-leaf8`. This row exists specifically to prevent a
formatter threshold from being promoted by comparing independent best samples or
by looking only at a single large-size pocket.

Release route rows:

- 1000 digits: ratio `0.972`, worst pair `1.318`, stable `3/5`, `observe-only`
- 4096 digits: ratio `0.931`, worst pair `1.019`, stable `4/5`,
  `promote-candidate`
- 8192 digits: ratio `0.949`, worst pair `0.973`, stable `5/5`,
  `promote-candidate`
- 16384 digits: ratio `0.996`, worst pair `1.036`, stable `2/5`,
  `observe-only`

`/GL` route rows:

- 1000 digits: ratio `0.916`, worst pair `1.367`, stable `3/5`, `observe-only`
- 4096 digits: ratio `1.046`, worst pair `1.130`, stable `2/5`, `observe-only`
- 8192 digits: ratio `0.941`, worst pair `1.226`, stable `3/5`, `observe-only`
- 16384 digits: ratio `0.903`, worst pair `1.064`, stable `4/5`,
  `promote-candidate`

The `/GL` result rejects the 4096/8192 windows that looked tempting in Release,
while Release does not confirm the `/GL` 16384 pocket strongly enough to form a
root-size gate. The product-policy safety rows also remain rejected against
MPIR: `format-policy-safety direct-ge4096-leaf8` reports neighbor-regression
with ratio `1.934` in Release and `1.989` under `/GL`; `direct-ge8192-leaf16`
reports neighbor-regression with ratio `1.946` in Release and `1.930` under
`/GL`.

Decision: keep production formatting on
`format-dc-ladder>=4096 digits leaf=8`. The direct-output formatter remains a
diagnostic scout with exact parity coverage, but no global threshold or
root-size-gated threshold is routed until same-run Release, `/GL`, neighbor
safety, and MPIR-facing policy rows all agree.

## 2026-06-16: Multiply Threshold Forced-Neighbor Gate

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-052236-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-052910-c4b04caf`

The benchmark now emits `mul-policy-safety` policy-gate rows for the two
tempting Toom-3+unroll4 route candidates. These rows force the candidate below
and at the proposed handoff instead of reusing ordinary `mul-policy` rows, which
can fall back to current scratch multiplication below `minDigits`. This prevents
the exact failure mode where a root-size or threshold candidate looks good at
one size, then hurts an adjacent size or flips under product-like `/GL` codegen.

Release rows:

- `mul-policy-safety toom3-u4-ge8192-leaf48`: `neighbor-regression`, safe sizes
  `2/3`, max ratio `1.073`, max worst pair `1.298`, `replacementReady=false`,
  `observe-only`
- `mul-policy-safety toom3-u4-rec-ge16384-leaf64-depth2`:
  `neighbor-regression`, safe sizes `0/2`, max ratio `1.035`, max worst pair
  `1.218`, `replacementReady=false`, `observe-only`
- Ordinary `mul-policy toom3-u4-ge8192-leaf48`, 4096 digits: ratio `0.803`,
  stable `5/5`, but `activeCandidate=current-scratch-mul`; this row is useful
  product-route evidence, not proof that the candidate is safe below threshold.
- Ordinary `mul-policy toom3-u4-ge8192-leaf48`, 8192 digits: ratio `1.059`,
  stable `1/5`, `observe-only`

`/GL` rows:

- `mul-policy-safety toom3-u4-ge8192-leaf48`: `neighbor-regression`, safe sizes
  `0/3`, max ratio `1.081`, max worst pair `1.234`, `replacementReady=false`,
  `observe-only`
- `mul-policy-safety toom3-u4-rec-ge16384-leaf64-depth2`:
  `neighbor-regression`, safe sizes `0/2`, max ratio `1.101`, max worst pair
  `1.537`, `replacementReady=false`, `observe-only`
- Some standalone `/GL` kernel probes for one-level Toom-3+unroll4 reported
  `promote-candidate` at 8192/16384 digits, but the forced policy gate rejected
  the route window. Kernel wins remain evidence only.

Decision: do not route either Toom-3+unroll4 multiply policy. The safety gate
proved that both candidates have adjacent-size or `/GL` regressions, so a
global or root-size-gated threshold would be harmful.

## 2026-06-16: Threshold Policy No-Auto-Route Guard

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-031129-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-031653-c4b04caf`

This hardens the benchmark semantics after a product-codegen lesson: a
threshold or root-size candidate can look good in one pocket and still hurt a
nearby size or disappear under `/GL`. The benchmark now makes that impossible
to confuse with a route decision. `format-policy`, `mul-policy`, and
`square-policy` rows with a nonzero gate, leaf threshold, or depth limit are
explicit scouts only. They keep their timing evidence, but they cannot set
`replacementReady=true`; they must point to a separate forced-neighbor safety
row before adoption.

Release guard examples:

- `mul-policy toom3-u4-ge8192-leaf48`, 1000 digits: ratio `0.672`, stable
  `5/5`, status `needs-safety-gate`, `replacementReady=false`, `observe-only`
- `mul-policy toom3-u4-ge8192-leaf48`, 8192 digits: ratio `1.097`, stable
  `0/5`, `observe-only`
- `square-policy karatsuba-thr96`, 1000 digits: ratio `0.782`, stable `4/5`,
  status `needs-safety-gate`, `replacementReady=false`, `observe-only`
- `format-policy-safety direct-ge4096-leaf8`: neighbor ratio `1.777`, gate
  ratio `1.660`, `neighbor-regression`, `observe-only`
- `qhat-u32-limb`: ratio `1.509`, stable `0/5`, `noAutoRoute=1`,
  `observe-only`

`/GL` guard examples:

- `mul-policy toom3-u4-ge8192-leaf48`, 1000 digits: ratio `0.612`, stable
  `5/5`, status `needs-safety-gate`, `replacementReady=false`, `observe-only`
- `mul-policy toom3-u4-ge8192-leaf48`, 8192 digits: ratio `0.911`, stable
  `3/5`, `observe-only`
- `square-policy karatsuba-thr96`, 1000 digits: ratio `0.872`, stable `4/5`,
  status `needs-safety-gate`, `replacementReady=false`, `observe-only`
- `format-policy-safety direct-ge4096-leaf8`: neighbor ratio `1.727`, gate
  ratio `1.675`, `neighbor-regression`, `observe-only`
- `qhat-u32-limb`: ratio `1.524`, stable `0/5`, `noAutoRoute=1`,
  `observe-only`

The normal Release run also showed why `/GL` remains mandatory:
`divmod-dc-power` at 4096 digits reported ratio `0.825` with `5/5` stable
pairs, but the `/GL` build flipped the same row to ratio `1.045` with `0/5`
stable pairs. That is a benchmark clue, not a route decision.

Decision: keep threshold/root-size ideas as measured scouts until they pass
forced neighbor rows and product-like `/GL` rows. Do not promote a global or
root-size-gated threshold from a single fast row.

## 2026-06-16: Pre-Inverted Qhat Probe

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-033345-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-033820-c4b04caf`

The qhat tournament now tests a normalized pre-inverted top-limb estimator
beside the failed 32-bit-limb Knuth estimator. This follows the same direction
as GMP's documented single-limb division strategy: pay a reciprocal setup cost
once for a divisor, then replace repeated hardware divides with multiply-high
plus correction. The benchmark keeps the setup out of the timed qhat loop and
labels the scope as `precomputeScope=per-divisor`.

Release rows:

- `qhat-preinv`: ratio `0.505`, stable `5/5`, worst pair `0.570`, exact
  parity, `replacementReady=false`, `observe-only`
- `qhat-u32-limb`: ratio `1.618`, stable `0/5`, worst pair `2.079`, exact
  parity, `replacementReady=false`, `observe-only`

`/GL` rows:

- `qhat-preinv`: ratio `0.523`, stable `5/5`, worst pair `0.580`, exact
  parity, `replacementReady=false`, `observe-only`
- `qhat-u32-limb`: ratio `1.794`, stable `0/5`, worst pair `1.931`, exact
  parity, `replacementReady=false`, `observe-only`

Decision: the pre-inverted qhat micro-kernel is promising and survived `/GL`,
but it is still `noAutoRoute=1`. The next proof must be a full division probe
that uses the pre-inverted estimator inside the normalized Knuth loop and
compares quotient/remainder parity plus product-like timings. Do not route the
micro-kernel by itself.

## 2026-06-16: Full Pre-Inverted Qhat Division Probe

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-035952-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-040612-c4b04caf`

The pre-inverted qhat idea now has a full quotient/remainder probe inside the
normalized Knuth division loop. The exported diagnostic API
`xray_bigint_divmod_preinv_qhat_probe()` keeps the same alias and workspace
contract as `xray_bigint_divmod_precomputed_workspace()`, computes the
normalized divisor top-limb reciprocal once per divisor, and uses multiply-high
plus correction for the quotient digit estimate. The benchmark compares this
candidate against the real context+workspace division path, and every sample is
checked against `mpz_tdiv_qr`.

This is deliberately not a threshold, root-size gate, or automatic route. The
row detail records `precomputeScope=per-divisor`,
`thresholdSafety=explicit-probe`, and `noAutoRoute=1`, and the result is held
at `replacementReady=false` even when the local median is faster.

Release rows:

- `divmod-preinv-qhat`, 4096 digits, 107 chunks: ratio `0.996`, stable `2/5`,
  worst pair `1.224`, `replacementReady=false`, `observe-only`
- `divmod-preinv-qhat`, 8192 digits, 215 chunks: ratio `0.825`, stable `3/5`,
  worst pair `1.110`, `replacementReady=false`, `observe-only`
- `divmod-preinv-qhat`, 16384 digits, 431 chunks: ratio `0.906`, stable `4/5`,
  worst pair `1.335`, `replacementReady=false`, `observe-only`

`/GL` rows:

- `divmod-preinv-qhat`, 4096 digits, 107 chunks: ratio `0.948`, stable `3/5`,
  worst pair `1.220`, `replacementReady=false`, `observe-only`
- `divmod-preinv-qhat`, 8192 digits, 215 chunks: ratio `0.963`, stable `3/5`,
  worst pair `0.999`, `replacementReady=false`, `observe-only`
- `divmod-preinv-qhat`, 16384 digits, 431 chunks: ratio `0.995`, stable `2/5`,
  worst pair `1.015`, `replacementReady=false`, `observe-only`

Decision: keep the full pre-inverted qhat division path as an explicit
diagnostic/importer probe only. It survives product-like `/GL` better than the
earlier division scouts, but it is not stable enough for routing: neighboring
sizes still show worst-pair regressions and the 16384 digit `/GL` row drops to
only `2/5` stable pairs. This directly avoids the threshold failure mode where
a promising large-size row becomes a harmful global or root-size-gated default.

## 2026-06-16: Pre-Inverted Qhat Safety Gate

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-044535-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-045049-c4b04caf`

The benchmark now emits a `divmod-preinv-qhat-safety` policy-gate row that
aggregates the existing 4096, 8192, and 16384 digit full divmod probe rows.
This row does not rerun the expensive division cases; it turns the measured
same-run rows into one threshold-safety verdict with
`thresholdSafety=forced-neighbor`, `forcedCandidate=yes`, `noAutoRoute=1`, and
`replacementReady=false`.

Release rows:

- `divmod-preinv-qhat`, 4096 digits: ratio `0.946`, stable `3/5`, worst pair
  `1.089`, `observe-only`
- `divmod-preinv-qhat`, 8192 digits: ratio `0.992`, stable `2/5`, worst pair
  `1.031`, `observe-only`
- `divmod-preinv-qhat`, 16384 digits: ratio `1.062`, stable `2/5`, worst pair
  `1.234`, `observe-only`
- `divmod-preinv-qhat-safety`: `neighbor-regression`, safe sizes `0/3`,
  max ratio `1.062`, max worst pair `1.234`, `observe-only`

`/GL` rows:

- `divmod-preinv-qhat`, 4096 digits: ratio `0.868`, stable `4/5`, worst pair
  `0.985`, `observe-only`
- `divmod-preinv-qhat`, 8192 digits: ratio `0.965`, stable `4/5`, worst pair
  `1.034`, `observe-only`
- `divmod-preinv-qhat`, 16384 digits: ratio `1.067`, stable `2/5`, worst pair
  `1.529`, `observe-only`
- `divmod-preinv-qhat-safety`: `neighbor-regression`, safe sizes `1/3`,
  max ratio `1.067`, max worst pair `1.529`, `observe-only`

Decision: do not route pre-inverted qhat from the tempting `/GL` 4096/8192
rows. The safety row captures the important counter-evidence in the same
human-readable and machine-readable report: 16384 digits regressed badly, so a
root-size or threshold route would be unsafe.

## 2026-06-16: Reusable Division Workspace Probe

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-023849-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-024328-c4b04caf`

The scratch bigint API now exposes `XrayBigIntDivisionWorkspace` and
`xray_bigint_divmod_precomputed_workspace()` for tools that want to reuse
temporary buffers across repeated divisions by a precomputed divisor. The
benchmark emits `divmod-workspace` rows that compare workspace reuse against
the explicit `divmod-precomputed` API in the same run, while still checking
quotient/remainder parity against `mpz_tdiv_qr`.

Safety update: explicit-only division probes now always report
`replacementReady=false` and `adoption=observe-only`, even when a local median
looks good. Their detail keeps `noAutoRoute=1`, so these rows cannot be
mistaken for a production threshold or automatic route.

Release rows:

- `divmod-dc-power`, 4096 digits, 107 chunks: ratio `1.177`, stable `0/5`,
  worst pair `1.476`, `observe-only`
- `divmod-dc-power`, 8192 digits, 215 chunks: ratio `1.523`, stable `0/5`,
  worst pair `1.670`, `observe-only`
- `divmod-dc-power`, 16384 digits, 431 chunks: ratio `1.946`, stable `0/5`,
  worst pair `2.090`, `observe-only`
- `divmod-precomputed`, 4096 digits, 107 chunks: ratio `0.912`, stable `5/5`,
  worst pair `0.934`, `replacementReady=false`, `observe-only`
- `divmod-precomputed`, 8192 digits, 215 chunks: ratio `0.940`, stable `4/5`,
  worst pair `1.071`, `replacementReady=false`, `observe-only`
- `divmod-precomputed`, 16384 digits, 431 chunks: ratio `0.945`, stable `5/5`,
  worst pair `0.963`, `replacementReady=false`, `observe-only`
- `divmod-workspace`, 4096 digits, 107 chunks: ratio `1.038`, stable `0/5`,
  worst pair `1.040`, `replacementReady=false`, `observe-only`
- `divmod-workspace`, 8192 digits, 215 chunks: ratio `0.994`, stable `2/5`,
  worst pair `1.190`, `replacementReady=false`, `observe-only`
- `divmod-workspace`, 16384 digits, 431 chunks: ratio `1.012`, stable `2/5`,
  worst pair `1.077`, `replacementReady=false`, `observe-only`

`/GL` rows:

- `divmod-dc-power`, 4096 digits, 107 chunks: ratio `1.040`, stable `1/5`,
  worst pair `1.294`, `observe-only`
- `divmod-dc-power`, 8192 digits, 215 chunks: ratio `1.410`, stable `0/5`,
  worst pair `1.682`, `observe-only`
- `divmod-dc-power`, 16384 digits, 431 chunks: ratio `1.786`, stable `0/5`,
  worst pair `2.140`, `observe-only`
- `divmod-precomputed`, 4096 digits, 107 chunks: ratio `1.033`, stable `1/5`,
  worst pair `1.817`, `replacementReady=false`, `observe-only`
- `divmod-precomputed`, 8192 digits, 215 chunks: ratio `1.024`, stable `1/5`,
  worst pair `1.087`, `replacementReady=false`, `observe-only`
- `divmod-precomputed`, 16384 digits, 431 chunks: ratio `1.003`, stable `2/5`,
  worst pair `1.121`, `replacementReady=false`, `observe-only`
- `divmod-workspace`, 4096 digits, 107 chunks: ratio `1.017`, stable `1/5`,
  worst pair `1.135`, `replacementReady=false`, `observe-only`
- `divmod-workspace`, 8192 digits, 215 chunks: ratio `0.963`, stable `3/5`,
  worst pair `1.169`, `replacementReady=false`, `observe-only`
- `divmod-workspace`, 16384 digits, 431 chunks: ratio `1.079`, stable `1/5`,
  worst pair `1.224`, `replacementReady=false`, `observe-only`

Decision: keep the workspace API for importers and as a diagnostic probe, but
do not route it. Reusing the temporary numerator/remainder buffers does not
move the product-like `/GL` division picture. The remaining division gap is not
mostly allocator churn; it points back to GMP-style quotient digit
pre-inversion and divide-and-conquer division.

## 2026-06-16: Precomputed Divisor Context Probe

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-014157-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-014646-c4b04caf`

The scratch bigint library now exposes `XrayBigIntDivisorContext` for repeated
division by the same full-width divisor. The context caches a private divisor
copy plus the normalized divisor and normalization shift used by the existing
Knuth-style division loop. The benchmark emits `divmod-precomputed` rows that
compare the context API against the current scratch `xray_bigint_divmod` in the
same run, while verifying quotient/remainder parity against `mpz_tdiv_qr`.

This is a precompute probe, not a threshold or production route. The row detail
includes `thresholdSafety=explicit-context` and `noAutoRoute=1` so a local
same-size win cannot become a harmful global default.

Release rows:

- `divmod-dc-power`, 4096 digits, 107 chunks: ratio `1.037`, stable `1/5`,
  worst pair `1.167`, `observe-only`
- `divmod-dc-power`, 8192 digits, 215 chunks: ratio `1.403`, stable `1/5`,
  worst pair `1.418`, `observe-only`
- `divmod-dc-power`, 16384 digits, 431 chunks: ratio `1.806`, stable `0/5`,
  worst pair `2.222`, `observe-only`
- `divmod-precomputed`, 4096 digits, 107 chunks: ratio `1.021`, stable `2/5`,
  worst pair `1.041`, `observe-only`
- `divmod-precomputed`, 8192 digits, 215 chunks: ratio `0.991`, stable `1/5`,
  worst pair `1.061`, `observe-only`
- `divmod-precomputed`, 16384 digits, 431 chunks: ratio `1.097`, stable `1/5`,
  worst pair `1.133`, `observe-only`

`/GL` rows:

- `divmod-dc-power`, 4096 digits, 107 chunks: ratio `1.055`, stable `1/5`,
  worst pair `1.419`, `observe-only`
- `divmod-dc-power`, 8192 digits, 215 chunks: ratio `1.148`, stable `0/5`,
  worst pair `1.273`, `observe-only`
- `divmod-dc-power`, 16384 digits, 431 chunks: ratio `1.633`, stable `0/5`,
  worst pair `2.132`, `observe-only`
- `divmod-precomputed`, 4096 digits, 107 chunks: ratio `1.186`, stable `1/5`,
  worst pair `1.437`, `observe-only`
- `divmod-precomputed`, 8192 digits, 215 chunks: ratio `0.919`, stable `3/5`,
  worst pair `1.344`, `observe-only`
- `divmod-precomputed`, 16384 digits, 431 chunks: ratio `0.968`, stable `3/5`,
  worst pair `1.053`, `observe-only`

Decision: keep the context as an explicit importer API and benchmark probe only.
The idea is plausible for repeated large divisions, but the product-like `/GL`
run still has worst-pair regressions and only 3 of 5 stable samples at the
interesting sizes. Do not turn this into a global/root-size threshold. The next
division work should move toward GMP's deeper clues: pre-inverted top-limb
division, divide-and-conquer division, and measured thresholds with forced
neighbor guards.

## 2026-06-16: Threshold Neighbor Guard

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-004924-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-005500-c4b04caf`

The benchmark now emits `format-policy-safety` rows for threshold/root-size
format policies. Each row forces the candidate at a neighbor size below the gate
and at the gate itself; both sizes must pass exact parity, same-run paired
median speed, and paired-sample stability before the policy can be promoted.
This directly guards against a threshold that looks good at one large size but
hurts an adjacent smaller size under product-like codegen.

Release guard rows:

- `direct-ge4096-leaf8`: worst ratio `2.236`, stable sizes `0/2`,
  `neighbor-regression`, `observe-only`
- `direct-ge8192-leaf16`: worst ratio `1.580`, stable sizes `0/2`,
  `neighbor-regression`, `observe-only`
- `static-ge4096-l16`: worst ratio `1.671`, stable sizes `0/2`,
  `neighbor-regression`, `observe-only`
- `static-ge8192-l8`: worst ratio `1.993`, stable sizes `0/2`,
  `neighbor-regression`, `observe-only`

`/GL` guard rows:

- `direct-ge4096-leaf8`: worst ratio `2.073`, stable sizes `0/2`,
  `neighbor-regression`, `observe-only`
- `direct-ge8192-leaf16`: worst ratio `2.162`, stable sizes `0/2`,
  `neighbor-regression`, `observe-only`
- `static-ge4096-l16`: worst ratio `2.126`, stable sizes `0/2`,
  `neighbor-regression`, `observe-only`
- `static-ge8192-l8`: worst ratio `1.971`, stable sizes `0/2`,
  `neighbor-regression`, `observe-only`

Decision: keep all tested decimal format threshold policies unrouted. The guard
does what we want: no single-size or non-product-codegen win can become a route
without proving the adjacent-size behavior too.

## 2026-06-16: D&C Division Primitive Probe

Runs:

- Release: `native/build-codex-pair-route/native-test-runs/20260616-011458-c4b04caf`
- `/GL`: `native/build-codex-ltcg/native-test-runs/20260616-012046-c4b04caf`

The benchmark now emits `divmod-dc-power` rows for the multi-limb division shape
used by decimal divide-and-conquer formatting: a large numerator divided by
`10^(19 * chunks)` near the half-split point. The row compares the scratch
Knuth-style division against `mpz_tdiv_qr` with exact quotient/remainder parity.

Release rows:

- 4096 digits, 107 chunks: ratio `0.860`, stable `3/5`, worst pair `1.222`,
  `observe-only`
- 8192 digits, 215 chunks: ratio `1.275`, stable `0/5`, worst pair `2.165`,
  `observe-only`
- 16384 digits, 431 chunks: ratio `1.547`, stable `0/5`, worst pair `1.866`,
  `observe-only`

`/GL` rows:

- 4096 digits, 107 chunks: ratio `0.770`, stable `4/5`, worst pair `1.455`,
  `promote-candidate`
- 8192 digits, 215 chunks: ratio `1.004`, stable `2/5`, worst pair `1.355`,
  `observe-only`
- 16384 digits, 431 chunks: ratio `1.254`, stable `0/5`, worst pair `1.426`,
  `observe-only`

Decision: expose exact full bigint `xray_bigint_divmod` for importers and keep
the D&C division rows as benchmark evidence. The large-size format gap now has a
measured primitive suspect: multi-limb division remains behind GMP/MPIR at 8192
and 16384 digit D&C split sizes, even under `/GL`.

## 2026-06-15: Toom-3 Full-Shape Gate

Run: `runs/20260615-041302-c4b04caf`

`mul-toom3-vs-scratch` compared one-level Toom-3 against current scratch multiply:

- 4096 digits, leaf 32: ratio `1.207`, stable `0/5`, `observe-only`
- 4096 digits, leaf 64: ratio `1.011`, stable `1/5`, `observe-only`
- 8192 digits, leaf 32: ratio `1.113`, stable `0/5`, `observe-only`
- 8192 digits, leaf 64: ratio `0.955`, stable `3/5`, `observe-only`

Decision: do not route Toom-3 into production multiply yet. The 8192 digit leaf
64 median was interesting, but it did not clear the 4 of 5 stability gate.

## 2026-06-15: Karatsuba Threshold Scout

Run: `runs/20260615-042112-c4b04caf`

A guarded dynamic threshold tried 32-limb Karatsuba leaves only for large inputs.
Production multiply stayed oracle-only:

- 4096 digits: ratio `1.03`
- 8192 digits: ratio `1.11`

Decision: rejected. The change did not improve the large rows and made the 4096
row noisier.

## 2026-06-15: BMI2/ADX Primitive Probe

Run: `runs/20260615-042614-c4b04caf`

`muladd-bmi2-adx` compared `_mulx_u64`/`_addcarryx_u64` with the existing
`_umul128`/`_addcarry_u64` multiply-accumulate primitive on this laptop:

- 617 digits: ratio `0.87`, stable `5/5`, `candidate-faster`
- 4933 digits: ratio `0.84`, stable `4/5`, `candidate-faster`

Decision: keep the probe. BMI2/ADX is locally real enough to investigate, and it
is a better bigint lead than a broad AVX2 build flag.

## 2026-06-15: BMI2/ADX Full-Route Scout

Run: `runs/20260615-044154-c4b04caf`

An unmerged scout threaded the single-chain BMI2/ADX primitive through the full
scratch multiply shape and compared it directly with current scratch multiply:

- 1000 digits: ratio `1.06`, baseline faster
- 4096 digits: ratio `1.16`, baseline faster
- 8192 digits: ratio `1.03`, baseline faster

Decision: rejected and not merged. The primitive win did not survive the current
Karatsuba/schoolbook operation shape. Future BMI2/ADX work should not be a simple
single-chain replacement; it should test a GMP-style addmul design with better
carry scheduling before touching production routing.

## 2026-06-15: Muladd Unroll Scheduling Probe

Run: `runs/20260615-045717-c4b04caf`

`muladd-unroll4` compared a four-limb unrolled `_umul128`/`_addcarry_u64`
multiply-accumulate loop with the scalar `_umul128`/`_addcarry_u64` loop:

- 617 digits: ratio `0.93`, stable `3/5`, `observe-only`
- 4933 digits: ratio `0.64`, stable `4/5`, `candidate-faster`

Decision: keep the probe. The larger row suggests loop scheduling can matter
more than swapping to BMI2/ADX alone. Production multiply should still remain
unchanged until a full multiply route proves exact parity and a stable same-run
win.

## 2026-06-15: Muladd Unroll Width Probe

Run: `runs/20260615-050656-c4b04caf`

`muladd-unroll8` was added beside `muladd-unroll4` to check whether wider loop
scheduling helps:

- unroll4, 617 digits: ratio `0.79`, stable `5/5`, `candidate-faster`
- unroll8, 617 digits: ratio `0.90`, stable `3/5`, `observe-only`
- unroll4, 4933 digits: ratio `0.79`, stable `5/5`, `candidate-faster`
- unroll8, 4933 digits: ratio `0.87`, stable `5/5`, `candidate-faster`

Decision: keep the width probe, but prefer unroll4 as the current scheduling
candidate. Wider unroll did not improve the small row and lost to unroll4 on
both measured sizes in this run.

## 2026-06-15: Unroll4 Full-Shape Probe

Run: `runs/20260615-052029-c4b04caf`

`mul-unroll4-vs-scratch` threaded the four-limb `_umul128`/`_addcarry_u64`
schedule through the full scratch multiply path and compared it directly with
current scratch multiply:

- 1000 digits: ratio `0.85`, stable `5/5`, `candidate-faster`
- 4096 digits: ratio `0.84`, stable `4/5`, `candidate-faster`
- 8192 digits: ratio `0.90`, stable `3/5`, `observe-only`

Decision: keep the probe, but do not route production multiply through it yet.
The 1000 and 4096 digit rows cleared the same-run gate, but the 8192 digit row
missed the 4 of 5 stability requirement. The same run also showed the raw
muladd primitive rows as noisy or slower, so the next step is confirmation and
carry-scheduling work rather than adoption.

## 2026-06-15: Unroll4 GMP Gate

Run: `runs/20260615-053253-c4b04caf`

`mul-unroll4-vs-gmp` compared the full unroll4 multiply candidate directly
against `mpz_mul`:

- 1000 digits: ratio `0.667`, stable `5/5`, `candidate-faster`
- 4096 digits: ratio `0.844`, stable `5/5`, `candidate-faster`
- 8192 digits: ratio `1.009`, stable `2/5`, `observe-only`

Decision: keep the direct GMP gate and do not adopt the route globally. The
candidate is locally safe-looking through 4096 digits in this run, but 8192
digits still fails the GMP gate. A future production route must be bounded to
the proven size window or collect stronger 8192+ evidence before replacing the
current path.

## 2026-06-15: Unroll4 Full Ladder Gate

Run: `runs/20260615-053942-c4b04caf`

The direct unroll4 GMP gate was expanded to the full scratch multiply ladder:

- 40 digits: vs scratch ratio `1.154`, vs GMP ratio `0.941`, stable `3/5`,
  `observe-only`
- 150 digits: vs scratch ratio `1.020`, vs GMP ratio `0.654`, stable `5/5`,
  `candidate-faster`
- 1000 digits: vs scratch ratio `0.833`, vs GMP ratio `0.658`, stable `5/5`,
  `candidate-faster`
- 4096 digits: vs scratch ratio `0.820`, vs GMP ratio `0.688`, stable `5/5`,
  `candidate-faster`
- 8192 digits: vs scratch ratio `0.921`, vs GMP ratio `1.185`, stable `1/5`,
  `observe-only`

Decision: do not use unroll4 globally. The locally safe-looking window starts
above tiny inputs and ends before the 8192 digit row. A later route scout may
try an MSVC-only bounded path for approximately 150 to 4096 digit operands, but
40 digit and 8192+ operands must stay on the current production path unless new
evidence changes those rows.

## 2026-06-15: Bounded Unroll4 Production Route

Final corrected run: `runs/20260615-055050-c4b04caf`

The production `mul` path was routed through the unroll4 leaf only on MSVC x64
when operands are not tiny, not too large, and not strongly unbalanced
(`8 <= min limbs`, `max limbs <= 256`, and `min * 3 >= max * 2`).

Production `mul` rows:

- 40 digits: ratio `0.792`, stable `5/5`, `replacement-ready`
- 150 digits: ratio `0.636`, stable `5/5`, `replacement-ready`
- 1000 digits: ratio `0.649`, stable `5/5`, `replacement-ready`
- 4096 digits: ratio `0.825`, stable `5/5`, `replacement-ready`
- 8192 digits: ratio `1.156`, stable `0/5`, `oracle-only`

Route probe rows against the scalar threshold baseline:

- 40 digits: ratio `1.203`, stable `1/5`, `observe-only`
- 150 digits: ratio `0.979`, stable `4/5`, `candidate-faster`
- 1000 digits: ratio `0.722`, stable `5/5`, `candidate-faster`
- 4096 digits: ratio `0.862`, stable `5/5`, `candidate-faster`
- 8192 digits: ratio `0.787`, stable `3/5`, `observe-only`

Decision: keep the bounded route. It improves the 4096 digit production
adoption row without routing the 8192 digit case, and it preserves the small
40 digit row by keeping it outside the route. The scalar-threshold probe remains
in the benchmark so future changes can still compare the route against the
non-routed path.

## 2026-06-15: Unroll4 Deep GMP Gate

Runs: `runs/20260615-060503-c4b04caf`, `runs/20260615-061507-c4b04caf`

The 8192 digit unroll4 rows were noisy across five-sample runs, so the
benchmark now emits a stricter `mul-unroll4-deep-vs-gmp` gate for the 4096 and
8192 digit rows using nine paired samples. Deep rows require 8 of 9 paired
samples inside the speed gate before they can be marked `promote-candidate`.

Initial gate run:

- 4096 digits: ratio `0.791`, stable `9/9`, worst pair ratio `0.925`,
  `candidate-faster`
- 8192 digits: ratio `1.017`, stable `3/9`, worst pair ratio `1.328`,
  `gmp-faster`

Strict-threshold confirmation run:

- 4096 digits: ratio `0.905`, stable `7/9`, worst pair ratio `1.063`,
  `observe-only`
- 8192 digits: ratio `1.012`, stable `3/9`, worst pair ratio `1.263`,
  `observe-only`

Decision: keep the production route bounded below the 8192 digit row. The deep
gate supports the existing 4096 production window, but it is not stable enough
to justify widening the route, and the 8192 digit result is still too noisy and
sometimes slower than GMP. A future 8192+ route needs a different algorithmic
primitive, not just a wider unroll4 switch.

## 2026-06-15: Add/Sub Measurement Floor

Run: `runs/20260615-062215-c4b04caf`

The add/sub scratch rows were sometimes flipping adoption at 4096 and 8192
digits because the sample windows were only a few hundred microseconds. The
benchmark now uses a higher add/sub iteration floor for 1000+ digit operands.
This does not change the scratch bigint implementation; it only lengthens the
measurement window.

- 4096 digit add: ratio `0.745`, stable `5/5`, `replacement-ready`
- 4096 digit sub: ratio `0.743`, stable `5/5`, `replacement-ready`
- 8192 digit add: ratio `0.799`, stable `5/5`, `replacement-ready`
- 8192 digit sub: ratio `0.818`, stable `5/5`, `replacement-ready`

Decision: keep the higher iteration floor. The benchmark still finishes in
about ten seconds locally while removing noisy large add/sub oracle-only
flips. Remaining scratch oracle-only rows are parse at 4096/8192 digits and
multiply at 8192 digits.

## 2026-06-15: 16384 Digit Multiply Discovery Tier

Run: `runs/20260615-063047-c4b04caf`

The next multiply frontier is larger than the current 8192 digit scratch gate,
but parse is being worked separately. The benchmark now adds 16384 digit rows
only to multiply-specific surfaces: scratch `mul`, runtime threshold probes,
Toom-3 probes, and unroll4 GMP gates. This is evidence collection only; no
production route changes are made by this tier.

- 16384 digit scratch `mul`: ratio `1.307`, stable `1/5`, `oracle-only`
- 16384 digit unroll4 vs scratch: ratio `0.693`, stable `5/5`,
  `candidate-faster`
- 16384 digit unroll4 vs GMP: ratio `1.040`, stable `2/5`, `gmp-faster`
- 16384 digit deep unroll4 vs GMP: ratio `1.143`, stable `3/9`,
  `gmp-faster`
- Best 16384 digit Toom-3 vs scratch row: ratio `0.940`, stable `3/5`,
  `candidate-no-margin`

Decision: do not widen the unroll4 route. The 16384 rows show that unroll4
improves our scalar scratch path but does not catch GMP at this size; GMP is
winning from higher-level multiplication strategy, not just leaf scheduling.
The next larger-number multiply work should target algorithm structure above
the leaf loop.

## 2026-06-15: Toom-3 + Unroll4 Leaf Probe

Run: `runs/20260615-070407-c4b04caf`

The benchmark now combines the one-level Toom-3 split with the bounded MSVC x64
unroll4 leaf schedule. This is an evidence-only probe for the larger-number
frontier: the candidate must still pass exact GMP parity, and the production
route is unchanged unless the same-run stability gate is met.

- 8192 digits, leaf 32 vs scratch: ratio `1.357`, stable `1/5`,
  `observe-only`
- 8192 digits, leaf 32 vs GMP: ratio `0.994`, stable `2/5`, `observe-only`
- 8192 digits, leaf 64 vs scratch: ratio `0.900`, stable `5/5`,
  `promote-candidate`
- 8192 digits, leaf 64 vs GMP: ratio `0.973`, stable `3/5`, `observe-only`
- 16384 digits, leaf 32 vs scratch: ratio `1.007`, stable `2/5`,
  `observe-only`
- 16384 digits, leaf 32 vs GMP: ratio `1.303`, stable `0/5`, `observe-only`
- 16384 digits, leaf 64 vs scratch: ratio `0.851`, stable `5/5`,
  `promote-candidate`
- 16384 digits, leaf 64 vs GMP: ratio `1.092`, stable `1/5`, `observe-only`

Decision: keep the probe, but do not route production multiplication through it
yet. Leaf 64 is a clear improvement over the current scratch path at 8192 and
16384 digits, which validates combining algorithm structure with leaf
scheduling. It still does not beat GMP with enough stability, so the next
larger-number step should explore deeper Toom thresholds or Karatsuba/Toom
handoff policy before any replacement claim.

## 2026-06-15: Toom-3 + Unroll4 Deep GMP Gate

Run: `runs/20260615-071642-c4b04caf`

The leaf 64 Toom-3+unroll4 row sometimes looks like a near GMP win in the
ordinary five-sample benchmark, so the benchmark now emits
`mul-toom3-unroll4-deep-vs-gmp` rows for 8192 and 16384 digits with nine paired
samples. Deep rows need 8 of 9 paired samples inside the speed gate before they
can become promotion candidates.

Five-sample rows from this run:

- 8192 digits, leaf 64 vs scratch: ratio `0.862`, stable `5/5`,
  `promote-candidate`
- 8192 digits, leaf 64 vs GMP: ratio `1.000`, stable `2/5`,
  `observe-only`
- 16384 digits, leaf 64 vs scratch: ratio `0.748`, stable `5/5`,
  `promote-candidate`
- 16384 digits, leaf 64 vs GMP: ratio `1.052`, stable `2/5`,
  `observe-only`

Deep GMP rows:

- 8192 digits, leaf 64 vs GMP: ratio `0.986`, stable `4/9`, worst pair ratio
  `1.047`, `observe-only`
- 16384 digits, leaf 64 vs GMP: ratio `1.125`, stable `3/9`, worst pair ratio
  `1.188`, `observe-only`

Decision: do not route production multiplication through Toom-3+unroll4 yet.
The combined candidate is a real improvement over the current scratch path, but
the stricter GMP gate shows it is not a stable GMP replacement on this laptop.
Next work should test a more GMP-like handoff ladder, not simply reuse the same
one-level Toom split with more confidence.

## 2026-06-15: Toom-3 + Unroll4 Handoff Scout

Run: `runs/20260615-072856-c4b04caf`

The benchmark now adds GMP-facing Toom-3+unroll4 scout rows at leaf thresholds
`24`, `48`, and `96` for 8192 and 16384 digit operands. The goal is to learn
whether a GMP-like handoff threshold changes the picture before any production
route is touched. Leaf 96 also gets the nine-sample deep GMP gate because it
looked promising in local scout rows.

Five-sample GMP rows:

- 8192 digits, leaf 24: ratio `1.239`, stable `0/5`, `observe-only`
- 8192 digits, leaf 48: ratio `1.004`, stable `1/5`, `observe-only`
- 8192 digits, leaf 64: ratio `0.998`, stable `0/5`, `observe-only`
- 8192 digits, leaf 96: ratio `1.048`, stable `2/5`, `observe-only`
- 16384 digits, leaf 24: ratio `1.389`, stable `0/5`, `observe-only`
- 16384 digits, leaf 48: ratio `0.954`, stable `3/5`, `observe-only`
- 16384 digits, leaf 64: ratio `1.184`, stable `0/5`, `observe-only`
- 16384 digits, leaf 96: ratio `0.859`, stable `3/5`, `observe-only`

Deep GMP rows:

- 8192 digits, leaf 64: ratio `1.016`, stable `3/9`, worst pair ratio
  `1.622`, `observe-only`
- 8192 digits, leaf 96: ratio `0.868`, stable `9/9`, worst pair ratio
  `0.929`, `promote-candidate`
- 16384 digits, leaf 64: ratio `1.090`, stable `2/9`, worst pair ratio
  `1.349`, `observe-only`
- 16384 digits, leaf 96: ratio `0.923`, stable `5/9`, worst pair ratio
  `1.336`, `observe-only`

Decision: keep leaf 96 as the next bounded route candidate for the 8192 digit
frontier only. It survived the deep GMP gate at 8192, but not at 16384. The
next route experiment should be explicitly bounded by size and should keep the
16384 row as a guardrail, not as an adoption target.

## 2026-06-15: Bounded Toom-3 + Unroll4 Route Rejected

Runs: `native/build-codex-fresh/native-test-runs/20260615-073725-c4b04caf`,
`runs/20260615-073822-c4b04caf`

A temporary production scratch route sent balanced `384..512` limb operands
through Toom-3+unroll4 with a 96-limb leaf. The intended adoption target was
the 8192 digit multiply row, while 16384 digits remained the guardrail.

CTest benchmark with the route:

- 8192 digit scratch `mul`: ratio `1.314`, stable `2/5`, worst pair ratio
  `1.693`, `oracle-only`
- 16384 digit scratch `mul`: ratio `1.326`, stable `0/5`, worst pair ratio
  `1.479`, `oracle-only`
- 8192 digit deep leaf 96 vs GMP: ratio `1.006`, stable `4/9`,
  `observe-only`

CLI benchmark with the same route:

- 8192 digit scratch `mul`: ratio `0.903`, stable `4/5`, worst pair ratio
  `1.006`, `replacement-ready`
- 16384 digit scratch `mul`: ratio `1.240`, stable `2/5`, worst pair ratio
  `1.352`, `oracle-only`
- 8192 digit deep leaf 96 vs GMP: ratio `1.005`, stable `4/9`,
  `observe-only`

Decision: reject the route and leave production multiplication unchanged. The
ordinary scratch row was too run-sensitive, and the deep GMP gate did not
confirm the candidate. This is useful evidence, but not permission to route.

## 2026-06-15: Recursive Toom-3 + Unroll4 Probe

Runs: `native/build-codex-fresh/native-test-runs/20260615-075145-c4b04caf`,
`runs/20260615-075234-c4b04caf`

The benchmark now includes `mul-toom3-u4-rec-vs-gmp` rows at the 16384 digit
frontier. The candidate uses depth-limited recursive Toom-3 with the MSVC x64
unroll4 leaf. A direct 12000 digit oracle test verifies exact output parity
against GMP before benchmark rows are trusted.

CTest benchmark:

- 16384 digits, leaf 64, depth 2: ratio `0.930`, stable `4/5`, worst pair
  ratio `1.266`, `promote-candidate`
- 16384 digits, leaf 96, depth 2: ratio `1.103`, stable `1/5`, worst pair
  ratio `1.317`, `observe-only`

CLI benchmark:

- 16384 digits, leaf 64, depth 2: ratio `1.082`, stable `0/5`, worst pair
  ratio `1.413`, `observe-only`
- 16384 digits, leaf 96, depth 2: ratio `1.178`, stable `1/5`, worst pair
  ratio `1.258`, `observe-only`

Decision: keep the recursive probe, but do not route it or add a deep gate yet.
The leaf 64 result was promising in one run and disappeared in the canonical
CLI benchmark. This points to benchmark noise or operand-shape sensitivity, not
a proven GMP replacement.

## 2026-06-15: Recursive Toom-3 + Unroll4 Deep Gate

Run: `runs/20260615-080354-c4b04caf`

The recursive leaf 64 row produced contradictory five-sample results, so the
benchmark now emits `mul-toom3-u4-rec-deep-vs-gmp` for the 16384 digit frontier
with nine paired samples.

CTest benchmark:

- 16384 digits, leaf 64, depth 2: ratio `1.092`, stable `1/5`,
  `observe-only`
- 16384 digits, leaf 96, depth 2: ratio `1.103`, stable `1/5`,
  `observe-only`
- 16384 digits, leaf 64, depth 2 deep gate: ratio `1.135`, stable `0/9`,
  worst pair ratio `1.724`, `observe-only`

CLI benchmark:

- 16384 digits, leaf 64, depth 2: ratio `1.016`, stable `2/5`,
  `observe-only`
- 16384 digits, leaf 96, depth 2: ratio `0.872`, stable `4/5`,
  `promote-candidate`
- 16384 digits, leaf 64, depth 2 deep gate: ratio `1.061`, stable `2/9`,
  worst pair ratio `1.451`, `observe-only`

Decision: reject recursive leaf 64 as a route candidate. The deep gate shows it
does not beat GMP consistently. Leaf 96 had a noisy five-sample win in the CLI
run, but it needs its own deep gate before it can become more than a scout row.

## 2026-06-15: Recursive Toom-3 Leaf 96 Deep Gate

Run: `runs/20260615-081355-c4b04caf`

The leaf 96 recursive row also looked noisy in prior runs, so the benchmark now
emits a second `mul-toom3-u4-rec-deep-vs-gmp` row at the 16384 digit frontier.
Both recursive leaf thresholds are now checked with nine paired samples before
they can influence any production route.

CTest benchmark:

- 16384 digits, leaf 64, depth 2: ratio `1.099`, stable `2/5`,
  `observe-only`
- 16384 digits, leaf 96, depth 2: ratio `1.103`, stable `1/5`,
  `observe-only`
- 16384 digits, leaf 64, depth 2 deep gate: ratio `1.080`, stable `0/9`,
  worst pair ratio `1.452`, `observe-only`
- 16384 digits, leaf 96, depth 2 deep gate: ratio `1.093`, stable `1/9`,
  worst pair ratio `1.183`, `observe-only`

CLI benchmark:

- 16384 digits, leaf 64, depth 2: ratio `0.856`, stable `4/5`,
  `promote-candidate`
- 16384 digits, leaf 96, depth 2: ratio `1.040`, stable `2/5`,
  `observe-only`
- 16384 digits, leaf 64, depth 2 deep gate: ratio `0.977`, stable `5/9`,
  worst pair ratio `1.567`, `observe-only`
- 16384 digits, leaf 96, depth 2 deep gate: ratio `1.103`, stable `2/9`,
  worst pair ratio `1.387`, `observe-only`

Decision: reject recursive leaf 96 as a route candidate. The deep gate shows it
does not beat GMP, and the broader recursive experiment still has high
worst-pair variance even when a five-sample scout row looks attractive.

## 2026-06-15: Specialized Square Basecase Probe

Runs: `native-test-runs/20260615-083151-c4b04caf` and
`runs/20260615-082901-c4b04caf`

GMP documents squaring as a separate basecase path because the off-diagonal
cross-products can be computed once and doubled instead of doing a full
rectangular multiply (`https://gmplib.org/manual/Basecase-Multiplication.html`).
The scratch engine now has an oracle-tested
`xray_bigint_square` primitive and emits `square` rows in the benchmark ladder,
but the production multiply route is unchanged.

CTest benchmark:

- 40 digits: ratio `1.038`, stable `0/5`, `oracle-only`
- 150 digits: ratio `0.676`, stable `5/5`, `allowed`
- 1000 digits: ratio `0.874`, stable `5/5`, `allowed`
- 4096 digits: ratio `1.500`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `2.500`, stable `0/5`, `oracle-only`
- 16384 digits: ratio `2.673`, stable `0/5`, `oracle-only`

CLI benchmark:

- 40 digits: ratio `1.000`, stable `3/5`, `oracle-only`
- 150 digits: ratio `0.725`, stable `5/5`, `allowed`
- 1000 digits: ratio `0.849`, stable `4/5`, `allowed`
- 4096 digits: ratio `1.469`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `2.095`, stable `0/5`, `oracle-only`
- 16384 digits: ratio `3.101`, stable `0/5`, `oracle-only`

Decision: keep square as a proved primitive and benchmark row, but do not route
large multiplication through it. It only wins in the bounded schoolbook-size
middle and loses badly once Karatsuba/Toom-style multiplication should dominate.

## 2026-06-15: Specialized Square Route Gate

Runs: `native-test-runs/20260615-084310-c4b04caf` and
`runs/20260615-084419-c4b04caf`

The first square probe compared specialized square to GMP. This gate compares
the same primitive to the current scratch self-multiply path using
`square-vs-mul`, so a production route must beat the code it would replace, not
just the GMP baseline.

CTest benchmark:

- 40 digits: ratio `1.231`, stable `0/5`, `observe-only`
- 150 digits: ratio `1.000`, stable `2/5`, `observe-only`
- 1000 digits: ratio `0.724`, stable `5/5`, `promote-candidate`
- 4096 digits: ratio `0.933`, stable `3/5`, `observe-only`
- 8192 digits: ratio `1.170`, stable `0/5`, `observe-only`
- 16384 digits: ratio `1.470`, stable `0/5`, `observe-only`

CLI benchmark:

- 40 digits: ratio `1.231`, stable `0/5`, `observe-only`
- 150 digits: ratio `1.000`, stable `1/5`, `observe-only`
- 1000 digits: ratio `0.707`, stable `5/5`, `promote-candidate`
- 4096 digits: ratio `0.903`, stable `3/5`, `observe-only`
- 8192 digits: ratio `0.948`, stable `3/5`, `observe-only`
- 16384 digits: ratio `1.589`, stable `0/5`, `observe-only`

Decision: reject the production square route for now. The 1000 digit row is
real, but adjacent and larger tiers do not pass stability, so the improvement is
not broad enough to apply safely.

## 2026-06-15: Karatsuba Square Probe

Runs: `native-test-runs/20260615-085808-c4b04caf` and
`runs/20260615-085910-c4b04caf`

The schoolbook square route gate showed that a specialized square primitive is
not enough at large sizes. This probe adds an unrouted recursive Karatsuba
square candidate and compares it against both current scratch self-multiply and
`mpz_mul`.

CTest benchmark:

- `square-karatsuba-vs-mul`, 1000 digits: ratio `0.686`, stable `5/5`,
  `promote-candidate`
- `square-karatsuba-vs-gmp`, 1000 digits: ratio `0.887`, stable `5/5`,
  `promote-candidate`
- `square-karatsuba-vs-mul`, 4096 digits: ratio `0.715`, stable `4/5`,
  `promote-candidate`
- `square-karatsuba-vs-gmp`, 4096 digits: ratio `0.966`, stable `3/5`,
  `observe-only`
- `square-karatsuba-vs-mul`, 8192 digits: ratio `1.040`, stable `2/5`,
  `observe-only`
- `square-karatsuba-vs-gmp`, 8192 digits: ratio `1.433`, stable `1/5`,
  `observe-only`
- `square-karatsuba-vs-mul`, 16384 digits: ratio `0.839`, stable `3/5`,
  `observe-only`
- `square-karatsuba-vs-gmp`, 16384 digits: ratio `1.546`, stable `0/5`,
  `observe-only`

CLI benchmark:

- `square-karatsuba-vs-mul`, 1000 digits: ratio `0.718`, stable `5/5`,
  `promote-candidate`
- `square-karatsuba-vs-gmp`, 1000 digits: ratio `0.895`, stable `4/5`,
  `promote-candidate`
- `square-karatsuba-vs-mul`, 4096 digits: ratio `0.690`, stable `5/5`,
  `promote-candidate`
- `square-karatsuba-vs-gmp`, 4096 digits: ratio `1.036`, stable `1/5`,
  `observe-only`
- `square-karatsuba-vs-mul`, 8192 digits: ratio `0.596`, stable `5/5`,
  `promote-candidate`
- `square-karatsuba-vs-gmp`, 8192 digits: ratio `1.279`, stable `2/5`,
  `observe-only`
- `square-karatsuba-vs-mul`, 16384 digits: ratio `0.737`, stable `5/5`,
  `promote-candidate`
- `square-karatsuba-vs-gmp`, 16384 digits: ratio `1.521`, stable `1/5`,
  `observe-only`

Decision: keep Karatsuba square as an exact, promising probe but do not route it
yet. It beats current scratch self-multiply in useful bands, but it only beats
GMP at 1000 digits and remains behind GMP at larger sizes on this laptop.

## 2026-06-15: Karatsuba Square Threshold Scout

Runs: `native-test-runs/20260615-090857-c4b04caf` and
`runs/20260615-091005-c4b04caf`

The threshold scout adds 32, 96, and 128 limb handoff rows for the large
Karatsuba-square candidate. The goal is to see whether the 64 limb handoff was
masking a GMP-competitive route.

CTest benchmark:

- Best 4096 digit `square-karatsuba-vs-gmp`: threshold `96`, ratio `1.054`,
  stable `1/5`, `observe-only`
- Best 8192 digit `square-karatsuba-vs-gmp`: threshold `64`, ratio `1.237`,
  stable `0/5`, `observe-only`
- Best 16384 digit `square-karatsuba-vs-gmp`: threshold `128`, ratio `1.302`,
  stable `0/5`, `observe-only`
- Best 4096 digit `square-karatsuba-vs-mul`: threshold `96`, ratio `0.737`,
  stable `5/5`, `promote-candidate`
- Best 8192 digit `square-karatsuba-vs-mul`: threshold `64`, ratio `0.658`,
  stable `4/5`, `promote-candidate`
- Best 16384 digit `square-karatsuba-vs-mul`: threshold `128`, ratio `0.689`,
  stable `5/5`, `promote-candidate`

CLI benchmark:

- Best 4096 digit `square-karatsuba-vs-gmp`: threshold `96`, ratio `0.994`,
  stable `2/5`, `observe-only`
- Best 8192 digit `square-karatsuba-vs-gmp`: threshold `96`, ratio `1.207`,
  stable `1/5`, `observe-only`
- Best 16384 digit `square-karatsuba-vs-gmp`: threshold `64`, ratio `1.370`,
  stable `0/5`, `observe-only`
- Best 4096 digit `square-karatsuba-vs-mul`: threshold `32`, ratio `0.633`,
  stable `4/5`, `promote-candidate`
- Best 8192 digit `square-karatsuba-vs-mul`: threshold `96`, ratio `0.717`,
  stable `5/5`, `promote-candidate`
- Best 16384 digit `square-karatsuba-vs-mul`: threshold `64`, ratio `0.709`,
  stable `5/5`, `promote-candidate`

Decision: do not route Karatsuba square yet. Threshold tuning confirms the
candidate is useful against our current self-multiply path, but no large
threshold is stable or fast enough against GMP.

## 2026-06-15: Decimal Format Benchmark Gate

Runs: `native-test-runs/20260615-092635-c4b04caf` and
`runs/20260615-092749-c4b04caf`

The parse path already uses 19 digit chunks, so a matching 19 digit decimal
formatter was tested as a possible serialization improvement. The local run was
exact but slower than both GMP and the restored 9 digit formatter on the larger
rows, so the production formatter remains on the existing 9 digit chunk path.
The benchmark ladder now keeps `format` rows beside `parse` rows so future
formatter changes have an explicit parity and speed gate.

Rejected 19 digit formatter CTest scout:

- 40 digits: ratio `2.183`, stable `0/5`, `oracle-only`
- 150 digits: ratio `2.230`, stable `0/5`, `oracle-only`
- 1000 digits: ratio `4.232`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `6.833`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `8.975`, stable `0/5`, `oracle-only`

Restored 9 digit formatter CTest benchmark:

- 40 digits: ratio `2.101`, stable `0/5`, `oracle-only`
- 150 digits: ratio `2.691`, stable `0/5`, `oracle-only`
- 1000 digits: ratio `3.091`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `5.510`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `6.069`, stable `0/5`, `oracle-only`

Restored 9 digit formatter CLI benchmark:

- 40 digits: ratio `2.087`, stable `0/5`, `oracle-only`
- 150 digits: ratio `2.585`, stable `0/5`, `oracle-only`
- 1000 digits: ratio `3.078`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `4.475`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `5.269`, stable `0/5`, `oracle-only`

Decision: do not adopt the 19 digit formatter. Keep decimal formatting measured
as an oracle-only row until a new algorithm beats `mpz_get_str` locally with
stable same-run ratios.

## 2026-06-15: Manual Decimal Chunk Writer

Runs: `native-test-runs/20260615-093548-c4b04caf` and
`runs/20260615-093650-c4b04caf`

The previous formatter called `snprintf` once per 9 digit output chunk after the
division pass. This change keeps the same chunking and division algorithm, but
writes the leading chunk and padded 9 digit chunks with small fixed-width loops.
It is a local overhead cleanup, not a new base-conversion algorithm.

CTest benchmark:

- 40 digits: ratio `0.934`, stable `3/5`, `oracle-only`
- 150 digits: ratio `1.113`, stable `0/5`, `oracle-only`
- 1000 digits: ratio `2.304`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `4.927`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `5.641`, stable `0/5`, `oracle-only`

CLI benchmark:

- 40 digits: ratio `0.834`, stable `3/5`, `oracle-only`
- 150 digits: ratio `1.056`, stable `2/5`, `oracle-only`
- 1000 digits: ratio `2.299`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `4.410`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `4.590`, stable `0/5`, `oracle-only`

Decision: keep the manual chunk writer because it improves the measured format
rows without changing arithmetic or output shape. Formatting remains
oracle-only; the next meaningful step is an algorithmic conversion change rather
than more output-copy tuning.

## 2026-06-15: Formatter In-Place Divmod Specialization

Runs: `native-test-runs/20260615-094557-c4b04caf` and
`runs/20260615-094706-c4b04caf`

The formatter's base-1e9 conversion used the generic `xray_bigint_divmod_u32`
helper once per output chunk, even though the divisor and output destination are
fixed. This change keeps the same conversion algorithm but uses a
formatter-private in-place loop with the precomputed reciprocal for `1e9`.

CTest benchmark:

- 40 digits: ratio `0.853`, stable `5/5`, `replacement-ready`
- 150 digits: ratio `1.032`, stable `1/5`, `oracle-only`
- 1000 digits: ratio `2.234`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `4.890`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `6.260`, stable `0/5`, `oracle-only`

CLI benchmark:

- 40 digits: ratio `0.851`, stable `5/5`, `replacement-ready`
- 150 digits: ratio `1.024`, stable `2/5`, `oracle-only`
- 1000 digits: ratio `2.215`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `3.223`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `4.328`, stable `0/5`, `oracle-only`

Decision: keep the specialization. It promotes the 40 digit format row under
both CTest and CLI, and CLI shows larger rows improving too. Large decimal
formatting remains far from GMP, so the next frontier is divide-and-conquer or
larger-base conversion rather than more wrapper trimming.

## 2026-06-15: Route Karatsuba Square

Runs: `native-test-runs/20260615-095524-c4b04caf` and
`runs/20260615-095624-c4b04caf`

`xray_bigint_square` was still calling the schoolbook square implementation
directly even though the Karatsuba square probe was exact and consistently
faster against the current scratch self-multiply baseline at larger sizes. This
change routes public square through the existing threshold dispatcher. Small
operands below the handoff still use schoolbook square.

CTest benchmark:

- 40 digits: ratio `1.190`, stable `0/5`, `oracle-only`
- 150 digits: ratio `0.744`, stable `5/5`, `replacement-ready`
- 1000 digits: ratio `0.892`, stable `5/5`, `replacement-ready`
- 4096 digits: ratio `1.062`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `1.265`, stable `0/5`, `oracle-only`
- 16384 digits: ratio `1.564`, stable `0/5`, `oracle-only`

CLI benchmark:

- 40 digits: ratio `1.063`, stable `1/5`, `oracle-only`
- 150 digits: ratio `0.672`, stable `5/5`, `replacement-ready`
- 1000 digits: ratio `0.889`, stable `4/5`, `replacement-ready`
- 4096 digits: ratio `1.021`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `1.303`, stable `0/5`, `oracle-only`
- 16384 digits: ratio `1.481`, stable `0/5`, `oracle-only`

Decision: keep the route. It substantially improves large square behavior
without claiming GMP replacement status. The 40 digit row remains too small and
noisy for promotion, while 150 and 1000 digit rows stay replacement-ready.

## 2026-06-15: Widen Unroll4 Multiply Window To 512 Limbs

Runs: `native-test-runs/20260615-100414-c4b04caf`,
`runs/20260615-100513-c4b04caf`, and `runs/20260615-100626-c4b04caf`

The production unroll4 multiply route was capped at `256` limbs, so the
8192-digit benchmark stayed on the scalar leaf even though the deep unroll4
probe showed a stable 8192-digit win. This change widens the balanced MSVC x64
window to `512` limbs. The 16384-digit row remains outside the route because its
probe still loses to GMP.

CTest benchmark:

- 4096 digit `mul`: ratio `0.882`, stable `4/5`, `replacement-ready`
- 8192 digit `mul`: ratio `0.957`, stable `4/5`, `replacement-ready`
- 16384 digit `mul`: ratio `1.202`, stable `2/5`, `oracle-only`

CLI benchmark:

- 4096 digit `mul`: ratio `0.906`, stable `3/5`, `oracle-only`
- 8192 digit `mul`: ratio `0.939`, stable `3/5`, `oracle-only`
- 16384 digit `mul`: ratio `1.313`, stable `1/5`, `oracle-only`

CLI repeat benchmark:

- 4096 digit `mul`: ratio `0.873`, stable `4/5`, `replacement-ready`
- 8192 digit `mul`: ratio `0.925`, stable `5/5`, `replacement-ready`
- 16384 digit `mul`: ratio `1.155`, stable `1/5`, `oracle-only`

Decision: keep the 512 limb cap. It targets the 8192-digit band that the deep
probe supported, avoids routing the still-losing 16384-digit band, and improves
the 8192 row in all local runs even when one CLI run did not cross the stability
gate.

## 2026-06-15: Large Decimal Horner Formatter

Runs: `native-test-runs/20260615-103430-c4b04caf`,
`runs/20260615-102828-c4b04caf`, and `runs/20260615-102935-c4b04caf`

The repeated base-1e9 division formatter still scales poorly on large decimal
output. This change adds a larger-limb conversion path that builds base-1e9
decimal chunks directly from binary limbs using `decimal = decimal * 2^64 +
limb`. Small values remain on the existing division path so the already-proven
short-format rows are protected.

CTest benchmark:

- 1000 digit `format`: ratio `2.235`, stable `0/5`, `oracle-only`
- 4096 digit `format`: ratio `2.780`, stable `0/5`, `oracle-only`
- 8192 digit `format`: ratio `3.617`, stable `0/5`, `oracle-only`

CLI benchmark:

- 1000 digit `format`: ratio `2.281`, stable `0/5`, `oracle-only`
- 4096 digit `format`: ratio `3.415`, stable `0/5`, `oracle-only`
- 8192 digit `format`: ratio `3.153`, stable `0/5`, `oracle-only`

CLI repeat benchmark:

- 1000 digit `format`: ratio `1.794`, stable `0/5`, `oracle-only`
- 4096 digit `format`: ratio `3.893`, stable `0/5`, `oracle-only`
- 8192 digit `format`: ratio `5.102`, stable `0/5`, `oracle-only`

Decision: keep the Horner path as a large-format improvement, but do not treat
it as a replacement route. Scratch time improves on the large rows, especially
4096 digits, while GMP still wins decisively at the largest output sizes. The
next formatter frontier is a divide-and-conquer conversion, not another linear
chunk loop.

## 2026-06-15: Copy Subtraction Tails After Borrow Clears

Runs: `native/build-codex-fresh/native-test-runs/20260615-112350-c4b04caf`
and `runs/20260615-112502-c4b04caf`

Large subtraction was close to the adoption gate but still unstable at the
8192-digit row. The subtraction loop now stops using the borrow-chain primitive
once the right operand is exhausted and no borrow remains, then copies the
unchanged high limbs from the left operand. The route is covered by a GMP oracle
test using a large left operand, a small right operand, and aliasing back into
the left operand.

CTest benchmark:

- summary: `176/176` passed, `40` replacement-ready, `12` oracle-only
- 4096 digit `sub`: ratio `0.63`, stable `5/5`, `replacement-ready`
- 8192 digit `sub`: ratio `0.71`, stable `4/5`, `replacement-ready`

CLI benchmark:

- summary: `176/176` passed, `40` replacement-ready, `12` oracle-only
- 4096 digit `sub`: ratio `0.67`, stable `5/5`, `replacement-ready`
- 8192 digit `sub`: ratio `0.62`, stable `5/5`, `replacement-ready`

Decision: keep the tail copy. It is a semantics-preserving subtraction fast path
and moves the 8192-digit subtraction row from evidence-only to locally
replacement-ready in both local benchmark paths.

## 2026-06-15: Pre-Reserve Decimal Formatter Chunks

Runs: `native/build-codex-fresh/native-test-runs/20260615-114431-c4b04caf`
and `runs/20260615-114528-c4b04caf`

The decimal formatter now reserves a conservative decimal-chunk capacity from
the binary limb count before conversion starts. This avoids repeated chunk-array
growth in both the small repeated-division formatter and the larger Horner limb
formatter. The reserve helper also rejects impossible allocation sizes before
`realloc` size multiplication can overflow.

CTest benchmark:

- summary: `176/176` passed, `41` replacement-ready, `11` oracle-only
- 40 digit `format`: ratio `0.85`, stable `4/5`, `replacement-ready`
- 150 digit `format`: ratio `0.84`, stable `5/5`, `replacement-ready`
- 1000 digit `format`: ratio `2.21`, stable `0/5`, `oracle-only`
- 4096 digit `format`: ratio `4.05`, stable `0/5`, `oracle-only`
- 8192 digit `format`: ratio `4.47`, stable `0/5`, `oracle-only`

CLI benchmark:

- summary: `176/176` passed, `40` replacement-ready, `12` oracle-only
- 40 digit `format`: ratio `0.82`, stable `5/5`, `replacement-ready`
- 150 digit `format`: ratio `0.84`, stable `4/5`, `replacement-ready`
- 1000 digit `format`: ratio `2.20`, stable `0/5`, `oracle-only`
- 4096 digit `format`: ratio `4.23`, stable `0/5`, `oracle-only`
- 8192 digit `format`: ratio `4.03`, stable `0/5`, `oracle-only`

Decision: keep the pre-reserve path as a small formatter safety and stability
improvement. It promotes the 150-digit format row in both local runs, but it
does not change the large-number conclusion: 1000+ digit formatting still needs
a divide-and-conquer conversion rather than more linear chunk-loop tuning.

## 2026-06-15: External MFastFermat Root-Gated Threshold Lesson

Source repo: `C:\Users\mike\Documents\MFastFermat`

MFastFermat's BigCPU Montgomery square path retested Karatsuba leaf/band
thresholds on real factor-form survivor rows, not generic random multiply
inputs. The useful result was shape-gated, not global:

- A global 96-limb Karatsuba band improved the 96k/128k frontier phase rows and
  product rows, but it regressed a smaller 3180-bit worker-matched product row
  versus the existing default.
- The promoted route kept smaller roots on the prior 128-limb fallback and used
  a 96-limb band only for roots of at least 1800 limbs.
- The rebuilt default then reported `active_band_threshold=128` at 8036 bits and
  `active_band_threshold=96` at 128036 bits.
- The 128036-bit product/GMP row moved from the old default miss
  (`0.855x` in `benchmark_product_default_vs_gmp_96k128k_20260615.txt`) to a
  same-shape default win (`1.047x` in
  `benchmark_promoted_default_vs_gmp_128k_20260615.txt`), with product/GMP
  sample fingerprints checked by the harness.

Decision: carry this lesson into PayamAnalysis threshold work. Do not promote a
single multiply/square threshold just because it wins a frontier row. Record the
operand shape and route by shape or size band when needed. Treat phase probes as
scouts only; the adoption signal is the full operation under the actual app
workload, paired against the GMP oracle with matching samples and stable-run
evidence. Also keep the sum-vs-difference Karatsuba middle term as an explicit
benchmark dimension: the MFastFermat difference-form plus endpoint-copy route
survived product testing better than the classic sum-form middle in that
workload.

## 2026-06-16: External MFastFermat Frontier Swatch Lesson

Source repo: `C:\Users\mike\Documents\MFastFermat`

MFastFermat pushed its factor-form Montgomery square scout further to 192k and
256k bit shapes after a full 192k product/GMP confirmation row took about 16
minutes on the laptop. The new fast scout groups many existing compile-time
experiments into one short phase run, then reruns plausible finalists with more
repeats before any production route is considered.

Key transferable findings:

- Broad swatches need failure-tolerant harnesses. `toom3_1024` failed warmup
  self-checks on both 192k and 256k factor-form shapes, and the harness now
  reports that as a row instead of aborting the whole run.
- Broad swatches can be order-biased. One `frontier` group run produced default
  rows much slower than nearby runs, so it was used only to discover finalists,
  not to claim speedups.
- The 5-repeat finalist pass kept the production default unchanged:
  `fastredc_512` was flat at 192k and slower at 256k (`1.004x`, `0.967x`);
  `static_redc` helped 256k but hurt 192k (`1.197x`, `0.874x`);
  `copy_gap_clear`, `carry_chain`, `direct_z2`, and `hoist_redc` lost both
  sizes.

Decision: carry this into PayamAnalysis route work. A larger-size-only win must
be shape- or size-gated before it is even a product candidate, and a broad
discovery run is not an adoption gate. Keep failed-route reporting explicit, run
the finalists again with longer paired samples, and only then compare against
GMP on the actual operation shape.

## 2026-06-15: Karatsuba Middle Sum-vs-Difference Probe

Runs:

- Normal Release:
  `native/build-codex-pair-route/native-test-runs/20260615-232726-c4b04caf`
- Product-like `/GL`:
  `native/build-codex-ltcg/native-test-runs/20260615-233221-c4b04caf`

The benchmark now measures the classic sum-form Karatsuba middle term
`(a0 + a1) * (b0 + b1) - z0 - z2` against the current difference-form middle
term as a `kernel-probe`. The candidate is exact-oracle checked against GMP and
the current implementation, but it does not affect production routing.

Normal Release rows:

- 1000 digits, threshold 64: ratio `0.959`, stable `4/5`, `candidate-faster`
- 1000 digits, threshold 96: ratio `0.840`, stable `5/5`, `candidate-faster`
- 1000 digits, threshold 128: ratio `0.939`, stable `4/5`, `observe-only`
- 4096 digits, threshold 64: ratio `0.997`, stable `2/5`, `observe-only`
- 4096 digits, threshold 96: ratio `1.044`, stable `1/5`, `observe-only`
- 4096 digits, threshold 128: ratio `0.997`, stable `2/5`, `observe-only`
- 8192 digits, threshold 64: ratio `1.140`, stable `2/5`, `observe-only`
- 8192 digits, threshold 96: ratio `0.996`, stable `2/5`, `observe-only`
- 8192 digits, threshold 128: ratio `1.010`, stable `2/5`, `observe-only`
- 16384 digits, threshold 64: ratio `0.794`, stable `5/5`,
  `candidate-faster`
- 16384 digits, threshold 96: ratio `0.977`, stable `3/5`, `observe-only`
- 16384 digits, threshold 128: ratio `0.951`, stable `3/5`, `observe-only`

Product-like `/GL` rows:

- 1000 digits, threshold 64: ratio `1.011`, stable `0/5`, `observe-only`
- 1000 digits, threshold 96: ratio `0.884`, stable `4/5`, `candidate-faster`
- 1000 digits, threshold 128: ratio `1.055`, stable `1/5`, `observe-only`
- 4096 digits, threshold 64: ratio `0.943`, stable `3/5`, `observe-only`
- 4096 digits, threshold 96: ratio `0.976`, stable `3/5`, `observe-only`
- 4096 digits, threshold 128: ratio `0.902`, stable `3/5`, `observe-only`
- 8192 digits, threshold 64: ratio `0.945`, stable `3/5`, `observe-only`
- 8192 digits, threshold 96: ratio `1.028`, stable `2/5`, `observe-only`
- 8192 digits, threshold 128: ratio `0.873`, stable `4/5`,
  `candidate-faster`
- 16384 digits, threshold 64: ratio `0.977`, stable `3/5`, `observe-only`
- 16384 digits, threshold 96: ratio `0.991`, stable `1/5`, `observe-only`
- 16384 digits, threshold 128: ratio `0.954`, stable `4/5`,
  `candidate-faster`

Decision: keep the sum-form middle as a benchmark dimension only. The normal
Release winners did not reproduce cleanly under `/GL`, and several adjacent
threshold/size cells were slower or unstable. This is the same failure class as
the root-size threshold issue from MFastFermat: a pocket win is not a global
route. A future promotion must be root-size gated, product-shape tested, and
confirmed in the `/GL` build before touching production multiply.

## 2026-06-16: Static Decimal Power Table Probe

Runs:

- Normal Release:
  `native/build-codex-pair-route/native-test-runs/20260615-235944-c4b04caf`
- Product-like `/GL`:
  `native/build-codex-ltcg/native-test-runs/20260616-000445-c4b04caf`

The divide-and-conquer decimal formatter now has benchmark-only probes that use
a small built-in table for `(10^19)^(2^0)` through `(10^19)^(2^6)`. Existing
dynamic ladder and production formatting are unchanged. The static probes are
exactly checked against current scratch formatting and `mpz_get_str`.

Normal Release rows:

- static ladder, 1000 digits, leaf 8: ratio `1.303`, stable `0/5`,
  `observe-only`
- static direct, 1000 digits, leaf 8: ratio `0.930`, stable `3/5`,
  `observe-only`
- static ladder, 1000 digits, leaf 16: ratio `0.983`, stable `2/5`,
  `observe-only`
- static direct, 1000 digits, leaf 16: ratio `0.921`, stable `5/5`,
  `candidate-faster`
- static ladder, 4096 digits, leaf 8: ratio `1.093`, stable `1/5`,
  `observe-only`
- static direct, 4096 digits, leaf 8: ratio `0.822`, stable `4/5`,
  `candidate-faster`
- static ladder, 4096 digits, leaf 16: ratio `0.945`, stable `3/5`,
  `observe-only`
- static direct, 4096 digits, leaf 16: ratio `0.877`, stable `3/5`,
  `observe-only`
- static ladder, 8192 digits, leaf 8: ratio `0.969`, stable `3/5`,
  `observe-only`
- static direct, 8192 digits, leaf 8: ratio `0.938`, stable `3/5`,
  `observe-only`
- static ladder, 8192 digits, leaf 16: ratio `1.004`, stable `2/5`,
  `observe-only`
- static direct, 8192 digits, leaf 16: ratio `0.982`, stable `2/5`,
  `observe-only`
- static ladder, 16384 digits, leaf 8: ratio `0.962`, stable `3/5`,
  `observe-only`
- static direct, 16384 digits, leaf 8: ratio `0.909`, stable `4/5`,
  `candidate-faster`
- static ladder, 16384 digits, leaf 16: ratio `1.033`, stable `2/5`,
  `observe-only`
- static direct, 16384 digits, leaf 16: ratio `0.974`, stable `3/5`,
  `observe-only`

Product-like `/GL` rows:

- static ladder, 1000 digits, leaf 8: ratio `1.239`, stable `1/5`,
  `observe-only`
- static direct, 1000 digits, leaf 8: ratio `1.187`, stable `0/5`,
  `observe-only`
- static ladder, 1000 digits, leaf 16: ratio `1.213`, stable `1/5`,
  `observe-only`
- static direct, 1000 digits, leaf 16: ratio `1.139`, stable `2/5`,
  `observe-only`
- static ladder, 4096 digits, leaf 8: ratio `0.962`, stable `3/5`,
  `observe-only`
- static direct, 4096 digits, leaf 8: ratio `0.986`, stable `2/5`,
  `observe-only`
- static ladder, 4096 digits, leaf 16: ratio `0.967`, stable `3/5`,
  `observe-only`
- static direct, 4096 digits, leaf 16: ratio `0.828`, stable `5/5`,
  `candidate-faster`
- static ladder, 8192 digits, leaf 8: ratio `1.022`, stable `2/5`,
  `observe-only`
- static direct, 8192 digits, leaf 8: ratio `0.834`, stable `4/5`,
  `candidate-faster`
- static ladder, 8192 digits, leaf 16: ratio `1.013`, stable `1/5`,
  `observe-only`
- static direct, 8192 digits, leaf 16: ratio `0.955`, stable `3/5`,
  `observe-only`
- static ladder, 16384 digits, leaf 8: ratio `1.017`, stable `2/5`,
  `observe-only`
- static direct, 16384 digits, leaf 8: ratio `0.978`, stable `3/5`,
  `observe-only`
- static ladder, 16384 digits, leaf 16: ratio `0.960`, stable `5/5`,
  `candidate-faster`
- static direct, 16384 digits, leaf 16: ratio `0.931`, stable `3/5`,
  `observe-only`

Decision: keep the static table probes and do not route production formatting
through them yet. Static direct is the only credible lead, but the winning cells
move under `/GL` and 1000-digit rows regress under product-like codegen. A
future promotion should test a gated policy such as `>=4096` with leaf 16 or
`>=8192` with leaf 8 against `mpz_get_str`, not a global static D&C route.

## 2026-06-16: Static Decimal Policy Gate

Runs:

- Normal Release:
  `native/build-codex-pair-route/native-test-runs/20260616-002106-c4b04caf`
- Product-like `/GL`:
  `native/build-codex-ltcg/native-test-runs/20260616-002645-c4b04caf`

The static direct formatter was promoted only into policy probes, not
production routing. Two gated policies were tested against `mpz_get_str`:
`static-ge4096-l16` and `static-ge8192-l8`. Below the gate, each policy keeps
the current scratch formatter active so the 1000-digit regression is protected.

Normal Release rows:

- current default, 1000 digits: ratio `1.877`, stable `0/5`, `observe-only`
- current default, 4096 digits: ratio `1.779`, stable `0/5`, `observe-only`
- current default, 8192 digits: ratio `1.829`, stable `0/5`, `observe-only`
- current default, 16384 digits: ratio `1.752`, stable `0/5`, `observe-only`
- static `>=4096` leaf 16, 1000 digits: ratio `1.891`, stable `0/5`,
  `observe-only`
- static `>=4096` leaf 16, 4096 digits: ratio `1.770`, stable `0/5`,
  `observe-only`
- static `>=4096` leaf 16, 8192 digits: ratio `1.620`, stable `0/5`,
  `observe-only`
- static `>=4096` leaf 16, 16384 digits: ratio `1.703`, stable `0/5`,
  `observe-only`
- static `>=8192` leaf 8, 1000 digits: ratio `1.543`, stable `1/5`,
  `observe-only`
- static `>=8192` leaf 8, 4096 digits: ratio `1.903`, stable `0/5`,
  `observe-only`
- static `>=8192` leaf 8, 8192 digits: ratio `1.513`, stable `0/5`,
  `observe-only`
- static `>=8192` leaf 8, 16384 digits: ratio `1.538`, stable `0/5`,
  `observe-only`

Product-like `/GL` rows:

- current default, 1000 digits: ratio `1.843`, stable `0/5`, `observe-only`
- current default, 4096 digits: ratio `1.994`, stable `0/5`, `observe-only`
- current default, 8192 digits: ratio `1.828`, stable `0/5`, `observe-only`
- current default, 16384 digits: ratio `1.527`, stable `0/5`, `observe-only`
- static `>=4096` leaf 16, 1000 digits: ratio `1.856`, stable `0/5`,
  `observe-only`
- static `>=4096` leaf 16, 4096 digits: ratio `1.752`, stable `0/5`,
  `observe-only`
- static `>=4096` leaf 16, 8192 digits: ratio `1.666`, stable `0/5`,
  `observe-only`
- static `>=4096` leaf 16, 16384 digits: ratio `1.708`, stable `0/5`,
  `observe-only`
- static `>=8192` leaf 8, 1000 digits: ratio `1.842`, stable `1/5`,
  `observe-only`
- static `>=8192` leaf 8, 4096 digits: ratio `1.499`, stable `0/5`,
  `observe-only`
- static `>=8192` leaf 8, 8192 digits: ratio `1.691`, stable `0/5`,
  `observe-only`
- static `>=8192` leaf 8, 16384 digits: ratio `1.632`, stable `0/5`,
  `observe-only`

Decision: reject static direct as a production formatter policy for now. It can
beat current scratch in kernel rows, but the policy-to-GMP gate shows every
tested size still backend-faster in both normal and `/GL` builds. The next large
formatting work should attack the underlying bigint division or use a more
GMP-like conversion algorithm, not just static power precompute.
