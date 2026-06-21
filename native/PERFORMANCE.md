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

## 2026-06-18: Rejected Preinv-Qhat Formatter Route

The `format-dc-preinv-qhat` direct formatter was tested as a possible
production route for `xray_bigint_get_decimal()` at estimated 4096+ decimal
digits, with leaf `8` below the larger band and leaf `16` above it. This was a
deliberate root-size-gated threshold test, motivated by earlier same-run probe
rows and by the MFastFermat lesson that external or primitive-looking wins must
be re-proven inside the real product route before promotion.

Artifacts:

- Baseline before the route attempt:
  `native/build-codex-exact-estimate/native-test-runs/20260618-001813-c4b04caf`
- Route attempt with updated manifest labels:
  `native/build-codex-exact-estimate/native-test-runs/20260618-005029-c4b04caf`

Observed `format` rows versus MPIR:

- 4096 digits improved on median from ratio `1.790` to `1.407` (`21.4%`
  lower), but worst-pair safety regressed from `1.975` to `2.168`.
- 8192 digits improved on median from ratio `1.686` to `1.600` (`5.1%`
  lower), but worst-pair safety regressed from `1.860` to `2.187`.
- 768 and 896 digits remained unrelated scratch gaps and stayed oracle-only.
- The run's route digest moved from `routeCompleted=145`, `routeOpen=669`,
  `safetyRejected=27` to `routeCompleted=133`, `routeOpen=681`,
  `safetyRejected=31`.

Decision: rejected and not routed. The median improvement at 4096/8192 digits is
not enough because the worst-pair and threshold-safety evidence got worse, and
the MPIR-facing `format-policy-safety preinv-ge*` rows still report
neighbor-regression. Keep preinv-qhat as a diagnostic probe only. The current
production formatter remains the base-`1e19` D&C ladder at 4096+ digits, plus
the separate exact-estimate 1001 digit preinverse pair-writer window.

## 2026-06-18: Current Multiply Route Audit

MFastFermat's current MMFF/CUDA reporting keeps source-supported rows separate
from completed rows and refuses to score raw external rates as comparable
kernel evidence. Number X-Ray applies the same caution to dense multiply before
promoting any Toom-3, transform, or threshold candidate. This pass adds
`mul-route-audit` rows that time the current production multiply route against
`mpz_mul` on the same generated operands, using two operand families, deep
interleaved samples, exact decimal parity, and hash counts.

Artifact:

- `native/build-codex-exact-estimate/native-test-runs/20260618-011909-c4b04caf`

Observed rows:

- Ordinary 4096 digit scratch multiply: ratio `0.836`, worst pair `0.975`,
  stable `5/5`, `replacement-ready`.
- Ordinary 8192 digit scratch multiply: ratio `1.247`, worst pair `1.466`,
  stable `1/5`, `oracle-only`.
- Ordinary 16384 digit scratch multiply: ratio `1.386`, worst pair `1.876`,
  stable `1/5`, `oracle-only`.
- `mul-route-audit current-default-16384`: exact hash/parity `18/18`, ratio
  `1.309`, worst pair `1.582`, `safeSizes=0/1`, `observe-only`.
- `mul-route-audit current-default-4096-16384`: exact hash/parity `54/54`,
  max ratio `1.331`, max worst pair `1.536`, `safeSizes=1/3`,
  `observe-only`.

Decision: reporting and baseline evidence only. Current dense multiply has a
real 4096 digit win, but the large route is still backend-faster at 8192 and
16384 digits. Future Toom/FFT/NTT candidates must beat this same-run audit, not
just a standalone probe row, before any production route changes.

## 2026-06-17: Focused Square Route Audit

MFastFermat's CUDA-only benchmark mode is a useful measurement clue: isolate the
dimension being judged instead of letting a mixed tournament answer a different
question. Number X-Ray applies that lesson to dense square. Existing rows showed
current 1000 digit production square sometimes beating MPIR, while the ordinary
scratch row remained one stable pair short. This pass adds
`square-route-audit` rows that time current production square against `mpz_mul`
on the same input with deep interleaved samples:

- `current-default-1000` isolates the 1000 digit pocket.
- `current-default-1000-16384` keeps the larger square gap visible in the same
  artifact without letting it hide the focused pocket result.

Decision: evidence only. Production square routing is unchanged; any claim that
the 1000 digit square route is MPIR-clean must come from the local benchmark
row's parity, hash, stable-pair, and worst-pair gates.

Local Release validation artifact
`native/build-codex-parse-large/native-test-runs/20260617-181326-c4b04caf`
split the conclusion:

- `current-default-1000`: exact parity/hash (`9/9`), `candGmpMax=0.931`,
  `maxWorstPairRatio=0.996`, `safeSizes=1/1`, `promotion-ready`.
- `current-default-1000-16384`: exact parity/hash (`36/36`), but
  `candGmpMax=1.549`, `maxWorstPairRatio=1.756`, `safeSizes=0/4`,
  `observe-only`.

This makes the 1000 digit square pocket measurable without pretending the same
route is clean for 4096+ digit square workloads.

## 2026-06-17: Focused 768/896 Formatter Route Audit

The 10^19 preinverse formatter family has repeatedly shown a tempting 768 and
896 digit pocket while broader 768..1001 policies exposed route ambiguity. This
pass adds focused `format-policy-route-audit` rows for exactly 768 and 896
digits, alongside the existing broad audit. The new rows force the candidate,
compare it against the current production formatter and `mpz_get_str` on the
same inputs, and keep `noAutoRoute=1`.

Decision: evidence only. A 768/896 formatter route still must survive local
benchmark output and repeat/product-shaped confirmation before any production
formatter branch changes.

Local Release validation artifact
`native/build-codex-parse-large/native-test-runs/20260617-173352-c4b04caf`
kept both focused audits exact but unrouted:

- `audit-preinv10e19-window768-896`: `candCurrentMax=0.386`,
  `candGmpMax=0.764`, `safeSizes=1/2`, rejected by worst-pair regression
  (`maxWorstPairRatio=1.400`).
- `audit-preinv10e19-pairs-window768-896`: `candCurrentMax=0.361`,
  `candGmpMax=0.767`, `safeSizes=1/2`, rejected by worst-pair regression
  (`maxWorstPairRatio=1.173`).

This proves the median pocket is real but also proves it is not harmless enough
to route yet.

## 2026-06-17: Promotion Blocker Reporting

MFastFermat's latest CUDA tournament reporting exported external baseline
templates and cross-bit safety labels so a single fast row does not masquerade
as a safe route. Number X-Ray now mirrors that evidence discipline in
`xray_benchmark_progress_tsv_text()` and
`xray_benchmark_progress_classification_tsv()`: fast-looking median wins that
remain blocked by neighbor checks, worst-pair regressions, product gates,
deep-confirmation requirements, or stability shortfalls are surfaced in a
dedicated digest section and a machine-readable `blockerReason` column.

Decision: reporting only. This does not change arithmetic routes, but it makes
the next formatter, division, or multiplication threshold review harder to
misread and easier for external tools to import.

## 2026-06-17: Decimal 1000-Digit Pair-Writer Route

The benchmark artifact
`native/build-codex-parse-large/native-test-runs/20260617-163404-c4b04caf`
identified `decimal-divide-1e19-preinv-pair-writer` as the best route across
768 to 1000 actual decimal digits. A later product run showed that the wider
window was not uniformly safe: 768 and 896 digit scratch rows were median wins
but had worst-pair regressions above 1.0. The production gate therefore routes
only estimated 1001-digit inputs, which captures the benchmarked 1000-digit
sample whose bit-length estimate rounds up by one. The original route audit
showed `safeSizes=4/4`, `hashSafe=36/36`, `maxWorstPairRatio=1.000`,
`candCurrentMax=0.467`, and `candGmpMax=0.859` for the 768, 896, 960, and 1000
digit proof points.

Decision: route only the 1000-digit bucket before the 4096+ D&C ladder. Do not
route the broader 768-896 window or extend the same family to larger formatter
sizes; product probes at 4096, 8192, and 16384 digits remain slower and stay
default-off.

## 2026-06-17: Large Decimal Formatter Route Rejection

The focused formatter run
`native/build-codex-parse-large/native-test-runs/20260617-160720-c4b04caf`
separated unsafe mid-size formatter windows from the larger D&C tier. The
route-filter digest found `format-dc-route direct16-vs-ladder8` beating the
ladder baseline at 1000 digits (`0.879x`, worst pair `0.910x`, stable `5/5`),
4096 digits (`0.839x`, worst pair `0.971x`, stable `5/5`), and 16384 digits
(`0.968x`, worst pair `0.985x`, stable `4/5`), but the same candidate was
not safe at 8192 digits (`0.981x`, worst pair `1.104x`, stable `2/5`).
The explicit `format-dc-route-safety` row rejected the policy with
`neighbor-regression`, `safeSizes=0/3`, `maxWorstPairRatio=1.338`, and
`noAutoRoute=1`. The same run kept `format-dc-preinv-qhat` and the tempting
768-1000 digit divide-`1e19` preinverse windows product-gated or rejected by
neighbor/worst-pair safety.

Decision: `xray_bigint_get_decimal()` keeps the existing large threshold of
`216` wide decimal chunks and remains on the ladder formatter at leaf `8`.
Direct-output, workspace, and preinv-qhat D&C stay as diagnostic comparators
until the explicit safety rows pass. This avoids promoting a root-size-gated or
single-pocket threshold that looks good in one row and loses in the product
shape.

## 2026-06-17: Importable Bigint Route Map

Local MPIR/GMP source review found that GMP's `mpn_get_str` separates decimal
serialization into basecase, power-table precompute, and divide-and-conquer
thresholds before it commits to a large conversion route. Number X-Ray now
records that clue in an importable route summary instead of copying any GMP
code or promoting an unproven formatter path.

The new `xray_bigint_route_config_json()` API returns the full scratch bigint
route map as JSON: ABI-stable struct fields, decimal conversion thresholds,
base-`1e19` constants, sparse square/multiply gates, production route names,
and default-off diagnostic probe families. Benchmark and workbench JSON now
reuse the same object under `scratchRouteConfig`, and the Python ctypes helper
exposes it as `bigint_route_summary()`.

Decision: reporting and import surface only. No production arithmetic,
formatting, parser, or solver route changes in this step. The value is that
external tools and future PRs can tie benchmark artifacts to the exact route
map and MPIR/GMP clue trail before testing another formatter threshold.

## 2026-06-17: Large Decimal Parse Chunk Route

The parse-chunk tournament in
`native-test-runs/20260617-140143-c4b04caf/benchmark.tsv` found that 15-digit
decimal ingestion is a stable winner only once inputs are large enough. It won
5/5 paired samples at 2048 digits (`0.665x` paired median, `0.744x` worst
pair), 4096 digits (`0.443x`, `0.482x` worst pair), 8192 digits (`0.378x`,
`0.442x` worst pair), and 16384 digits (`0.345x`, `0.350x` worst pair). The
same route was not promoted below 2048 digits because 1000-digit and smaller
rows were noisy or baseline-faster.

Decision: `xray_bigint_set_decimal()` now keeps the 19-digit chunk path below
2048 decimal digits and routes 2048+ digit inputs through 15-digit chunks.
The threshold counts decimal digits, not raw pasted string length, so spaces,
commas, and underscores do not accidentally promote a small value. The explicit
`xray_bigint_set_decimal_chunk_probe()` API remains available for future
tournaments.

Post-route verification in
`native/build-codex-parse-large/native-test-runs/20260617-143017-c4b04caf`
moved production parse from oracle-only to replacement-ready at the large
covered sizes: 4096 digits improved from `1.32x` slower than MPIR to `0.64x`
of MPIR time, and 8192 digits improved from `1.87x` slower to `0.73x` of MPIR
time. Scratch replacement-ready rows moved from 37 to 39.

## 2026-06-17: Formatter Tournament Detail Rows

MFastFermat `5fce429` made its CUDA tournament easier to audit by exporting
detail rows for every kernel contender instead of only the recommendation.
Number X-Ray now mirrors that measurement hygiene for the formatter route
tournament: each `format-route-tournament` winner row is followed by
`format-route-tournament-detail` control rows for every contender timed on the
same input and same run.

Decision: reporting only. The detail rows use
`controlSafety=tournament-detail`, `noAutoRoute=1`, exact hash gates, and
paired route/current plus route/GMP ratios. They make broad formatter swatches
importable without changing production decimal formatting or route-completed
progress counts.

## 2026-06-17: Importable Progress Shape Metadata

MFastFermat `75241b7`, `ea5c11f`, and `5e388e2` preserved root-class masks and
then reported root-class counts plus rejection splits beside CUDA tournament
rows. The transferable lesson is that optimizer evidence should carry its shape
labels next to the measured row, not in a prose-only note.

Number X-Ray now appends machine-readable metadata to
`benchmark_progress.tsv`: `digitBand`, `workloadShape`, `policy`, `candidate`,
`activeCandidate`, `baseline`, `featureGate`, `gmpClue`, `controlSafety`,
`thresholdSafety`, and `hashGate`. The progress boolean for current/default
baseline rows is exported as `baselineRow` so the detail-derived `baseline`
route label remains importable without duplicate TSV headers. This is reporting
only. It lets external tools group broad swatches by size band, route family,
and safety gate before ranking contenders, while leaving all arithmetic and
production routing unchanged.

## 2026-06-17: Focused Benchmark Digit Windows

The hourly MFastFermat watch advanced `main` to `8d9735e` with a focused
known-factor bit-band filter. The useful import for Number X-Ray is benchmark
review discipline, not a direct CUDA route: when an optimization is only
credible for a narrow size or shape, the review artifact should make that
window explicit.

Number X-Ray now exposes `xray_benchmark_filter_tsv_digits()` and matching CLI
flags `--bench-min-digits` / `--bench-max-digits` for `--bench-progress`,
`--bench-progress-tsv`, and `--bench-compare`. The filter preserves the full
raw benchmark artifact and slices only the review view, so a 768..1000 digit
formatter pocket or a 4096..8192 digit D&C formatter scout can be inspected
without pretending it is a global route. This does not promote any arithmetic
or formatting path; it makes the evidence harder to overstate.

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

MFastFermat `6e734a4` added another measurement guardrail for long prefix
scouts: timed-out helper rows are recorded as `timeout lower-bound`, the TSV
separates `Runs` from `CompletedRuns`, and incomplete rows are excluded from
medians and gates. Number X-Ray now mirrors that rule in `--bench-progress`.
Rows with status/adoption/detail text such as `timeout`, `lower-bound`,
`no-complete-run`, `incomplete`, or `CompletedRuns=0` go into the
`lowerBoundRows` lane and do not count as route candidates, route-completed, or
route-open evidence. This keeps partial large-number probes reviewable without
letting a timeout masquerade as either failure-to-improve or promotion progress.

MFastFermat's follow-up warmup-review branch adds the same separation for
setup-heavy rows: ordinary `WarmupPolicy=not-counted` setup remains planning
context, but `WarmupPolicy=review-warmup` is reported in its own lane and cannot
count as a route candidate or ordinary open/noisy evidence. Number X-Ray mirrors
that in `--bench-progress` for rows carrying `WarmupPolicy=review-warmup`,
`warmupPolicy=review-warmup`, or `setupPolicy=review-warmup`. This matters for
large-number precompute work because a fast timed kernel is not actionable if
its required setup is close to timeout, budget, or practical usability limits.

MFastFermat's current `codex/prefix-warmup-seconds` feedback goes one step
finer: prefix scouts print measured `warmup_s` beside the scored
`median_prefix-round/s`, and product-prefix TSVs carry
`WarmupSecondsMedian`. Number X-Ray now mirrors that visibility in
`--bench-progress` with a non-exclusive `setupContextRows` lane for
`setupPolicy=reported-not-scored`, `setupUs`, `warmup_s`,
`WarmupSecondsMedian`, and related setup tags. These rows are reported for
review but not scored as throughput; review-warmup and lower-bound rows still
take precedence and remain excluded from route candidate totals.

MFastFermat `0070fae` then made that lane importable by adding
`HasSetupContext` to its progress TSV output. Number X-Ray mirrors the same
handoff shape with `benchmark_progress.tsv` and
`xray_benchmark_progress_classification_tsv()`. External tools can now read
`routeCandidate`, `routeCompleted`, `productGated`, `hasSetupContext`,
`setupSeconds`, `warmupReview`, `lowerBound`, `runFailed`, `baselineRow`, `control`, and
`noisyControl` directly instead of scraping the human digest. The numeric setup
column follows the latest MFast `SetupSeconds` lesson: explicit
`SetupSeconds`/`setupSeconds` tags win, otherwise measured setup and warmup tags
such as `setupUs`, `setupMs`, `warmup_s`, `WarmupSecondsMedian`, and
`HelperWarmupSeconds` are converted to seconds. The value is context for review,
not route score.

MFastFermat's in-progress `codex/product-prefix-timeout-rows` feedback extends
that same discipline to product-prefix failures: timed-out product rows carry
`Runs`, `CompletedRuns`, and `Status`, and plain failed rows are separated from
timeout lower bounds. Number X-Ray mirrors the import side with a `runFailed`
classification column, `attemptedRuns` / `completedRuns` columns, and a
`Run-failed rows observed` digest lane. A failed row remains visible even if no
speed ratio was recorded, but it does not count as completed, open,
product-gated, or lower-bound progress. If `Status=run failed` and
`CompletedRuns=0` both appear, the run-failed lane wins so process failures do
not masquerade as timeout lower bounds.

## 2026-06-17: Static Formatter Leaf Expansion

MFastFermat follow-up evidence around `d942c12`, `34ba68b`, and `f479009`
keeps a strong transform signal open because duplicate controls still fail.
The local analogue is to widen benchmark evidence before moving any route:
the D&C decimal formatter now tests static power-table leaves `32` and `64`
alongside the existing `8` and `16` rows, for both recursive string assembly
and direct-buffer output. These rows are exact-parity, default-off probes for
the 8k/16k formatting gap and must not be treated as production policy until
same-run stability and product-like route checks pass.

## 2026-06-17: MFastFermat 8M DIF/DIT Promotion Signal

MFastFermat `f3f1ab9` moved `8,192,036` from strong-open to default-off
route-completed evidence. Its current 8M winner is the 24k wide61 DIF/DIT
handoff: `1.612x` median Product/GMP, `2/2` GMP wins, `1.205x` worst GMP
pair, `8.359x` over release, `6.416x` control-adjusted, and low duplicate-base
noise with `1.303x` median and `1.078x` worst. Number X-Ray's frontier-scout
rows now cite that current signal as
`gmpClue=mfast8m-difdit24-complete`, `mfastKnob=difdit24k`,
`mfastPG=1.612`, and `mfastCW=1.078`. The emitted tags are intentionally
compact so the benchmark detail still retains `noAutoRoute=1` and duplicate
control metadata. This still does not promote a Number X-Ray arithmetic route;
it keeps the sparse-transform clue accurate for future large-number
square/multiply work.

## 2026-06-17: MFastFermat DIF/DIT 98,304-Limb Pocket

The latest MFastFermat watch tested a stricter wide61 DIF/DIT handoff,
`ntt16_wide61_difdit_98304`, with the handoff gated to 98,304+ limbs. The
product-matrix run
`product_matrix_difdit98304_u6144_u8192_r1_20260617.tsv` is useful as a
threshold-pocket warning, not a promotion signal:

- At `6,144,036` bits, `difdit98304` was only `1.054x` versus release and
  `0.209x` versus GMP, or `1.019x` after duplicate-base control adjustment.
- At `8,192,036` bits, it jumped to `6.194x` versus release and `1.320x`
  versus GMP, with `6.726x` control-adjusted.
- The same run reported high duplicate-base control noise on the 8M case
  (`base_control=0.921x`), so the range gate stayed `noisy-control`.

Number X-Ray's `frontier-scout` rows now carry this clue as compact metadata:
`mfastPocket=difdit98304`, `mfastPocketFloor=98304`,
`mfastPocket6144=1.019`, `mfastPocket8192PG=1.320`,
`mfastPocket8192VB=6.194`, and `mfastPocketGate=noisy-control`. The rows still
emit `noAutoRoute=1` and `adoption=observe-only`. The actionable lesson is to
keep testing root-size-gated threshold pockets with adjacent-size and duplicate
control evidence before touching production arithmetic.

## 2026-06-17: MFastFermat 16M Preflight Discipline

MFastFermat `b025856` added repeated dense-residue preflight medians for the
16M frontier so a huge steady-warmup transform route can be ranked before a
full product/GMP run. The useful local lesson is measurement shape, not the
transform itself: do cheap, repeated, exact-parity confirmation first, and keep
warmup/planning evidence out of route-completed totals.

Number X-Ray now adds a `format-dc-route-safety` gate for the tempting
direct-output D&C formatter route. It compares `direct16` against the current
`ladder8` route at 4096, 8192, and 16384 digits with 9 paired samples, requires
8 stable wins per size, records the worst same-run pair, hash-verifies the
candidate/baseline/GMP output strings, and emits `hashSafe`, `hashGate`,
`noAutoRoute=1`, `thresholdSafety=forced-neighbor`, and
`deepConfirmation=required`. This follows the current MFastFermat hash-gated
preflight discipline (`615fe9e`/`4bfa54b`): warmup and preflight timings can
rank ideas, but output hashes must agree before any row is allowed into a
route-safety decision. Even a clean row remains `observe-only` until a
repeat/product-shaped build proves that changing production formatting is not
harmful.

First local result (`20260617-010626-c4b04caf`): the shallow route rows were
tempting at 1000 and 4096 digits (`0.899x` and `0.913x`, both `5/5` stable),
but 8192 and 16384 digits had worst-pair misses (`1.036` and `1.004`). The new
9-sample safety gate reported median `0.924x`, worst pair `1.008`, safe sizes
`2/3`, and status `neighbor-regression`, so the production formatter stays on
the current ladder route.

Follow-up MFastFermat feedback added warmup-cache accounting for 16M swatches:
the default helper pays the warmup once, variants reuse that residue, and the
TSV records helper setup separately from scored work. Number X-Ray mirrors that
measurement discipline in the division precompute rows. `divmod-precomputed`,
`divmod-workspace`, and `divmod-preinv-qhat` now report `setupUs`,
`setupSamples`, `setupIterations`, `setupPolicy=reported-not-scored`, and
`cacheRole=divisor-context`. The timed ratio still measures only candidate
division work, while setup cost remains visible in JSON/TSV for reviewers.

MFastFermat `20e96f5` and `5cd9c65` refine the DIF/DIT threshold picture. The
1M product-prefix matrix (`product_matrix_difdit_thresholds_u1024k_r3_pr2_min75_20260617.tsv`)
showed all tested thresholds ahead of GMP by median Product/GMP
(`difdit8192=1.326x`, `difdit12000=1.442x`, `difdit16000=1.480x`), but the
duplicate-base control was noisy (`0.924x` median, `0.911x` worst), so the
range gate remained `noisy-control`. The 16M cached preflight run
(`frontier_phase_preflight_cache_16m_difdit98304_r3_20260617.tsv`) hash-matched
all rows and kept warmup as `not-counted` setup context. Its evaluation
ranking was default `2.876s`, twiddle24 `0.442s`, twiddle32 `0.432s`,
difdit24 `0.335s`, difdit32 `0.381s`, and difdit98304 `0.404s`. Number X-Ray
therefore keeps emitting `noAutoRoute=1` and now records compact frontier tags
`mfast1mGate=noisy`, `mfast1mBest=difdit16000PG1.480`,
`mfast16mBest=difdit24k`, `mfast16mD24=0.335`, and
`mfast16mD98304=0.404`. The local lesson is precise: 98,304 remains useful for
8M pocket isolation, but the 16M target lane should confirm the 24k DIF/DIT
handoff first unless product/GMP evidence reverses it.

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

## 2026-06-18: Rejected Threshold48 Multiply Route Audit

Run:

- Release:
  `native/build-codex-exact-estimate/native-test-runs/20260618-014753-c4b04caf`

The threshold tournament had a tempting but contradictory large-multiply signal,
so this run added a forced, same-run route audit for `threshold48` across 8192
and 16384 decimal digits. The audit compares the candidate route against the
current production route and `mpz_mul` on the same generated inputs, using
interleaved rotating batches, two operand families, parity/hash checks, and a
forced-neighbor policy gate.

Key rows:

- `mul 8192`: ratio `0.941`, worst `1.413`, stable `3/5`, `oracle-only`
- `mul 16384`: ratio `1.611`, worst `1.853`, stable `0/5`, `oracle-only`
- `mul-route-audit current-default-16384`: ratio `1.388`, worst `1.510`,
  hash/parity `18/18`, safe sizes `0/1`, `observe-only`
- `mul-route-audit current-default-4096-16384`: ratio `1.405`, worst `1.681`,
  hash/parity `54/54`, safe sizes `0/3`, `observe-only`
- `mul-threshold-route-audit threshold48-8192-16384`: status
  `current-regression`, candidate/current max `1.299`, candidate/GMP max
  `1.568`, current/GMP max `1.511`, worst pair `1.662`, hash/parity `36/36`,
  safe sizes `0/2`, `observe-only`
- `mul-threshold-tournament 8192`: best threshold equals the current threshold
  `64`, ratio `0.899`, worst `1.227`, stable `3/5`,
  `duplicateControl=best-is-current`, `controlSafety=noisy-control`
- `mul-threshold-tournament 16384`: best threshold `32`, ratio `0.910`,
  worst `1.080`, stable `3/5`, `duplicateControl=candidate-vs-current`,
  `controlSafety=not-control`

Decision: reject `threshold48` as a production route. It is exact, but it does
not beat the current route under the same-run forced-neighbor audit. The
tournament result is now explicitly labeled for duplicate-control safety so a
noisy current-threshold control cannot be mistaken for a route win. The next
large multiply work should look at deeper multiplication primitives or a cleaner
threshold measurement design, not a global threshold48 handoff.

## 2026-06-18: Dense Leaf Sparse-Scan Audit

Run:

- Release:
  `native/build-codex-exact-estimate/native-test-runs/20260618-023638-c4b04caf`

MFastFermat feedback kept pointing at shape-decision caching: do not repeatedly
rediscover sparse/dense shape inside hot loops unless the measurement proves it
helps. Number X-Ray's generic multiply leaves scan both operands for non-zero
limbs before deciding whether to use sparse schoolbook work, even for dense
random benchmark operands. This run adds a default-off diagnostic probe,
`mul-dense-leaf-vs-scan`, that keeps the same Karatsuba recursion and unroll4
leaf schedule but disables the sparse scan inside schoolbook leaves. It compares
against the matching scanned leaf route, not against GMP, and is labeled
`noAutoRoute=1`.

Key rows:

- `mul-dense-leaf-vs-scan 4096`: ratio `0.919`, worst `1.271`,
  stable `3/5`, `observe-only`
- `mul-dense-leaf-vs-scan 8192`: ratio `0.990`, worst `1.136`,
  stable `2/5`, `observe-only`
- `mul-dense-leaf-vs-scan 16384`: ratio `1.110`, worst `1.123`,
  stable `1/5`, `observe-only`
- same-run scratch `mul 8192`: ratio `1.051`, worst `1.296`,
  stable `2/5`, `oracle-only`
- same-run scratch `mul 16384`: ratio `1.360`, worst `1.544`,
  stable `0/5`, `oracle-only`

Decision: do not promote dense-leaf no-scan routing. Skipping sparse scans is
correct and shows a 4096-digit pocket in this run, but the pocket is noisy,
8192 digits is effectively flat, and 16384 digits regresses. The result narrows
the next search: repeated sparse scans are not the current dense multiply
bottleneck; future work should focus on Karatsuba allocation/copy overhead,
no-copy split views, or a larger algorithm handoff with same-run route audits
before changing production behavior.

## 2026-06-18: Karatsuba Split-View Copy-Tax Audit

Run:

- Release:
  `native/build-codex-exact-estimate/native-test-runs/20260618-030505-c4b04caf`

This run adds a default-off diagnostic probe, `mul-karatsuba-view-vs-copy`, to
measure whether Karatsuba's recursive low/high halves should be read-only views
instead of owned scratch copies. The comparison is deliberately narrow: same
input fingerprints, same leaf threshold, same sparse-scan behavior, same MSVC
x64 unroll4 leaf schedule, and paired medians against the current copied-slice
route. The probe is exported only for benchmarking and labeled `noAutoRoute=1`.

Key rows:

- `mul-karatsuba-view-vs-copy 4096`: ratio `0.940`, worst `1.131`,
  stable `4/5`, `observe-only`
- `mul-karatsuba-view-vs-copy 8192`: ratio `0.996`, worst `1.088`,
  stable `2/5`, `observe-only`
- `mul-karatsuba-view-vs-copy 16384`: ratio `0.956`, worst `1.000`,
  stable `3/5`, `observe-only`
- same-run scratch `mul 4096`: ratio `1.005`, worst `1.343`,
  stable `2/5`, `oracle-only`
- same-run scratch `mul 8192`: ratio `0.976`, worst `1.132`,
  stable `3/5`, `oracle-only`
- same-run scratch `mul 16384`: ratio `1.327`, worst `1.473`,
  stable `0/5`, `oracle-only`

Decision: keep split views as an audited probe, not a production route yet.
The route is exact and median-positive at 4096 and 16384 digits, but the 8192
row is flat and the worst-pair/stability evidence still argues for caution.
This does prove that slice-copy overhead is a real candidate at some sizes; the
next improvement should combine split views with a workspace/no-realloc plan or
run a deeper route audit before changing the default multiply policy.

## 2026-06-18: Large Multiply CPU Campaign Workspace And Toom View Probe

Run:

- Release:
  `native-test-runs/20260618-233248-c4b04caf`

This run adds a default-off diagnostic probe,
`xray_bigint_mul_karatsuba_workspace_probe()`, and a same-run
`mul-large-cpu-campaign` benchmark family. The probe keeps production multiply
unchanged, uses read-only split views for Karatsuba halves, and reuses
pre-sized recursive temporaries per workspace depth. Each campaign row compares
current production multiply, split-view Karatsuba, split-view plus workspace
reuse, and `mpz_mul` on identical operand fingerprints. The benchmark window now
includes power-of-two anchors plus deterministic random-looking in-between
spots: `4096`, `5639`, `8192`, `11717`, `16384`, `24103`, `32768`, `52163`,
and `65536` decimal digits. On MSVC x64, the same campaign also emits
`mul-large-cpu-toom-branch`, which times the existing recursive Toom-3 plus
unroll4 leaf diagnostic branch (`leafThreshold=64`, `depthLimit=2`) against
current production multiply, the workspace candidate, and `mpz_mul`, plus
`mul-large-cpu-toom-view-branch`, which times read-only Toom operand-third split
views against the copied Toom branch in the same run.

The run was built from fresh folder `native/build-codex-large-mul-campaign` with
Visual Studio 16 2019 x64 Release flags `/O2 /Ob2 /DNDEBUG`, IPO `/GL=false`,
MSVC `1929 full=192930159`, and CPU flags `AVX2 BMI1 BMI2 ADX`. The full
native test binary printed `native xray tests passed` (`916/916`). The generated
TSV, JSON, frontier text, progress text, progress TSV, and native GUI-visible
benchmark summary all include the new campaign rows.

Key rows:

- `mul-large-cpu-campaign 4096`: ratio `0.810`, worst `1.092`,
  stable `4/5`, `observe-only`
- `mul-large-cpu-campaign 5639`: ratio `0.849`, worst `1.370`,
  stable `4/5`, `observe-only`
- `mul-large-cpu-campaign 8192`: ratio `0.940`, worst `1.104`,
  stable `4/5`, `observe-only`
- `mul-large-cpu-campaign 11717`: ratio `0.834`, worst `1.059`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-campaign 16384`: ratio `0.837`, worst `1.455`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-campaign 24103`: ratio `0.767`, worst `1.349`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-campaign 32768`: ratio `0.842`, worst `1.684`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-campaign 52163`: ratio `0.801`, worst `1.651`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-campaign 65536`: ratio `0.828`, worst `2.464`,
  stable `0/5`, `observe-only`

Toom branch rows:

- `mul-large-cpu-toom-branch 4096`: ratio `1.067`, worst `2.606`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-branch 5639`: ratio `1.165`, worst `1.891`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-toom-branch 8192`: ratio `0.861`, worst `1.165`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-toom-branch 11717`: ratio `0.969`, worst `1.415`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-branch 16384`: ratio `0.834`, worst `1.268`,
  stable `2/5`, `observe-only`
- `mul-large-cpu-toom-branch 24103`: ratio `0.833`, worst `1.584`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-branch 32768`: ratio `0.815`, worst `1.481`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-toom-branch 52163`: ratio `0.740`, worst `1.545`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-branch 65536`: ratio `0.663`, worst `2.157`,
  stable `0/5`, `observe-only`

Toom split-view branch rows:

- `mul-large-cpu-toom-view-branch 4096`: ratio `0.978`, worst `1.303`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-view-branch 5639`: ratio `0.805`, worst `1.451`,
  stable `2/5`, `observe-only`
- `mul-large-cpu-toom-view-branch 8192`: ratio `1.193`, worst `1.513`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-view-branch 11717`: ratio `0.999`, worst `1.498`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-view-branch 16384`: ratio `0.945`, worst `1.356`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-toom-view-branch 24103`: ratio `0.956`, worst `1.328`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-toom-view-branch 32768`: ratio `0.990`, worst `1.610`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-toom-view-branch 52163`: ratio `0.990`, worst `1.800`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-view-branch 65536`: ratio `0.977`, worst `2.108`,
  stable `0/5`, `observe-only`

Decision: keep the workspace split-view multiply, copied recursive Toom branch,
and split-view recursive Toom branch as diagnostic probes. They are exact and
median-positive in pockets, including several in-between sizes, but all fail the
promotion bar because worst-pair ratios exceed safety and larger rows have weak
or zero stable-pair counts. The next multiply PR should rerun this campaign on
the faster CPU and only proceed to a forced route audit if the same-run route
gates pass there. Otherwise, the next implementation step should reduce
Toom/Karatsuba temporary allocation or broaden the recursive workspace strategy
before changing production multiply routing.

## 2026-06-19: Recursive Toom Workspace Probe

Run:

- Release:
  `native-test-runs/20260619-001030-c4b04caf`

This run adds `xray_bigint_mul_toom3_unroll4_recursive_workspace_probe()`, a
default-off diagnostic route for MSVC x64 builds. It keeps production multiply
unchanged, uses read-only Toom operand-third views, and reuses per-depth Toom
evaluation, interpolation, exact-division, and product temporaries. The same
large multiply CPU campaign now emits `mul-large-cpu-toom-ws-branch` rows, which
compare the Toom workspace probe against current production multiply,
Karatsuba workspace, copied recursive Toom, split-view recursive Toom, and
`mpz_mul` on identical operands.

The run used `native/build-codex-large-mul-campaign` with Visual Studio 16 2019
x64 Release flags `/O2 /Ob2 /DNDEBUG`, IPO `/GL=false`, MSVC
`1929 full=192930159`, and CPU flags `AVX2 BMI1 BMI2 ADX`. The full native test
binary printed `native xray tests passed` (`925/925`). TSV, JSON, frontier text,
progress text, progress TSV, and the native GUI-visible benchmark-row event
stream include the new row.

Toom workspace rows:

- `mul-large-cpu-toom-ws-branch 4096`: ratio `0.855`, worst `1.120`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-toom-ws-branch 5639`: ratio `0.957`, worst `1.127`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-ws-branch 8192`: ratio `0.924`, worst `1.171`,
  stable `2/5`, `observe-only`
- `mul-large-cpu-toom-ws-branch 11717`: ratio `0.838`, worst `1.134`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-ws-branch 16384`: ratio `0.940`, worst `1.265`,
  stable `2/5`, `observe-only`
- `mul-large-cpu-toom-ws-branch 24103`: ratio `1.015`, worst `1.644`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-ws-branch 32768`: ratio `1.047`, worst `1.380`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-ws-branch 52163`: ratio `0.914`, worst `1.371`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-ws-branch 65536`: ratio `0.901`, worst `2.585`,
  stable `1/5`, `observe-only`

Decision: keep the Toom workspace route diagnostic-only. It is exact and wins
the median against split-view Toom at seven of nine campaign sizes, including
the deterministic random spots `5639`, `11717`, and `52163`, but it still misses
the strict promotion bar because stable-pair counts remain weak and every row
has a worst-pair ratio above `1.0`. The next multiply step should either
increase same-run samples on the faster CPU for this row or reduce the remaining
basecase/Karatsuba copy tax before any production route audit.

## 2026-06-19: Recursive Toom Full Workspace Probe

Run:

- Release:
  `native-test-runs/20260619-004040-c4b04caf`

This run adds
`xray_bigint_mul_toom3_unroll4_recursive_full_workspace_probe()`, a default-off
diagnostic route for MSVC x64 builds. It keeps production multiply unchanged,
reuses per-depth Toom temporaries like the prior workspace probe, and also
reuses Karatsuba temporaries on the recursive handoff path. The same large
multiply CPU campaign now emits `mul-large-cpu-toom-full-ws` rows, which compare
the broader workspace strategy against current production multiply, Karatsuba
workspace, copied recursive Toom, split-view recursive Toom, Toom workspace, and
`mpz_mul` on identical operands.

The run used `native/build-codex-large-mul-campaign` with Visual Studio 16 2019
x64 Release flags `/O2 /Ob2 /DNDEBUG`, IPO `/GL=false`, MSVC
`1929 full=192930159`, and CPU flags `AVX2 BMI1 BMI2 ADX`. The full native test
binary printed `native xray tests passed`. TSV, JSON, frontier text, progress
text, progress TSV, and the native GUI-visible benchmark-row event stream include
the new row.

Full workspace rows:

- `mul-large-cpu-toom-full-ws 4096`: ratio `0.913`, worst `1.495`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-toom-full-ws 5639`: ratio `0.911`, worst `1.500`,
  stable `2/5`, `observe-only`
- `mul-large-cpu-toom-full-ws 8192`: ratio `0.954`, worst `1.379`,
  stable `3/5`, `observe-only`
- `mul-large-cpu-toom-full-ws 11717`: ratio `0.975`, worst `1.240`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-toom-full-ws 16384`: ratio `0.947`, worst `1.169`,
  stable `2/5`, `observe-only`
- `mul-large-cpu-toom-full-ws 24103`: ratio `0.945`, worst `1.459`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-full-ws 32768`: ratio `0.960`, worst `1.496`,
  stable `1/5`, `observe-only`
- `mul-large-cpu-toom-full-ws 52163`: ratio `0.937`, worst `1.369`,
  stable `0/5`, `observe-only`
- `mul-large-cpu-toom-full-ws 65536`: ratio `1.052`, worst `1.769`,
  stable `0/5`, `observe-only`

Decision: keep the full recursive workspace route diagnostic-only. It is exact
and median-positive against the prior Toom workspace baseline at eight of nine
campaign sizes, including every deterministic random spot, but it still misses
the strict promotion bar because stable-pair counts are weak and every row has a
worst-pair ratio above `1.0`. The next multiply step should use the faster CPU
campaign to decide whether this broader workspace strategy deserves a forced
route audit; it is not a production default.

## 2026-06-19: Full Workspace Route Audit

Run:

- Release:
  `native-test-runs/20260619-080250-c4b04caf`

This run adds `mul-large-cpu-toom-full-audit`, an observe-only row that compares
the full recursive workspace probe directly against current production multiply
using the same samples and operands as the large multiply CPU campaign. The row
does not change production routing and records `noAutoRoute=1` plus
`replacementReady=false` until a later promotion PR has exact parity, same-run
route audit proof, worst-pair safety, and stable-pair gates.

Audit rows versus current production multiply:

- `mul-large-cpu-toom-full-audit 4096`: ratio `1.003`, worst `1.100`,
  stable `2/5`, `observe-only`
- `mul-large-cpu-toom-full-audit 5639`: ratio `0.954`, worst `1.065`,
  stable `3/5`, `observe-only`
- `mul-large-cpu-toom-full-audit 8192`: ratio `0.947`, worst `1.020`,
  stable `4/5`, `observe-only`
- `mul-large-cpu-toom-full-audit 11717`: ratio `0.791`, worst `0.851`,
  stable `5/5`, `observe-only`
- `mul-large-cpu-toom-full-audit 16384`: ratio `0.815`, worst `0.860`,
  stable `5/5`, `observe-only`
- `mul-large-cpu-toom-full-audit 24103`: ratio `0.831`, worst `0.868`,
  stable `5/5`, `observe-only`
- `mul-large-cpu-toom-full-audit 32768`: ratio `0.902`, worst `0.919`,
  stable `5/5`, `observe-only`
- `mul-large-cpu-toom-full-audit 52163`: ratio `0.750`, worst `0.770`,
  stable `5/5`, `observe-only`
- `mul-large-cpu-toom-full-audit 65536`: ratio `0.769`, worst `0.870`,
  stable `5/5`, `observe-only`

Decision: no promotion. The route audit is strong from `11717` through `65536`
digits, but the required `4096` through `8192` start of the policy window still
fails: `4096` is a median loss, while `5639` and `8192` remain above the
worst-pair safety bar. The next route work should either narrow a thresholded
handoff above `8192` or improve the small end of the Toom/full-workspace path
before any default multiply change.

## 2026-06-19: Full Workspace Active-Window Deep Audit

Run:

- Release:
  `native-test-runs/20260619-092659-c4b04caf`

This run adds `mul-large-toom-full-deep-audit`, an aggregate policy-gate row
for the active high-size handoff window: `11717`, `16384`, `24103`, `32768`,
`52163`, and `65536` decimal digits. It forces the full recursive Toom-3 plus
unroll4 workspace candidate, times current production multiply and `mpz_mul` on
the same operands in rotating batches, and uses 9 paired samples per size. The
row remains `noAutoRoute=1`, `replacementReady=false`, and `observe-only`.

Observed aggregate:

- Exact parity/hash against current and GMP: `hashSafe=108/108`,
  `hashGate=matched`, `parity=matched`.
- Current-route median gate stayed favorable: `candCurrentMax=0.792`.
- GMP-facing and safety gates failed: `candGmpMax=1.321`,
  `maxWorstPairRatio=1.389`, `safeSizes=0/6`, `stableSampleCount=0/6`.
- Progress artifacts classify the row as `backend-regression` /
  `safety-rejected`, even though the median versus current is faster.

Per-size point rows from the same measurement make the rejection visible in the
ordinary artifact tables:

| Digits | Current Ratio | GMP Ratio | Worst Pair | Current Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `0.764` | `1.004` | `1.048` | `9/9` | `4/9` | backend-regression |
| `16384` | `0.762` | `1.021` | `1.121` | `9/9` | `2/9` | backend-regression |
| `24103` | `0.792` | `1.225` | `1.334` | `9/9` | `0/9` | backend-regression |
| `32768` | `0.743` | `1.157` | `1.293` | `9/9` | `0/9` | backend-regression |
| `52163` | `0.693` | `1.216` | `1.267` | `9/9` | `0/9` | backend-regression |
| `65536` | `0.689` | `1.321` | `1.389` | `9/9` | `0/9` | backend-regression |

Decision: no thresholded handoff promotion from the current high-size evidence.
The 5-sample route audit showed a promising current-route pocket above `8192`,
but the 9-sample active-window audit rejects it once GMP timing, worst-pair
safety, and stable-pair gates are required together. Keep the full-workspace
route diagnostic-only and continue with deeper multiplication structure work or
a broader recursive workspace strategy before another promotion attempt.

## 2026-06-19: Full Workspace Depth-3 Scout

Run:

- Release:
  `native-test-runs/20260619-100600-c4b04caf`

This run adds `mul-large-toom-depth-scout`, an observe-only active-window scout
that compares depth-3 full recursive Toom workspace against the existing depth-2
full-workspace probe, current production multiply, and `mpz_mul` on the same
operands in rotating batches. It uses the same active `11717`, `16384`, `24103`,
`32768`, `52163`, and `65536` digit window, keeps `noAutoRoute=1`, and emits
matching `mul-large-toom-depth-point` rows for TSV, JSON, frontier/progress, and
GUI benchmark-table visibility.

Observed aggregate:

- Exact parity/hash against depth 2, current, and GMP: `hashSafe=108/108`,
  `hashGate=matched`, `parity=matched`.
- Depth-3 still beats current production multiply on median:
  `candCurrentMax=0.786`.
- Depth-3 does not beat the depth-2 full-workspace baseline:
  `candBaseMax=1.045`, `safeSizes=0/6`, `stableSampleCount=0/6`.
- GMP and worst-pair gates still fail: `candGmpMax=1.342`,
  `maxWorstPairRatio=1.443`.

Per-size point rows:

| Digits | Depth-3 / Depth-2 | Depth-3 / Current | Depth-3 / GMP | Worst Pair | Depth Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `1.010` | `0.784` | `1.034` | `1.105` | `1/9` | `1/9` | depth2-regression |
| `16384` | `1.045` | `0.786` | `1.046` | `1.161` | `2/9` | `2/9` | depth2-regression |
| `24103` | `0.977` | `0.689` | `1.068` | `1.105` | `5/9` | `0/9` | depth2-regression |
| `32768` | `0.996` | `0.711` | `1.191` | `1.234` | `1/9` | `0/9` | depth2-regression |
| `52163` | `1.025` | `0.669` | `1.262` | `1.358` | `1/9` | `0/9` | depth2-regression |
| `65536` | `0.994` | `0.673` | `1.342` | `1.443` | `1/9` | `0/9` | depth2-regression |

Decision: do not pursue depth 3 as the next promotion candidate. The scout is
exact and reinforces that the full-workspace family is faster than current
production multiply in the active window, but extra Toom depth is not a stable
improvement over the depth-2 workspace baseline and remains worse than GMP.
The next multiply PR should tune leaf/handoff shape or improve the lower-level
Toom arithmetic rather than simply increasing recursion depth.

## 2026-06-19: Full Workspace Leaf96 Scout

Run:

- Release:
  `native-test-runs/20260619-103207-c4b04caf`

This run adds `mul-large-toom-leaf-scout`, an observe-only active-window scout
that compares leaf96 depth-2 full recursive Toom workspace against the leaf64
depth-2 full-workspace baseline, current production multiply, and `mpz_mul`.
The measured window is `11717`, `16384`, `24103`, `32768`, `52163`, and
`65536`, preserving the deterministic in-between spots `11717`, `24103`, and
`52163` between power-of-two anchors. Matching `mul-large-toom-leaf-point` rows
keep every size visible in TSV, JSON, frontier/progress text, and the GUI
benchmark table.

Observed aggregate:

- Exact parity/hash against leaf64, current, and GMP: `hashSafe=108/108`,
  `hashGate=matched`, `parity=matched`.
- Leaf96 still beats current production multiply on median:
  `candCurrentMax=0.827`.
- Leaf96 does not beat the leaf64 full-workspace baseline:
  `candBaseMax=1.137`, `safeSizes=0/6`, `stableSampleCount=0/6`.
- GMP and worst-pair gates still fail: `candGmpMax=1.515`,
  `maxWorstPairRatio=1.578`.

Per-size point rows:

| Digits | Leaf96 / Leaf64 | Leaf96 / Current | Leaf96 / GMP | Worst Pair | Leaf Stable | Current Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `1.003` | `0.779` | `1.012` | `1.141` | `3/9` | `9/9` | `2/9` | leaf64-regression |
| `16384` | `1.074` | `0.824` | `1.106` | `1.149` | `0/9` | `9/9` | `1/9` | leaf64-regression |
| `24103` | `1.051` | `0.762` | `1.147` | `1.305` | `1/9` | `9/9` | `0/9` | leaf64-regression |
| `32768` | `1.120` | `0.827` | `1.314` | `1.344` | `0/9` | `9/9` | `0/9` | leaf64-regression |
| `52163` | `1.113` | `0.749` | `1.301` | `1.379` | `0/9` | `9/9` | `0/9` | leaf64-regression |
| `65536` | `1.137` | `0.779` | `1.515` | `1.578` | `0/9` | `9/9` | `0/9` | leaf64-regression |

Decision: keep leaf96 as a diagnostic rejection, not a promotion candidate. It
is exact and current-route-faster across the active window, including the
deterministic random spots, but leaf64 remains the stronger full-workspace
handoff and GMP remains ahead. The next multiply PR should inspect lower-level
Toom arithmetic or a different handoff shape rather than raising the recursive
leaf threshold to 96.

## 2026-06-19: Full Workspace Leaf48 Scout

Run:

- Release:
  `native-test-runs/20260619-105456-c4b04caf`

This run adds `mul-large-toom-leaf48-scout`, an observe-only active-window scout
that compares leaf48 depth-2 full recursive Toom workspace against the leaf64
depth-2 full-workspace baseline, current production multiply, and `mpz_mul`.
The measured window again includes the deterministic in-between spots `11717`,
`24103`, and `52163` between power-of-two anchors.

Observed aggregate:

- Exact parity/hash against leaf64, current, and GMP: `hashSafe=108/108`,
  `hashGate=matched`, `parity=matched`.
- Leaf48 beats current production multiply on median:
  `candCurrentMax=0.804`.
- Leaf48 does not beat leaf64 across the full active window:
  `candBaseMax=1.030`, `safeSizes=0/6`, `stableSampleCount=0/6`.
- GMP and worst-pair gates still fail: `candGmpMax=1.340`,
  `maxWorstPairRatio=1.457`.

Per-size point rows:

| Digits | Leaf48 / Leaf64 | Leaf48 / Current | Leaf48 / GMP | Worst Pair | Leaf Stable | Current Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `1.030` | `0.804` | `1.039` | `1.065` | `1/9` | `9/9` | `0/9` | leaf64-regression |
| `16384` | `0.964` | `0.761` | `0.995` | `1.054` | `7/9` | `9/9` | `5/9` | leaf64-regression |
| `24103` | `1.025` | `0.687` | `1.079` | `1.178` | `0/9` | `9/9` | `2/9` | leaf64-regression |
| `32768` | `0.985` | `0.667` | `1.105` | `1.186` | `4/9` | `9/9` | `0/9` | leaf64-regression |
| `52163` | `1.004` | `0.645` | `1.107` | `1.266` | `3/9` | `9/9` | `0/9` | leaf64-regression |
| `65536` | `0.977` | `0.670` | `1.340` | `1.457` | `6/9` | `9/9` | `0/9` | leaf64-regression |

Decision: keep leaf48 observe-only. It is much closer than leaf96 and has
anchor-size pockets at `16384`, `32768`, and `65536`, but the deterministic
random spots fail the leaf64 baseline gate, the stable-pair gate never reaches
`8/9`, and GMP remains ahead. The next multiply slice should move below the
handoff threshold knobs into Toom interpolation/evaluation cost or another
structural arithmetic improvement.

## 2026-06-19: Full Workspace Div2 Scout

Run:

- Release:
  `native-test-runs/20260619-112537-c4b04caf`

This run adds `mul-large-toom-div2-scout`, an observe-only active-window scout
that keeps leaf64/depth2 full recursive Toom workspace but replaces the two
exact division-by-two interpolation steps with checked bit shifts. The measured
window remains `11717`, `16384`, `24103`, `32768`, `52163`, and `65536`,
preserving deterministic in-between spots between the power-of-two anchors.

Observed aggregate:

- Exact parity/hash against leaf64, current, and GMP: `hashSafe=108/108`,
  `hashGate=matched`, `parity=matched`.
- The div2 candidate beats current production multiply on median:
  `candCurrentMax=0.726`.
- It does not beat leaf64 across the full active window:
  `candBaseMax=1.008`, `safeSizes=0/6`, `stableSampleCount=0/6`.
- GMP and worst-pair gates still fail: `candGmpMax=1.282`,
  `maxWorstPairRatio=1.408`.

Per-size point rows:

| Digits | Div2 / Leaf64 | Div2 / Current | Div2 / GMP | Worst Pair | Leaf Stable | Current Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `0.961` | `0.726` | `0.968` | `1.105` | `7/9` | `9/9` | `7/9` | leaf64-regression |
| `16384` | `0.966` | `0.721` | `0.944` | `1.098` | `5/9` | `9/9` | `7/9` | leaf64-regression |
| `24103` | `0.971` | `0.665` | `1.052` | `1.216` | `6/9` | `9/9` | `2/9` | leaf64-regression |
| `32768` | `1.008` | `0.698` | `1.119` | `1.230` | `3/9` | `9/9` | `1/9` | leaf64-regression |
| `52163` | `1.008` | `0.638` | `1.149` | `1.324` | `4/9` | `9/9` | `1/9` | leaf64-regression |
| `65536` | `0.963` | `0.645` | `1.282` | `1.408` | `5/9` | `9/9` | `0/9` | leaf64-regression |

Decision: keep div2 observe-only. Checked shift division is a useful
lower-level Toom clue and improves the GMP comparison versus leaf48, but the
strict gate still rejects it: worst pairs exceed policy limits throughout, the
random spots expose baseline misses, and no production route changes are
allowed from this evidence.

## 2026-06-19: Full Workspace Div3 Scout

Run:

- Release:
  `native-test-runs/20260619-115618-c4b04caf`

This run adds `mul-large-toom-div3-scout`, an observe-only active-window scout
that keeps leaf64/depth2 full recursive Toom workspace but replaces the exact
division-by-three interpolation step with an odd-limb exact division shortcut.
The measured window remains `11717`, `16384`, `24103`, `32768`, `52163`, and
`65536`, preserving deterministic in-between spots between the power-of-two
anchors.

Observed aggregate:

- Exact parity/hash against leaf64, current, and GMP: `hashSafe=108/108`,
  `hashGate=matched`, `parity=matched`.
- The div3 candidate beats current production multiply on median:
  `candCurrentMax=0.720`.
- It nearly beats leaf64 on median but does not clear the strict gate:
  `candBaseMax=0.999`, `safeSizes=0/6`, `stableSampleCount=0/6`.
- GMP remains ahead on larger rows: `candGmpMax=1.324`.
- Worst-pair ratio is lower than div2 but still recorded as observe-only:
  `maxWorstPairRatio=1.364`.

Per-size point rows:

| Digits | Div3 / Leaf64 | Div3 / Current | Div3 / GMP | Worst Pair | Leaf Stable | Current Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `0.929` | `0.694` | `0.971` | `1.066` | `7/9` | `9/9` | `7/9` | leaf64-regression |
| `16384` | `0.971` | `0.720` | `0.978` | `1.173` | `5/9` | `9/9` | `6/9` | leaf64-regression |
| `24103` | `0.972` | `0.609` | `1.034` | `1.091` | `5/9` | `9/9` | `2/9` | leaf64-regression |
| `32768` | `0.999` | `0.692` | `1.151` | `1.230` | `2/9` | `9/9` | `0/9` | leaf64-regression |
| `52163` | `0.981` | `0.603` | `1.193` | `1.264` | `4/9` | `9/9` | `0/9` | leaf64-regression |
| `65536` | `0.984` | `0.680` | `1.324` | `1.364` | `3/9` | `9/9` | `0/9` | leaf64-regression |

Decision: keep div3 observe-only. It is exact and improves the leaf64 baseline
comparison versus div2 on every active-window size, including deterministic
random spots, but it still misses the `<=0.98` leaf64 threshold at `32768`,
`52163`, and `65536`, never reaches the `8/9` stable-pair bar, and trails GMP
on larger rows. The next multiply slice should test a combined interpolation
shortcut or a broader interpolation/evaluation rewrite before any route audit.

## 2026-06-19: Full Workspace Div2+Div3 Scout

Run:

- Release:
  `native-test-runs/20260619-122328-c4b04caf`

This run adds `mul-large-toom-div2-div3-scout`, an observe-only active-window
scout that combines the checked division-by-two interpolation shifts with the
odd-limb exact division-by-three shortcut inside the leaf64/depth2 full
recursive Toom workspace probe. The measured window remains `11717`, `16384`,
`24103`, `32768`, `52163`, and `65536`, preserving deterministic in-between
spots between the power-of-two anchors.

Observed aggregate:

- Exact parity/hash against leaf64, current, and GMP: `hashSafe=108/108`,
  `hashGate=matched`, `parity=matched`.
- The combined candidate beats current production multiply on median:
  `candCurrentMax=0.719`.
- It also beats the leaf64 full-workspace baseline on median across the full
  active window: `candBaseMax=0.958`.
- It still does not clear the strict promotion gate:
  `safeSizes=1/6`, `stableSampleCount=1/6`.
- GMP remains ahead on larger rows: `candGmpMax=1.292`.
- Worst-pair ratio remains observe-only: `maxWorstPairRatio=1.416`.

Per-size point rows:

| Digits | Combo / Leaf64 | Combo / Current | Combo / GMP | Worst Pair | Leaf Stable | Current Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `0.930` | `0.653` | `0.930` | `1.026` | `9/9` | `9/9` | `8/9` | backend-regression |
| `16384` | `0.925` | `0.719` | `0.948` | `0.990` | `8/9` | `9/9` | `9/9` | div2-div3-scout-clean |
| `24103` | `0.913` | `0.657` | `0.998` | `1.042` | `9/9` | `9/9` | `5/9` | backend-regression |
| `32768` | `0.933` | `0.651` | `1.109` | `1.143` | `7/9` | `9/9` | `0/9` | leaf64-regression |
| `52163` | `0.958` | `0.644` | `1.141` | `1.163` | `7/9` | `9/9` | `0/9` | leaf64-regression |
| `65536` | `0.951` | `0.668` | `1.292` | `1.416` | `9/9` | `9/9` | `0/9` | backend-regression |

Decision: keep the combined shortcut observe-only. It is the first
active-window Toom interpolation scout to beat leaf64 on median at every
measured size, including the deterministic random spots, but promotion still
requires more proof: only `16384` clears all safe-size gates, GMP remains ahead
on larger rows, and stable-pair counts miss the `8/9` bar at `32768` and
`52163`. The next multiply slice should either attack the GMP-facing backend
gap or broaden interpolation/evaluation structure before any forced route
audit.

## 2026-06-19: Full Workspace Combo Leaf48 Scout

Run:

- Release:
  `native-test-runs/20260619-135039-c4b04caf`

This run adds `mul-large-toom-cmb-leaf48-scout`, an observe-only active-window
scout that keeps the combined division-by-two plus division-by-three
interpolation shortcut but lowers the recursive Toom handoff from leaf64 to
leaf48. The baseline is the same combined interpolation shortcut at leaf64, so
the row isolates whether the lower handoff helps after the interpolation
change. The measured window remains `11717`, `16384`, `24103`, `32768`,
`52163`, and `65536`, preserving deterministic in-between spots between the
power-of-two anchors.

Observed aggregate:

- Exact parity/hash against combo leaf64, current, and GMP:
  `hashSafe=108/108`, `hashGate=matched`, `parity=matched`.
- Combo leaf48 still beats current production multiply on median:
  `candCurrentMax=0.719`.
- Combo leaf48 does not beat combo leaf64 across the full active window:
  `candBaseMax=1.017`, `safeSizes=0/6`, `stableSampleCount=0/6`.
- GMP remains ahead on larger rows: `candGmpMax=1.257`.
- Worst-pair ratio remains observe-only: `maxWorstPairRatio=1.334`.

Per-size point rows:

| Digits | Combo Leaf48 / Combo Leaf64 | Combo Leaf48 / Current | Combo Leaf48 / GMP | Worst Pair | Combo64 Stable | Current Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `1.017` | `0.719` | `0.934` | `1.043` | `1/9` | `9/9` | `9/9` | combo-leaf64-regression |
| `16384` | `0.977` | `0.681` | `0.908` | `1.000` | `6/9` | `9/9` | `9/9` | combo-leaf64-regression |
| `24103` | `0.986` | `0.627` | `0.998` | `1.091` | `4/9` | `9/9` | `5/9` | combo-leaf64-regression |
| `32768` | `0.970` | `0.645` | `1.022` | `1.138` | `6/9` | `9/9` | `3/9` | combo-leaf64-regression |
| `52163` | `1.008` | `0.603` | `1.164` | `1.218` | `1/9` | `9/9` | `0/9` | combo-leaf64-regression |
| `65536` | `0.964` | `0.647` | `1.257` | `1.334` | `7/9` | `9/9` | `0/9` | combo-leaf64-regression |

Decision: keep combo leaf48 observe-only and do not route it. Lowering the leaf
handoff after the combined interpolation shortcut is exact and preserves a
large current-production win, but it fails the combo leaf64 baseline gate at
every measured size once stability and worst-pair safety are included. The
random spots are useful here: `11717` and `52163` regress against combo leaf64,
while `24103` is only a marginal median improvement and still misses stability,
GMP, and worst-pair gates. The next multiply slice should keep the combined
leaf64 scout as the stronger baseline and move toward backend/GMP gap work or a
broader Toom structure rather than another handoff-only tweak.

## 2026-06-19: Full Workspace Combo Depth3 Scout

Run:

- Release:
  `native-test-runs/20260619-142015-c4b04caf`

This run adds `mul-large-toom-cmb-depth3-scout`, an observe-only active-window
scout that keeps the combined division-by-two plus division-by-three
interpolation shortcut and raises the recursive Toom depth limit from 2 to 3
at leaf64. The baseline is the same combined interpolation shortcut at depth2,
so the row isolates whether broader recursive structure helps after the
interpolation work. The measured window remains `11717`, `16384`, `24103`,
`32768`, `52163`, and `65536`, preserving deterministic in-between spots
between the power-of-two anchors.

Observed aggregate:

- Exact parity/hash against combo depth2, current, and GMP:
  `hashSafe=108/108`, `hashGate=matched`, `parity=matched`.
- Combo depth3 still beats current production multiply on median:
  `candCurrentMax=0.729`.
- Combo depth3 does not beat combo depth2 across the full active window:
  `candBaseMax=1.014`, `safeSizes=0/6`.
- GMP remains ahead on larger rows: `candGmpMax=1.220`.
- Worst-pair ratio remains observe-only: `maxWorstPairRatio=1.277`.

Per-size point rows:

| Digits | Combo Depth3 / Combo Depth2 | Combo Depth3 / Current | Combo Depth3 / GMP | Worst Pair | ComboDepth2 Stable | Current Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `1.014` | `0.729` | `0.924` | `1.098` | `0/9` | `9/9` | `8/9` | combo-depth2-regression |
| `16384` | `1.002` | `0.717` | `0.950` | `1.031` | `2/9` | `9/9` | `9/9` | combo-depth2-regression |
| `24103` | `1.007` | `0.659` | `1.015` | `1.071` | `0/9` | `9/9` | `3/9` | combo-depth2-regression |
| `32768` | `1.005` | `0.699` | `1.124` | `1.141` | `0/9` | `9/9` | `0/9` | combo-depth2-regression |
| `52163` | `0.973` | `0.625` | `1.126` | `1.169` | `6/9` | `9/9` | `0/9` | combo-depth2-regression |
| `65536` | `0.945` | `0.638` | `1.220` | `1.277` | `9/9` | `9/9` | `0/9` | backend-regression |

Decision: keep combo depth3 observe-only and do not route it. Deeper recursion
helps the largest two rows on median, including the `52163` deterministic
random spot, but the smaller random spots and power-of-two anchors regress
against combo depth2. Since no size clears the safe-size gate and GMP remains
ahead at the upper half of the window, the next multiply slice should keep
combo depth2/leaf64 as the active baseline and look for a structural change
that moves the backend-facing gap without sacrificing the in-between sizes.

## 2026-06-19: Full Workspace Combo Lower-Window Scout

Run:

- Release:
  `native-test-runs/20260619-145709-c4b04caf`

This run adds `mul-large-toom-cmb-lower-scout`, an observe-only lower-window
gate for the current strongest combined interpolation candidate. It compares
`full-ws-combo-depth2` against the plain leaf64 full-workspace baseline,
current production multiply, and GMP/MPIR at `4096`, `5639`, and `8192`.
The `5639` row is the deterministic in-between spot for the lower
power-of-two gap. Production multiply remains unchanged.

Observed aggregate:

- Exact parity/hash against leaf64, current, and GMP:
  `hashSafe=54/54`, `hashGate=matched`, `parity=matched`.
- Combo depth2 beats current production multiply on median:
  `candCurrentMax=0.911`.
- Combo depth2 beats GMP/MPIR on median at all three lower sizes:
  `candGmpMax=0.893`.
- The lower gate is still not promotion-ready:
  `safeSizes=1/3`, `maxWorstPairRatio=1.012`.

Per-size point rows:

| Digits | Combo Depth2 / Leaf64 | Combo Depth2 / Current | Combo Depth2 / GMP | Worst Pair | Leaf64 Stable | Current Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `4096` | `0.952` | `0.911` | `0.827` | `0.973` | `9/9` | `9/9` | `9/9` | combo-lower-clean |
| `5639` | `0.964` | `0.901` | `0.792` | `1.007` | `6/9` | `9/9` | `9/9` | leaf64-regression |
| `8192` | `0.979` | `0.854` | `0.893` | `1.012` | `6/9` | `9/9` | `9/9` | leaf64-regression |

Decision: keep the lower combo scout observe-only. This is the best lower-band
signal so far because it is median-positive against current production and
GMP/MPIR at `4096`, the `5639` random spot, and `8192`. The strict proof bar
still blocks promotion because `5639` and `8192` miss baseline stability and
worst-pair safety. A route audit may be worth testing only after a repeat run
or a candidate that reduces those lower-window worst-pair tails.

## 2026-06-19: Full Workspace Combo Full-Window Route Audit

Run:

- Release:
  `native-test-runs/20260619-153624-c4b04caf`

This run adds `mul-large-toom-cmb-route-audit`, an observe-only route audit
for `full-ws-combo-depth2` over the complete multiply promotion window:
`4096`, `5639`, `8192`, `11717`, `16384`, `24103`, `32768`, `52163`, and
`65536`. Unlike the lower-window scout, this audit compares directly against
current production multiply and GMP/MPIR, so it is the route-facing verdict
for the combined interpolation candidate. Production multiply remains
unchanged.

Observed aggregate:

- Exact parity/hash against current production and GMP:
  `hashSafe=162/162`, `hashGate=matched`, `parity=matched`.
- Combo depth2 beats current production multiply at every measured size:
  `candCurrentMax=0.924`.
- GMP/MPIR still blocks promotion on the upper half:
  `candGmpMax=1.298`, `safeSizes=5/9`.
- Worst-pair safety still blocks promotion:
  `maxWorstPairRatio=1.378`.

Per-size point rows:

| Digits | Combo / Current | Combo / GMP | Current / GMP | Worst Pair | Current Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `4096` | `0.924` | `0.836` | `0.909` | `0.964` | `9/9` | `9/9` | combo-route-clean |
| `5639` | `0.882` | `0.805` | `0.922` | `0.909` | `9/9` | `9/9` | combo-route-clean |
| `8192` | `0.848` | `0.880` | `1.038` | `0.994` | `8/9` | `9/9` | combo-route-clean |
| `11717` | `0.695` | `0.929` | `1.324` | `0.983` | `9/9` | `9/9` | combo-route-clean |
| `16384` | `0.689` | `0.952` | `1.376` | `0.980` | `9/9` | `9/9` | combo-route-clean |
| `24103` | `0.642` | `1.006` | `1.560` | `1.032` | `9/9` | `4/9` | backend-regression |
| `32768` | `0.685` | `1.114` | `1.639` | `1.183` | `9/9` | `0/9` | backend-regression |
| `52163` | `0.618` | `1.135` | `1.827` | `1.145` | `9/9` | `0/9` | backend-regression |
| `65536` | `0.662` | `1.298` | `1.947` | `1.378` | `9/9` | `0/9` | backend-regression |

Decision: keep the combo route observe-only. This is the first full-window
audit showing the app-shaped combo route beats current production multiply at
every target size and every deterministic random spot. The strict proof bar
still blocks production routing because GMP/MPIR wins from `24103` through
`65536`, and the worst-pair gate fails in the same upper band. The next CPU
multiply work should target the backend-facing gap above `16384`, not another
lower-window route check.

## 2026-06-19: Combo Leaf48 Depth3 Upper Scout

Run:

- Release:
  `native-test-runs/20260619-160707-c4b04caf`

This run adds `mul-large-toom-cmb-l48d3-scout`, an observe-only upper-window
scout for combo interpolation with leaf48 and depth3. The window is focused on
the failing high end from the full route audit: `24103`, `32768`, `52163`, and
`65536`. It keeps the deterministic random spots between powers of two in the
same evidence band as the powers of two.

Observed aggregate:

- Exact parity/hash against the combo baseline, current production, and GMP:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- L48D3 improves over the leaf64/depth2 combo baseline on median at every
  upper size: `candBaseMax=0.980`.
- It strongly beats current production multiply in the same run:
  `candCurrentMax=0.633`.
- It narrows, but does not close, the GMP/MPIR gap:
  `candGmpMax=1.180`, `safeSizes=0/4`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.208`.

Per-size point rows:

| Digits | L48D3 / L64D2 | L48D3 / Current | L48D3 / GMP | L64D2 / GMP | Current / GMP | Worst Pair | Stable Base | Stable Current | Stable GMP | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.980` | `0.633` | `0.973` | `0.988` | `1.545` | `1.055` | `4/9` | `9/9` | `7/9` | combo-l64d2-regression |
| `32768` | `0.969` | `0.626` | `1.048` | `1.089` | `1.682` | `1.065` | `7/9` | `9/9` | `0/9` | combo-l64d2-regression |
| `52163` | `0.920` | `0.568` | `1.049` | `1.136` | `1.844` | `1.070` | `9/9` | `9/9` | `0/9` | backend-regression |
| `65536` | `0.917` | `0.571` | `1.180` | `1.279` | `2.041` | `1.208` | `9/9` | `9/9` | `0/9` | backend-regression |

Decision: keep L48D3 observe-only. It is useful evidence because it combines
the previous leaf and depth scout wins and reduces the upper-band backend gap
from the route audit (`candGmpMax` about `1.278` there, `1.180` here), with
`24103` now median-faster than GMP/MPIR. It still fails the strict promotion
bar at `32768`, `52163`, and `65536`, and the worst-pair/stable-pair gates do
not pass. The next CPU multiply slice should move beyond leaf/depth tuning and
try a deeper high-end structure or handoff strategy.

## 2026-06-19: Combo Leaf48 Depth4 Upper Scout

Run:

- Release:
  `native-test-runs/20260619-163518-c4b04caf`

This run adds `mul-large-toom-cmb-l48d4-scout`, an observe-only upper-window
scout for combo interpolation with leaf48 and depth4. It checks the same
upper band as the l48d3 scout: `24103`, `32768`, `52163`, and `65536`, keeping
both deterministic random spots visible next to the powers of two.

Observed aggregate:

- Exact parity/hash against l48d3, current production, and GMP:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- Depth4 does not beat the l48d3 baseline overall:
  `candBaseMax=1.030`.
- It still beats current production multiply:
  `candCurrentMax=0.632`.
- It does not close the GMP/MPIR gap:
  `candGmpMax=1.221`, `safeSizes=0/4`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.300`.

Per-size point rows:

| Digits | L48D4 / L48D3 | L48D4 / Current | L48D4 / GMP | L48D3 / GMP | Current / GMP | Worst Pair | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `1.030` | `0.602` | `0.995` | `0.967` | `1.654` | `1.090` | combo-l48d3-regression |
| `32768` | `1.018` | `0.632` | `1.029` | `1.085` | `1.757` | `1.225` | combo-l48d3-regression |
| `52163` | `0.958` | `0.543` | `1.106` | `1.125` | `1.944` | `1.199` | combo-l48d3-regression |
| `65536` | `0.991` | `0.558` | `1.221` | `1.185` | `2.117` | `1.300` | combo-l48d3-regression |

Decision: keep l48d4 observe-only and do not pursue a simple depth increase as
the next route candidate. Depth4 gives a modest middle-upper hint, especially
around `32768`, but it worsens the aggregate GMP gap and remains behind l48d3
at the high end. The next CPU multiply slice should test a different high-end
structure or handoff strategy rather than depth5.

## 2026-06-19: Combo Leaf48 Depth3 Full-Window Audit

Run:

- Release:
  `native-test-runs/20260619-184951-c4b04caf`

This run adds `mul-large-toom-cmb-l48d3-full`, an observe-only full-window
audit for the best recent upper-window combo shape. It covers the full
`4096` through `65536` campaign, including deterministic random spots
`5639`, `11717`, `24103`, and `52163`, and compares l48d3 against l64d2,
current production multiply, and GMP/MPIR in the same run.

Observed aggregate:

- Exact parity/hash across all nine sizes:
  `hashSafe=162/162`, `hashGate=matched`, `parity=matched`.
- It beats current production multiply at every measured size:
  `candCurrentMax=0.981`.
- It is not clean against the prior combo baseline:
  `candBaseMax=1.051`.
- It does not close the GMP/MPIR gap:
  `candGmpMax=1.179`, `safeSizes=0/9`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.216`.

Per-size point rows:

| Digits | L48D3 / L64D2 | L48D3 / Current | L48D3 / GMP | L64D2 / GMP | Current / GMP | Worst Pair | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `4096` | `1.051` | `0.981` | `0.872` | `0.811` | `0.897` | `1.126` | combo-l64d2-regression |
| `5639` | `0.987` | `0.875` | `0.771` | `0.759` | `0.878` | `1.112` | combo-l64d2-regression |
| `8192` | `1.014` | `0.876` | `0.901` | `0.901` | `1.039` | `1.136` | combo-l64d2-regression |
| `11717` | `1.042` | `0.735` | `0.923` | `0.887` | `1.225` | `1.203` | combo-l64d2-regression |
| `16384` | `0.991` | `0.705` | `0.937` | `0.952` | `1.336` | `1.132` | combo-l64d2-regression |
| `24103` | `0.997` | `0.667` | `0.999` | `1.014` | `1.510` | `1.118` | combo-l64d2-regression |
| `32768` | `0.964` | `0.665` | `1.068` | `1.107` | `1.601` | `1.216` | combo-l64d2-regression |
| `52163` | `0.921` | `0.600` | `1.055` | `1.141` | `1.762` | `1.065` | backend-regression |
| `65536` | `0.915` | `0.625` | `1.179` | `1.294` | `1.899` | `1.205` | backend-regression |

Decision: keep l48d3 full-window observe-only. The current-production win is
real but not sufficient for promotion because the row fails l64d2, GMP/MPIR,
worst-pair, and stability gates. The next multiply slice should not be another
simple leaf/depth tweak; it needs a different high-end structure or a narrowly
proved handoff policy.

## 2026-06-19: Combo L64D2 To L48D3 Handoff Audit

Run:

- Release:
  `native-test-runs/20260619-190919-c4b04caf`

This run adds `mul-large-toom-cmb-hand`, a benchmark-only full-window handoff
audit. The candidate uses `full-ws-combo-l64d2` below `32768` decimal digits
and switches to `full-ws-combo-l48d3` at `32768` and above. It keeps
`4096`, `5639`, `8192`, `11717`, `16384`, `24103`, `32768`, `52163`, and
`65536` in the same evidence window.

Observed aggregate:

- Exact parity/hash across all nine sizes:
  `hashSafe=162/162`, `hashGate=matched`, `parity=matched`.
- It beats current production multiply at every measured size:
  `candCurrentMax=0.887`.
- It still does not close the GMP/MPIR gap:
  `candGmpMax=1.208`, `safeSizes=3/9`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.357`.

Per-size point rows:

| Digits | Active Candidate | Candidate / Current | Candidate / GMP | Current / GMP | Worst Pair | Status |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| `4096` | `full-ws-combo-l64d2` | `0.887` | `0.813` | `0.947` | `0.935` | combo-handoff-clean |
| `5639` | `full-ws-combo-l64d2` | `0.884` | `0.799` | `0.915` | `0.966` | combo-handoff-clean |
| `8192` | `full-ws-combo-l64d2` | `0.810` | `0.900` | `1.104` | `1.357` | current-regression |
| `11717` | `full-ws-combo-l64d2` | `0.674` | `0.952` | `1.326` | `0.986` | combo-handoff-clean |
| `16384` | `full-ws-combo-l64d2` | `0.787` | `0.984` | `1.319` | `1.088` | backend-regression |
| `24103` | `full-ws-combo-l64d2` | `0.645` | `0.981` | `1.530` | `1.066` | backend-regression |
| `32768` | `full-ws-combo-l48d3` | `0.657` | `1.059` | `1.624` | `1.114` | backend-regression |
| `52163` | `full-ws-combo-l48d3` | `0.578` | `1.112` | `1.871` | `1.174` | backend-regression |
| `65536` | `full-ws-combo-l48d3` | `0.612` | `1.208` | `1.934` | `1.243` | backend-regression |

Decision: keep the handoff observe-only. It is exact and clearly improves the
current-production side of the full-window story, but it remains blocked by
GMP/MPIR, worst-pair, and stability gates. The next CPU multiply work should
attack the high-end arithmetic gap directly instead of only moving the l48d3
handoff threshold.

## 2026-06-19: Combo Upper Same-Input Tournament

Run:

- Release:
  `native-test-runs/20260619-193550-c4b04caf`

This run adds `mul-large-toom-cmb-tourn`, a benchmark-only upper-window
tournament over `full-ws-combo-l64d2`, `full-ws-combo-l48d3`,
`full-ws-combo-l48d4`, and `full-ws-combo-handoff-l64d2-l48d3`. It compares
all four candidates, current production multiply, and GMP/MPIR on identical
operand fingerprints in the same run for `24103`, `32768`, `52163`, and
`65536`.

Observed aggregate:

- Exact parity/hash across every route, sample, and operand family:
  `hashSafe=288/288`, `hashGate=matched`, `parity=matched`.
- The best per-size route beats current production multiply:
  `winnerCurrentMax=0.657`.
- It still does not close the GMP/MPIR gap:
  `winnerGmpMax=1.197`, `safeSizes=0/4`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.302`.
- Winner list:
  `full-ws-combo-l48d4,full-ws-combo-l48d4,full-ws-combo-l48d3,full-ws-combo-l48d3`.

Per-size winners:

| Digits | Winner | Winner / Current | Winner / GMP | Current / GMP | Worst Pair | Status |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| `24103` | `full-ws-combo-l48d4` | `0.587` | `0.967` | `1.587` | `1.172` | backend-regression |
| `32768` | `full-ws-combo-l48d4` | `0.657` | `1.069` | `1.627` | `1.221` | backend-regression |
| `52163` | `full-ws-combo-l48d3` | `0.574` | `1.066` | `1.813` | `1.098` | backend-regression |
| `65536` | `full-ws-combo-l48d3` | `0.612` | `1.197` | `1.906` | `1.302` | backend-regression |

Decision: keep the tournament observe-only. Same-input evidence now says l48d4
is the better lower-upper candidate, l48d3 is the better high-upper candidate,
and neither is promotion-ready against GMP/MPIR and worst-pair gates. Use this
as the next structural-design clue rather than a production route candidate.

## 2026-06-19: Combo Reusable Workspace Scout

Run:

- Release:
  `native-test-runs/20260619-200105-c4b04caf`

This run adds `mul-large-toom-cmb-reuse`, a benchmark-only no-realloc scout for
the upper-window tournament winners. It uses caller-owned recursive Toom and
Karatsuba workspaces across repeated multiply calls and compares against the
same route without workspace reuse on identical operands in the same run.

Observed aggregate:

- Exact parity/hash:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- Reuse beats the same non-reuse route on median at every size, but not by the
  strict route gate:
  `candBaseMax=0.990`.
- It beats current production multiply:
  `candCurrentMax=0.621`.
- It still does not close the GMP/MPIR gap:
  `candGmpMax=1.177`, `safeSizes=0/4`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.209`.

Per-size point rows:

| Digits | Active Candidate | Reuse / Non-Reuse | Reuse / Current | Reuse / GMP | Non-Reuse / GMP | Current / GMP | Worst Pair | Status |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `full-ws-combo-l48d4` | `0.893` | `0.613` | `0.926` | `1.014` | `1.583` | `1.138` | reuse-baseline-regression |
| `32768` | `full-ws-combo-l48d4` | `0.958` | `0.621` | `1.001` | `1.097` | `1.600` | `1.181` | reuse-baseline-regression |
| `52163` | `full-ws-combo-l48d3` | `0.948` | `0.594` | `1.079` | `1.137` | `1.819` | `1.125` | backend-regression |
| `65536` | `full-ws-combo-l48d3` | `0.990` | `0.611` | `1.177` | `1.191` | `1.916` | `1.209` | reuse-baseline-regression |

Decision: keep reusable workspaces as an implementation clue, not a route. The
allocation/preparation tax is measurable, but the upper-window gap still needs
arithmetic-structure work before a promotion audit can make sense.

## 2026-06-19: Combo Best-Map Full-Window Audit

Run:

- Release:
  `native-test-runs/20260619-202535-c4b04caf`

This run adds `mul-large-toom-cmb-map`, a benchmark-only fixed-map audit across
the full `4096` through `65536` campaign. The map uses `full-ws-combo-l64d2`
below `24103`, `full-ws-combo-l48d4` at `24103` and `32768`, and
`full-ws-combo-l48d3` at `52163` and `65536`. That keeps every deterministic
in-between spot in the same row as the power-of-two anchors.

Observed aggregate:

- Exact parity/hash:
  `hashSafe=162/162`, `hashGate=matched`, `parity=matched`.
- It beats current production multiply at every measured size:
  `candCurrentMax=0.930`.
- It still does not clear GMP/MPIR:
  `candGmpMax=1.204`, `safeSizes=0/9`.
- Worst-pair safety is the clearest blocker:
  `maxWorstPairRatio=2.869`.

Per-size point rows:

| Digits | Active Candidate | Candidate / Current | Candidate / GMP | Current / GMP | Worst Pair | Status |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| `4096` | `full-ws-combo-l64d2` | `0.921` | `0.860` | `0.945` | `1.195` | current-regression |
| `5639` | `full-ws-combo-l64d2` | `0.919` | `0.809` | `0.838` | `1.206` | current-regression |
| `8192` | `full-ws-combo-l64d2` | `0.930` | `0.953` | `1.147` | `1.560` | current-regression |
| `11717` | `full-ws-combo-l64d2` | `0.741` | `1.021` | `1.432` | `1.170` | backend-regression |
| `16384` | `full-ws-combo-l64d2` | `0.667` | `0.972` | `1.495` | `1.152` | backend-regression |
| `24103` | `full-ws-combo-l48d4` | `0.739` | `1.088` | `1.385` | `2.204` | current-regression |
| `32768` | `full-ws-combo-l48d4` | `0.634` | `1.119` | `1.758` | `2.869` | current-regression |
| `52163` | `full-ws-combo-l48d3` | `0.567` | `1.092` | `1.927` | `1.158` | backend-regression |
| `65536` | `full-ws-combo-l48d3` | `0.591` | `1.204` | `2.063` | `1.232` | backend-regression |

Decision: keep the best-map audit observe-only. The map is a useful summary of
the strongest known app-shaped multiply route and is consistently faster than
current production multiply, but it is not close to promotion-ready. The random
spots are again decision-changing: `5639`, `24103`, and `52163` all expose
different blocker shapes, while `32768` remains the worst-pair outlier.

## 2026-06-20: Combo Best-Map Duplicate-Control Audit

Local Release validation artifact
`native-test-runs/20260620-013252-c4b04caf` adds
`mul-large-toom-cmb-map-ctrl`, a duplicate-control audit for `24103` and
`32768`. The candidate and baseline are the exact same best-map route on the
same operand fingerprints; current production multiply and `mpz_mul` stay in
the same timing run.

Observed aggregate:

- Exact parity/hash:
  `hashSafe=36/36`, `hashGate=matched`, `parity=matched`.
- Duplicate control is stable:
  `controlRatioMax=0.997`, `controlWorstMax=1.077`, `controlSafety=stable`.
- The candidate still beats current production multiply:
  `candCurrentMax=0.638`.
- It still misses GMP/MPIR and stable-pair gates:
  `candGmpMax=1.040`, `safeSizes=0/2`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.091`.

Per-size point rows:

| Digits | Control Ratio | Control Worst | Candidate / Current | Candidate / GMP | Current / GMP | Product Worst | Control Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.991` | `1.077` | `0.638` | `0.979` | `1.540` | `1.058` | `9/9` | `7/9` | backend-regression |
| `32768` | `0.997` | `1.044` | `0.637` | `1.040` | `1.633` | `1.091` | `9/9` | `0/9` | backend-regression |

Decision: the best-map outlier failure is not explained by duplicate-route
measurement noise. Keep the route observe-only; the next multiply slice should
change the arithmetic structure rather than rerun the same map as a promotion
candidate.

## 2026-06-20: Combo Reuse-Map Full-Window Audit

Local Release validation artifact
`native-test-runs/20260620-015720-c4b04caf` adds
`mul-large-toom-cmb-reuse-map`, a benchmark-only full-window audit of the
mapped combo route with reusable recursive Toom workspace temporaries. The row
keeps `4096`, `5639`, `8192`, `11717`, `16384`, `24103`, `32768`, `52163`, and
`65536` in one same-run audit, preserving every deterministic random spot.

Observed aggregate:

- Exact parity/hash:
  `hashSafe=162/162`, `hashGate=matched`, `parity=matched`.
- Reuse beats the same non-reuse route on median, but not by the strict route
  gate:
  `candBaseMax=0.996`.
- It beats current production multiply at every measured size:
  `candCurrentMax=0.829`.
- It still does not clear GMP/MPIR:
  `candGmpMax=1.132`, `safeSizes=0/9`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.192`.

Per-size point rows:

| Digits | Active Candidate | Reuse / Non-Reuse | Reuse / Current | Reuse / GMP | Non-Reuse / GMP | Current / GMP | Worst Pair | GMP Stable | Status |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `4096` | `full-ws-combo-l64d2` | `0.875` | `0.807` | `0.752` | `0.847` | `0.907` | `1.091` | `9/9` | reuse-baseline-regression |
| `5639` | `full-ws-combo-l64d2` | `0.949` | `0.829` | `0.737` | `0.790` | `0.889` | `1.001` | `9/9` | reuse-baseline-regression |
| `8192` | `full-ws-combo-l64d2` | `0.979` | `0.817` | `0.855` | `0.879` | `1.048` | `1.025` | `9/9` | reuse-baseline-regression |
| `11717` | `full-ws-combo-l64d2` | `0.938` | `0.673` | `0.880` | `0.942` | `1.315` | `0.997` | `9/9` | reuse-baseline-regression |
| `16384` | `full-ws-combo-l64d2` | `0.996` | `0.689` | `0.928` | `0.940` | `1.340` | `1.008` | `9/9` | reuse-baseline-regression |
| `24103` | `full-ws-combo-l48d4` | `0.967` | `0.629` | `0.956` | `0.985` | `1.517` | `0.989` | `9/9` | reuse-baseline-regression |
| `32768` | `full-ws-combo-l48d4` | `0.976` | `0.639` | `1.014` | `1.030` | `1.585` | `1.020` | `2/9` | reuse-baseline-regression |
| `52163` | `full-ws-combo-l48d3` | `0.965` | `0.576` | `1.017` | `1.050` | `1.763` | `1.030` | `1/9` | backend-regression |
| `65536` | `full-ws-combo-l48d3` | `0.980` | `0.603` | `1.132` | `1.128` | `1.878` | `1.192` | `1/9` | reuse-baseline-regression |

Decision: reusable workspace is useful implementation evidence, but not a
production route. It beats current production multiply across the full window
and beats GMP/MPIR through `24103`, but the GMP/stability gate fails from
`32768` upward. Keep it observe-only and make the next PR an arithmetic-shape
or handoff scout, not a workspace-only promotion attempt.

## 2026-06-20: Combo Leaf32 Depth4 Upper Scout

Local Release validation artifact
`native-test-runs/20260620-021539-c4b04caf` adds
`mul-large-toom-cmb-l32d4-scout`, a benchmark-only arithmetic-shape scout for
the upper window. The candidate is `full-ws-combo-l32d4`; the baseline is
`full-ws-combo-l48d4`; current production multiply and `mpz_mul` remain in the
same timing run. The measured sizes are `24103`, `32768`, `52163`, and `65536`,
so both the random spots and power-of-two anchors stay paired.

Observed aggregate:

- Exact parity/hash:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- Leaf32/depth4 does not beat the leaf48/depth4 baseline:
  `candBaseMax=1.098`, `safeSizes=0/4`.
- It still beats current production multiply:
  `candCurrentMax=0.693`.
- It is not competitive with GMP/MPIR:
  `candGmpMax=1.188`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.228`.

Per-size point rows:

| Digits | L32D4 / L48D4 | L32D4 / Current | L32D4 / GMP | L48D4 / GMP | Current / GMP | Worst Pair | Stable vs L48D4 | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.976` | `0.651` | `0.979` | `1.001` | `1.508` | `1.014` | `5/9` | `8/9` | combo-l48d4-regression |
| `32768` | `1.070` | `0.693` | `1.079` | `1.035` | `1.585` | `1.166` | `0/9` | `0/9` | combo-l48d4-regression |
| `52163` | `1.098` | `0.645` | `1.146` | `1.052` | `1.776` | `1.165` | `0/9` | `0/9` | combo-l48d4-regression |
| `65536` | `1.011` | `0.626` | `1.188` | `1.167` | `1.900` | `1.228` | `1/9` | `0/9` | combo-l48d4-regression |

Decision: reject `l32d4` as a route direction. It slightly improves the
`24103` median but loses the larger rows to `l48d4`, and it worsens stability
and worst-pair behavior. Keep the row as negative evidence and move the next
multiply scout toward handoff or interpolation/evaluation changes.

## 2026-06-20: Combo In-Place Interpolation Upper Scout

Local Release validation artifact
`native-test-runs/20260620-024037-c4b04caf` adds
`mul-large-toom-cmb-ipdiv`, a benchmark-only interpolation-copy scout for the
upper window. The candidate is `full-ws-combo-inplace-div-l48d4`; the baseline
is the regular `full-ws-combo-l48d4`; current production multiply and
`mpz_mul` remain in the same timing run. The measured sizes are `24103`,
`32768`, `52163`, and `65536`, preserving both deterministic random spots and
power-of-two anchors.

Observed aggregate:

- Exact parity/hash:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- In-place interpolation division does not beat the regular `l48d4` baseline:
  `candBaseMax=1.025`, `safeSizes=0/4`.
- It still beats current production multiply:
  `candCurrentMax=0.658`.
- It is not competitive with GMP/MPIR:
  `candGmpMax=1.182`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.230`.

Per-size point rows:

| Digits | In-Place / L48D4 | In-Place / Current | In-Place / GMP | L48D4 / GMP | Current / GMP | Worst Pair | Stable vs L48D4 | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `1.025` | `0.628` | `1.015` | `1.004` | `1.617` | `1.167` | `3/9` | `4/9` | combo-l48d4-regression |
| `32768` | `0.995` | `0.658` | `1.054` | `1.060` | `1.609` | `1.084` | `1/9` | `0/9` | combo-l48d4-regression |
| `52163` | `1.000` | `0.568` | `1.055` | `1.054` | `1.858` | `1.070` | `0/9` | `0/9` | combo-l48d4-regression |
| `65536` | `1.009` | `0.617` | `1.182` | `1.182` | `1.923` | `1.230` | `0/9` | `0/9` | combo-l48d4-regression |

Decision: reject `ipdiv` as a promotion direction. It keeps exact parity and
beats current production multiply, but it cannot consistently beat `l48d4` or
GMP/MPIR and fails the stability/worst-pair gates. The next multiply scout
needs a larger structural change than removing interpolation copy-back.

## 2026-06-20: Combo Leaf64 Depth3 Upper Scout

Local Release validation artifact
`native-test-runs/20260620-025945-c4b04caf` adds
`mul-large-toom-cmb-l64d3-scout`, a benchmark-only leaf/depth scout for the
upper window. The candidate is `full-ws-combo-l64d3`; the baseline is
`full-ws-combo-l48d4`; current production multiply and `mpz_mul` remain in the
same timing run. The measured sizes are `24103`, `32768`, `52163`, and
`65536`, preserving both deterministic random spots and power-of-two anchors.

Observed aggregate:

- Exact parity/hash:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- Leaf64/depth3 does not beat the leaf48/depth4 baseline:
  `candBaseMax=1.059`, `safeSizes=0/4`.
- It still beats current production multiply:
  `candCurrentMax=0.696`.
- It is not competitive with GMP/MPIR:
  `candGmpMax=1.201`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.224`.

Per-size point rows:

| Digits | L64D3 / L48D4 | L64D3 / Current | L64D3 / GMP | L48D4 / GMP | Current / GMP | Worst Pair | Stable vs L48D4 | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.983` | `0.642` | `0.978` | `0.996` | `1.548` | `1.017` | `4/9` | `8/9` | combo-l48d4-regression |
| `32768` | `1.059` | `0.696` | `1.113` | `1.047` | `1.598` | `1.185` | `0/9` | `0/9` | combo-l48d4-regression |
| `52163` | `1.050` | `0.624` | `1.105` | `1.048` | `1.751` | `1.122` | `0/9` | `0/9` | combo-l48d4-regression |
| `65536` | `1.026` | `0.638` | `1.201` | `1.170` | `1.900` | `1.224` | `0/9` | `0/9` | combo-l48d4-regression |

Decision: reject `l64d3` as a route direction. It nearly helps at `24103`,
but misses the baseline gate and regresses the larger rows against `l48d4` and
GMP/MPIR. This is more evidence that simple leaf/depth reshaping is exhausted
for the upper window.

## 2026-06-20: Combo Reuse plus In-Place Interpolation Map

Local Release validation artifact
`native-test-runs/20260620-033959-c4b04caf` adds
`mul-large-toom-cmb-ripdiv`, a benchmark-only full-window audit combining
reusable recursive workspaces with in-place exact interpolation division. The
candidate is `full-ws-combo-reuse-ipdiv-map-l64d2-l48d4-l48d3`; the baseline
is the regular reusable map `full-ws-combo-reuse-map-l64d2-l48d4-l48d3`;
current production multiply and `mpz_mul` remain in the same timing run.

Observed aggregate:

- Exact parity/hash:
  `hashSafe=162/162`, `hashGate=matched`, `parity=matched`.
- Reuse plus in-place interpolation does not beat the regular reusable map:
  `candBaseMax=1.017`, `safeSizes=0/9`.
- It still beats current production multiply:
  `candCurrentMax=0.842`.
- It is not competitive with GMP/MPIR at the high end:
  `candGmpMax=1.158`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.297`.

Per-size point rows:

| Digits | Reuse+IPDiv / Reuse | Reuse+IPDiv / Current | Reuse+IPDiv / GMP | Reuse / GMP | Current / GMP | Worst Pair | Stable vs Reuse | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `4096` | `1.004` | `0.830` | `0.763` | `0.755` | `0.913` | `1.065` | `4/9` | `9/9` | reuse-baseline-regression |
| `5639` | `1.017` | `0.842` | `0.763` | `0.746` | `0.897` | `1.078` | `1/9` | `9/9` | reuse-baseline-regression |
| `8192` | `1.004` | `0.811` | `0.860` | `0.856` | `1.061` | `1.034` | `1/9` | `9/9` | reuse-baseline-regression |
| `11717` | `1.014` | `0.693` | `0.911` | `0.902` | `1.320` | `1.035` | `0/9` | `9/9` | reuse-baseline-regression |
| `16384` | `0.993` | `0.682` | `0.922` | `0.929` | `1.351` | `1.018` | `3/9` | `9/9` | reuse-baseline-regression |
| `24103` | `1.001` | `0.628` | `0.967` | `0.971` | `1.557` | `1.019` | `3/9` | `9/9` | reuse-baseline-regression |
| `32768` | `0.983` | `0.625` | `1.008` | `1.022` | `1.622` | `1.092` | `4/9` | `2/9` | reuse-baseline-regression |
| `52163` | `0.999` | `0.573` | `1.041` | `1.040` | `1.815` | `1.065` | `0/9` | `0/9` | reuse-baseline-regression |
| `65536` | `0.999` | `0.591` | `1.158` | `1.158` | `1.967` | `1.297` | `2/9` | `0/9` | reuse-baseline-regression |

Decision: reject `ripdiv` as a promotion direction. It confirms that
combining reusable workspaces with in-place interpolation still does not beat
the regular reusable map under the strict gates, even though it remains faster
than current production multiply.

## 2026-06-20: Top-Level Toom-4 Scout

Local Release validation artifact
`native-test-runs/20260620-040903-c4b04caf` adds `mul-large-toom4-top`, a
benchmark-only upper-window scout that tries one top-level Toom-4 split over
the existing reusable Toom-3 combo point products. The candidate is
`full-ws-toom4-top-l48d3`; the baseline is `full-ws-combo-l48d3`; current
production multiply and `mpz_mul` remain in the same timing run.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- Top-level Toom-4 does not beat the combo baseline across the window:
  `candBaseMax=1.054`, `safeSizes=0/4`.
- It remains faster than current production multiply:
  `candCurrentMax=0.665`.
- It is slower than GMP/MPIR at every measured upper-window size:
  `candGmpMax=1.141`.
- Worst-pair safety is not close enough for promotion:
  `maxWorstPairRatio=1.359`.

Per-size point rows:

| Digits | Toom-4 / Combo L48D3 | Toom-4 / Current | Toom-4 / GMP | Combo L48D3 / GMP | Current / GMP | Worst Pair | Stable vs Combo | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `1.003` | `0.654` | `1.002` | `1.008` | `1.534` | `1.063` | `3/9` | `4/9` | combo-l48d3-regression |
| `32768` | `0.985` | `0.665` | `1.061` | `1.077` | `1.611` | `1.137` | `4/9` | `0/9` | combo-l48d3-regression |
| `52163` | `1.054` | `0.625` | `1.141` | `1.078` | `1.831` | `1.359` | `0/9` | `1/9` | combo-l48d3-regression |
| `65536` | `0.954` | `0.595` | `1.125` | `1.176` | `1.884` | `1.156` | `8/9` | `0/9` | combo-l48d3-regression |

Decision: reject top-level Toom-4 for promotion. The power-of-two anchors show
some median improvement over combo L48D3, but the deterministic random spots
are decisive: `24103` is effectively flat-to-slower and `52163` regresses
badly enough to fail the strict safe-size, stability, GMP/MPIR, and worst-pair
gates. The result is useful evidence that broadening the top-level split alone
is not enough.

## 2026-06-20: Top-Level Toom-4 Workspace Reuse Scout

Local Release validation artifact
`native-test-runs/20260620-045841-c4b04caf` adds
`mul-large-toom4-top-reuse`, a benchmark-only no-realloc variant of the
top-level Toom-4 scout. The candidate reuses caller-owned recursive Toom-3 and
Karatsuba workspaces for Toom-4 point products; the baseline is the same
top-level Toom-4 shape without reuse; current production multiply and
`mpz_mul` remain in the same timing run.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- Reuse improves the same top-level Toom-4 shape at every measured upper size:
  `candBaseMax=0.976`.
- It still fails the strict gate:
  `safeSizes=0/4`, `maxWorstPairRatio=1.112`.
- It remains faster than current production multiply:
  `candCurrentMax=0.640`.
- It is still not competitive with GMP/MPIR at the high end:
  `candGmpMax=1.071`.

Per-size point rows:

| Digits | Reuse / Non-Reuse Toom-4 | Reuse / Current | Reuse / GMP | Non-Reuse / GMP | Current / GMP | Worst Pair | Stable vs Non-Reuse | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.976` | `0.630` | `0.954` | `0.978` | `1.513` | `1.013` | `5/9` | `8/9` | toom4-reuse-baseline-regression |
| `32768` | `0.960` | `0.640` | `1.040` | `1.091` | `1.640` | `1.112` | `7/9` | `0/9` | toom4-reuse-baseline-regression |
| `52163` | `0.958` | `0.580` | `1.043` | `1.086` | `1.796` | `1.052` | `9/9` | `0/9` | backend-regression |
| `65536` | `0.970` | `0.552` | `1.071` | `1.101` | `1.920` | `1.105` | `5/9` | `3/9` | toom4-reuse-baseline-regression |

Decision: keep top-level Toom-4 workspace reuse as an opt-in diagnostic probe.
The reuse win is real, including at deterministic random spots, but it does
not clear stable-pair, worst-pair, safe-size, or GMP/MPIR gates. The next
multiply step should change the arithmetic shape or handoff strategy rather
than only shaving recursive point-product allocation.

## 2026-06-20: Top-Level Toom-4 Inner-Handoff Scout

Local Release validation artifact
`native-test-runs/20260620-054314-c4b04caf` adds
`mul-large-toom4-top-handoff`, a benchmark-only handoff scout for the reusable
top-level Toom-4 route. The candidate is `full-ws-toom4-top-reuse-l64d2`; the
baseline is `full-ws-toom4-top-reuse-l48d3`; current production multiply and
`mpz_mul` remain in the same timing run.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- `leaf64/depth2` improves median time over `leaf48/depth3` at every measured
  upper size, but only barely at `65536`:
  `candBaseMax=0.995`.
- It still fails the strict gate:
  `safeSizes=0/4`, `maxWorstPairRatio=1.255`.
- It remains faster than current production multiply:
  `candCurrentMax=0.650`.
- It is not competitive with GMP/MPIR at the high end:
  `candGmpMax=1.134`.

Per-size point rows:

| Digits | L64D2 / L48D3 Toom-4 | L64D2 / Current | L64D2 / GMP | L48D3 / GMP | Current / GMP | Worst Pair | Stable vs L48D3 | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.961` | `0.650` | `0.987` | `1.040` | `1.519` | `1.103` | `6/9` | `5/9` | toom4-handoff-baseline-regression |
| `32768` | `0.946` | `0.630` | `1.068` | `1.107` | `1.720` | `1.221` | `6/9` | `1/9` | toom4-handoff-baseline-regression |
| `52163` | `0.967` | `0.584` | `1.053` | `1.088` | `1.826` | `1.255` | `6/9` | `2/9` | toom4-handoff-baseline-regression |
| `65536` | `0.995` | `0.601` | `1.134` | `1.159` | `1.881` | `1.198` | `1/9` | `0/9` | toom4-handoff-baseline-regression |

Decision: reject the `leaf64/depth2` inner handoff for promotion. The median
improvement is real but too thin and too unstable, especially at `65536`, and
the route remains GMP/MPIR and worst-pair unsafe. Do not continue by only
tuning the inner Toom-3 leaf/depth pair.

## 2026-06-20: Top-Level Toom-4 Factored-Division Scout

Local Release validation artifact
`native-test-runs/20260620-063633-c4b04caf` adds
`mul-large-toom4-top-fdiv`, a benchmark-only interpolation scout for the
reusable top-level Toom-4 route. The candidate is
`full-ws-toom4-top-reuse-factored-div-l48d3`; the baseline is
`full-ws-toom4-top-reuse-l48d3`; current production multiply and `mpz_mul`
remain in the same timing run.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- Factored exact division does not beat the reusable Toom-4 baseline across the
  window:
  `candBaseMax=1.017`, `safeSizes=0/4`.
- It remains faster than current production multiply:
  `candCurrentMax=0.661`.
- It is not competitive with GMP/MPIR at the high end:
  `candGmpMax=1.094`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.162`.

Per-size point rows:

| Digits | Factored Div / Reuse Toom-4 | Factored Div / Current | Factored Div / GMP | Reuse Toom-4 / GMP | Current / GMP | Worst Pair | Stable vs Reuse | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `1.017` | `0.661` | `0.948` | `0.936` | `1.481` | `1.162` | `2/9` | `7/9` | toom4-fdiv-baseline-reg |
| `32768` | `0.979` | `0.624` | `1.025` | `1.044` | `1.645` | `1.051` | `5/9` | `3/9` | toom4-fdiv-baseline-reg |
| `52163` | `0.977` | `0.561` | `1.022` | `1.050` | `1.809` | `1.044` | `6/9` | `1/9` | toom4-fdiv-baseline-reg |
| `65536` | `0.984` | `0.553` | `1.094` | `1.110` | `1.986` | `1.131` | `3/9` | `0/9` | toom4-fdiv-baseline-reg |

Decision: reject factored exact division as a promotion path. It confirms the
Toom-4 interpolation divisions are exact and somewhat worth isolating, but
generic small-divisor division is not the remaining blocker: `24103` regresses
against the reusable baseline, and the upper rows remain GMP/MPIR and
worst-pair unsafe. Keep the row observe-only and move the next scout to a
different multiplication structure or a broader handoff strategy.

## 2026-06-20: Top-Level Toom-4 Reuse Versus Combo Reuse Audit

Local Release validation artifact
`native-test-runs/20260620-072031-c4b04caf` adds
`mul-large-toom4-top-vs-cmb`, a benchmark-only same-run audit for reusable
top-level Toom-4 against the reusable Toom-3 combo route. The candidate is
`full-ws-toom4-top-reuse-l48d3`; the baseline is
`full-ws-combo-reuse-l48d3`; current production multiply and `mpz_mul` remain
in the same timing run.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- Reusable top-level Toom-4 does not beat reusable combo across the upper
  window:
  `candBaseMax=1.036`, `safeSizes=0/4`.
- It remains faster than current production multiply:
  `candCurrentMax=0.606`.
- It is not competitive with GMP/MPIR at the high end:
  `candGmpMax=1.115`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.279`.

Per-size point rows:

| Digits | Toom-4 Reuse / Combo Reuse | Toom-4 Reuse / Current | Toom-4 Reuse / GMP | Combo Reuse / GMP | Current / GMP | Worst Pair | Stable vs Combo | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `1.035` | `0.591` | `1.014` | `0.980` | `1.660` | `1.164` | `1/9` | `3/9` | toom4-combo-baseline-re |
| `32768` | `1.036` | `0.606` | `0.984` | `0.985` | `1.647` | `1.246` | `1/9` | `5/9` | toom4-combo-baseline-re |
| `52163` | `0.975` | `0.520` | `1.006` | `1.059` | `1.929` | `1.086` | `6/9` | `3/9` | toom4-combo-baseline-re |
| `65536` | `1.012` | `0.537` | `1.115` | `1.131` | `2.124` | `1.279` | `4/9` | `1/9` | toom4-combo-baseline-re |

Decision: keep reusable top-level Toom-4 as evidence, not a route candidate.
It remains exact and faster than current production multiply, but reusable
combo is still the stronger upper-window comparator in the strict gate:
`24103`, `32768`, and `65536` regress against combo reuse, the lone `52163`
median win is not stable enough, and GMP/MPIR plus worst-pair gates remain
blocked. The next multiply branch should leave this Toom-4 line and try a
different multiplication structure or a wider handoff strategy.

## 2026-06-20: Combo Reuse GMP Duplicate-Control Audit

Local Release validation artifact
`native-test-runs/20260620-091748-c4b04caf` adds
`mul-large-toom-cmb-gmpctrl`, a benchmark-only duplicate-control audit for the
reusable combo map. The candidate is
`full-ws-combo-reuse-map-l64d2-l48d4-l48d3`; current production multiply,
primary `mpz_mul`, and duplicate `mpz_mul` remain in the same rotating run for
`24103`, `32768`, `52163`, and `65536`.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- The duplicate GMP/MPIR lane is stable in this run:
  `gmpControlRatioMax=1.014`, `gmpControlWorstMax=1.129`,
  `gmpControlSafety=stable`.
- The reusable combo map still fails the strict upper-window gate:
  `candGmpMax=1.106`, `candGmpDuplicateMax=1.112`, `safeSizes=0/4`.
- Current production multiply remains much slower than GMP/MPIR at the high
  end:
  `currentGmpMax=1.893`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.237`.

Per-size point rows:

| Digits | Candidate / GMP | Candidate / GMP Duplicate | Current / GMP | GMP Control | GMP Control Worst | Worst Pair | GMP Stable | Control Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.962` | `0.961` | `1.523` | `0.981` | `1.055` | `1.055` | `7/9` | `9/9` | backend-regression |
| `32768` | `1.031` | `1.049` | `1.566` | `0.996` | `1.129` | `1.129` | `3/9` | `8/9` | backend-regression |
| `52163` | `1.033` | `1.035` | `1.782` | `0.987` | `1.091` | `1.093` | `2/9` | `9/9` | backend-regression |
| `65536` | `1.106` | `1.112` | `1.893` | `1.014` | `1.098` | `1.237` | `0/9` | `9/9` | backend-regression |

Decision: keep the duplicate-control row as methodology evidence and do not
promote the reusable combo map. The stable duplicate `mpz_mul` lane makes the
result sharper: the upper-window miss is a backend/stability gap, not just a
noisy oracle comparison. `24103` is still useful as a deterministic random
spot because it is median-positive but misses the stable-pair bar by one pair;
`32768`, `52163`, and `65536` remain slower than GMP/MPIR. The next multiply
work should keep the control-aware same-run methodology and move to a different
arithmetic shape or broader handoff.

## 2026-06-20: Toom-3 Neg2 Arithmetic-Shape Scout

Local Release validation artifact
`native/build-codex-neg2-msvc142-nmake/native-test-runs/20260620-104543-c4b04caf`
adds `mul-large-toom-cmb-neg2`, a benchmark-only reusable Toom-3 combo scout
that evaluates the non-unit point at `-2` instead of `+2`. The candidate is
`full-ws-combo-reuse-neg2-l64d2-l48d4-l48d3`; the baseline is the regular
reusable combo map `full-ws-combo-reuse-map-l64d2-l48d4-l48d3`; current
production multiply and `mpz_mul` remain in the same timing run for `24103`,
`32768`, `52163`, and `65536`.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=72/72`, `hashGate=matched`, `parity=matched`.
- Neg2 does not beat the reusable combo map:
  `candBaseMax=1.065`, `safeSizes=0/4`.
- It remains faster than current production multiply:
  `candCurrentMax=0.614`.
- It is still not competitive with GMP/MPIR at the high end:
  `candGmpMax=1.111`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.199`.

Per-size point rows:

| Digits | Neg2 / Reuse Map | Neg2 / Current | Neg2 / GMP | Reuse Map / GMP | Current / GMP | Worst Pair | Stable vs Reuse | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.980` | `0.556` | `0.950` | `0.996` | `1.764` | `1.199` | `4/9` | `8/9` | reuse-neg2-baseline-regression |
| `32768` | `1.065` | `0.614` | `1.059` | `1.018` | `1.673` | `1.108` | `1/9` | `1/9` | reuse-neg2-baseline-regression |
| `52163` | `1.020` | `0.544` | `1.015` | `1.006` | `1.865` | `1.180` | `4/9` | `3/9` | reuse-neg2-baseline-regression |
| `65536` | `1.006` | `0.531` | `1.111` | `1.080` | `1.980` | `1.168` | `3/9` | `1/9` | reuse-neg2-baseline-regression |

Decision: reject neg2 as a promotion direction. The probe is exact and useful,
but the signed evaluation/interpolation arithmetic shape fails every upper
safe-size gate against the reusable combo map and does not solve the GMP/MPIR
backend gap. Future multiply work should use the same mixed-window methodology
but move to a broader handoff or a different multiplication structure, not
another small Toom-3 interpolation reshuffle.

## 2026-06-20: Top-Level Toom-5 Smoke Scout

Local Release validation artifact `native-test-runs/20260620-130732-c4b04caf`
adds `mul-large-toom5-top-reuse`, a benchmark-only smoke scout for a top-level
Toom-5 split. The candidate is `full-ws-toom5-top-reuse-l32d2`; the baseline is
`full-ws-combo-reuse-l32d2`; current production multiply and `mpz_mul` remain in
the same rotating run. The window is intentionally tiny, with the deterministic
random spot `5639` and the power-of-two anchor `8192`, so this is structure
evidence rather than a promotion audit.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=12/12`, `hashGate=matched`, `parity=matched`.
- Toom-5 beats the reusable combo baseline on median in the smoke window:
  `candBaseMax=0.767`.
- It is not safe against current production across both sizes:
  `candCurrentMax=1.031`, `safeSizes=0/2`.
- It is close to GMP/MPIR but not a proven win:
  `candGmpMax=0.980`, with only `2/3` GMP-stable pairs at `8192`.
- Worst-pair safety remains blocked:
  `maxWorstPairRatio=1.167`.

Per-size point rows:

| Digits | Toom-5 / Combo Reuse | Toom-5 / Current | Toom-5 / GMP | Combo / GMP | Current / GMP | Worst Pair | Stable vs Combo | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `5639` | `0.767` | `1.031` | `0.778` | `1.015` | `0.673` | `1.167` | `2/3` | `3/3` | toom5-combo-baseline-re |
| `8192` | `0.447` | `0.898` | `0.980` | `2.601` | `1.091` | `1.163` | `3/3` | `2/3` | current-regression |

Decision: keep top-level Toom-5 as observe-only evidence. It shows a real
structural median win over the same leaf/depth combo baseline, but the
lower-window random spot and worst-pair gates block promotion. Production
multiply remains unchanged; any future Toom-5 work needs a cheaper
full-window audit before route-gate consideration.

## 2026-06-20: Top-Level Toom-5 Leaf48 Handoff Smoke

Local Release validation artifact `native-test-runs/20260620-134707-c4b04caf`
adds `mul-large-toom5-top-handoff`, a benchmark-only smoke scout for the same
top-level Toom-5 structure with a leaf48/depth2 point-product handoff. The
candidate is `full-ws-toom5-top-reuse-l48d2`; the baseline is
`full-ws-combo-reuse-l48d2`; current production multiply and `mpz_mul` remain in
the same rotating run. The window uses deterministic random spot `11717` and
power-of-two anchor `16384`.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=12/12`, `hashGate=matched`, `parity=matched`.
- Leaf48 Toom-5 loses to the matching reusable combo baseline:
  `candBaseMax=1.252`, with `stableBase=0/3` at both sizes.
- It remains faster than current production multiply:
  `candCurrentMax=0.795`.
- It does not solve the GMP/MPIR gap:
  `candGmpMax=1.156`.
- Worst-pair safety is far outside the gate:
  `maxWorstPairRatio=1.566`.

Per-size point rows:

| Digits | Toom-5 / Combo Reuse | Toom-5 / Current | Toom-5 / GMP | Combo / GMP | Current / GMP | Worst Pair | Stable vs Combo | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `1.173` | `0.753` | `0.832` | `0.698` | `1.087` | `1.275` | `0/3` | `2/3` | toom5-combo-baseline-re |
| `16384` | `1.252` | `0.795` | `1.156` | `0.875` | `1.335` | `1.566` | `0/3` | `1/3` | toom5-combo-baseline-re |

Decision: reject the leaf48/depth2 Toom-5 handoff as a route candidate. It is
exact, but the matching combo reuse baseline is substantially better and the
high row is still slower than GMP/MPIR. Future multiply work should stop tuning
top-level Toom-5 handoff in this form and move to lower-level backend structure
or another multiplication family.

## 2026-06-20: Combo Reuse Transition GMP Duplicate-Control Audit

Local Release validation artifact `native-test-runs/20260620-143501-c4b04caf`
adds `mul-large-toom-cmb-gmptrans`, a benchmark-only duplicate-control audit for
the reusable combo map at deterministic random spot `11717` and power-of-two
anchor `16384`. The candidate is
`full-ws-combo-reuse-map-l64d2-l48d4-l48d3`; current production multiply,
primary `mpz_mul`, and duplicate `mpz_mul` remain in the same rotating run.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=36/36`, `hashGate=matched`, `parity=matched`.
- The duplicate GMP/MPIR lane is stable in the transition window:
  `gmpControlRatioMax=1.025`, `gmpControlWorstMax=1.073`,
  `gmpControlSafety=stable`.
- The reusable combo map passes the strict transition-window control gate:
  `candGmpMax=0.924`, `candGmpDuplicateMax=0.908`, `safeSizes=2/2`.
- Current production multiply is slower in the same run:
  `currentGmpMax=1.401`.
- Worst-pair safety is clean for this window:
  `maxWorstPairRatio=1.080`.

Per-size point rows:

| Digits | Candidate / GMP | Candidate / GMP Duplicate | Current / GMP | GMP Control | GMP Control Worst | Worst Pair | GMP Stable | Control Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `0.924` | `0.903` | `1.347` | `1.025` | `1.073` | `1.073` | `9/9` | `9/9` | combo-gmp-control-clean |
| `16384` | `0.906` | `0.908` | `1.401` | `0.991` | `1.058` | `1.080` | `9/9` | `9/9` | combo-gmp-control-clean |

Decision: keep this as observe-only evidence and do not change production
routing. The transition window is genuinely promising and the duplicate
GMP/MPIR control says the win is not an oracle-noise artifact, but promotion
still needs a forced route audit and cannot ignore the previously measured
upper-window GMP miss.
## 2026-06-20: Combo Reuse Transition Forced Route Audit

Local Release validation artifact `native-test-runs/20260620-162931-c4b04caf`
adds `mul-large-toom-cmb-troute`, a benchmark-only forced route audit for the
reusable combo map at deterministic random spot `11717` and power-of-two anchor
`16384`. The candidate is
`full-ws-combo-reuse-map-l64d2-l48d4-l48d3`; current production multiply,
`mpz_mul`, and the nonreuse combo baseline remain in the same rotating run.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=36/36`, `hashGate=matched`, `parity=matched`.
- The candidate still beats current production multiply in this transition
  pocket:
  `candCurrentMax=0.660`, while `currentGmpMax=1.388`.
- The candidate still beats GMP/MPIR at both sizes:
  `candGmpMax=0.971`.
- The strict route gate fails against the nonreuse combo baseline:
  `candBaseMax=1.041`, `safeSizes=0/2`.
- Worst-pair and stability are not promotion quality:
  `maxWorstPairRatio=1.642`, `stableBase=4/9` at `11717`, and
  `stableBase=2/9` at `16384`.

Per-size point rows:

| Digits | Candidate / Baseline | Candidate / Current | Candidate / GMP | Baseline / GMP | Current / GMP | Worst Pair | Baseline Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `0.987` | `0.598` | `0.861` | `0.794` | `1.316` | `1.115` | `4/9` | `8/9` | reuse-baseline-regression |
| `16384` | `1.041` | `0.660` | `0.971` | `0.923` | `1.388` | `1.642` | `2/9` | `5/9` | reuse-baseline-regression |

Decision: keep this as observe-only evidence and do not promote the transition
route. The random spot plus anchor are exact and faster than current production
multiply, but the forced route audit fails the baseline, stable-pair, and
worst-pair bars needed for a default-route change.

## 2026-06-21: Combo Reuse Transition Self-Control Audit

Local Release validation artifact
`native-test-runs/20260621-063109-transition-self-control/benchmark.tsv`
adds `mul-large-toom-cmb-tctrl`, a benchmark-only duplicate-route control for
the transition pocket at deterministic random spot `11717` and power-of-two
anchor `16384`. The candidate and duplicate baseline are both
`full-ws-combo-reuse-map-l64d2-l48d4-l48d3`, each measured through an
independent reusable workspace. Current production multiply and `mpz_mul` stay
in the same rotating run.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=36/36`, `hashGate=matched`, `parity=matched`.
- The duplicate route ratio was close, but the control was still noisy in this
  local pass:
  `controlRatioMax=0.997`, `controlWorstMax=1.140`,
  `controlSafety=noisy`.
- The candidate still beats current production multiply and GMP/MPIR in this
  transition pocket:
  `candCurrentMax=0.676`, `candGmpMax=0.925`, while
  `currentGmpMax=1.378`.
- The strict self-control gate remains observe-only:
  `safeSizes=1/2`, `safeSizeChunks=11717`,
  `maxWorstPairRatio=1.140`.

Per-size point rows:

| Digits | Control Ratio | Control Worst | Candidate / Current | Candidate / GMP | Current / GMP | Product Worst | Control Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `0.982` | `1.018` | `0.635` | `0.879` | `1.378` | `0.975` | `9/9` | `9/9` | combo-reuse-map-self-control-clean |
| `16384` | `0.997` | `1.140` | `0.676` | `0.925` | `1.340` | `0.988` | `7/9` | `9/9` | duplicate-control-noise |

Decision: keep this as observe-only noise-triage evidence and do not promote
the transition route. The new row is valuable because a focused
`mul-combo-transition` run can now distinguish a promising transition pocket
from a noisy same-route comparison before spending time on wider benchmark
sweeps. In this local run, only the `11717` measured point formed a safe chunk;
that is a contiguous measured-point clue, not proof of an unmeasured interval.

## 2026-06-21: Combo Transition Tournament Scout

Local Release validation artifact
`native-test-runs/20260621-064718-transition-tournament/benchmark.tsv`
adds `mul-large-toom-cmb-ttourn`, a benchmark-only same-input tournament over
`11717`, `16384`, and `24103`. The row compares `l64d2`, `l48d3`, `l48d4`,
and a `16384` handoff route on the same operand fingerprints while keeping
current production multiply and `mpz_mul` in the rotating run.

Observed aggregate:

- Exact parity/hash passed across every route and size:
  `hashSafe=216/216`, `hashGate=matched`, `parity=matched`.
- The handoff route won all three measured sizes:
  `winnerList=full-ws-combo-handoff-l64d2-l48d3,...`.
- The best winner remains observe-only:
  `winnerCurrentMax=0.663`, `winnerGmpMax=1.060`,
  `currentGmpMax=1.598`, `maxWorstPairRatio=1.307`.
- Contiguous measured safety is still narrow:
  `safeSizes=1/3`, `safeSizeChunks=11717`,
  `longestSafeSizeChunk=11717`.

Per-size winner rows:

| Digits | Winner | Winner / Current | Winner / GMP | Current / GMP | Worst Pair | GMP Stable | Status |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | handoff active `l64d2` | `0.614` | `0.904` | `1.490` | `0.989` | `9/9` | tournament-winner |
| `16384` | handoff active `l48d3` | `0.649` | `0.884` | `1.386` | `1.035` | `8/9` | backend-regression |
| `24103` | handoff active `l48d3` | `0.663` | `1.060` | `1.598` | `1.307` | `2/9` | backend-regression |

Decision: keep this as a fast transition-ladder scout, not promotion evidence.
The row says the handoff shape is the best local route to inspect next, but
only `11717` forms a safe measured chunk in this run. Piecemeal follow-up
should expand around that chunk before spending full-suite time on the larger
transition window.

## 2026-06-21: Combo Handoff Pocket Scout

Local Release validation artifact
`native-test-runs/20260621-065905-handoff-pocket/benchmark.tsv` adds
`mul-large-toom-cmb-hpocket`, a benchmark-only handoff pocket scout over seven
measured transition sizes:
`10007`, `10733`, `11717`, `12553`, `13649`, `14831`, and `16384`.
The row keeps the handoff shape from the transition tournament, using
`l64d2` below `16384` and `l48d3` at `16384`, while comparing against current
production multiply and `mpz_mul` on the same operand fingerprints.

Observed aggregate:

- Exact parity/hash passed:
  `hashSafe=126/126`, `hashGate=matched`, `parity=matched`.
- The candidate beats current production multiply across the measured pocket:
  `candCurrentMax=0.771`.
- The GMP/MPIR signal is promising but not clean:
  `candGmpMax=1.015`, with the only median GMP regression at `10733`.
- Strict contiguous safety rejects the pocket:
  `safeSizes=0/7`, `safeSizeChunks=none`,
  `longestSafeSizeChunk=none`, `maxWorstPairRatio=1.531`.

Per-size handoff signal:

| Digits | Active Route | Candidate / Current | Candidate / GMP | Current / GMP | Worst Pair | Current Stable | GMP Stable | Status |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `10007` | `l64d2` | `0.771` | `0.971` | `1.288` | `1.092` | `9/9` | `6/9` | backend-regression |
| `10733` | `l64d2` | `0.729` | `1.015` | `1.373` | `1.531` | `8/9` | `3/9` | current-regression |
| `11717` | `l64d2` | `0.721` | `0.946` | `1.335` | `1.001` | `9/9` | `8/9` | backend-regression |
| `12553` | `l64d2` | `0.674` | `0.947` | `1.418` | `1.029` | `9/9` | `8/9` | backend-regression |
| `13649` | `l64d2` | `0.707` | `0.928` | `1.348` | `1.002` | `9/9` | `8/9` | backend-regression |
| `14831` | `l64d2` | `0.697` | `0.906` | `1.393` | `1.333` | `9/9` | `8/9` | backend-regression |
| `16384` | `l48d3` | `0.743` | `0.994` | `1.427` | `1.406` | `9/9` | `5/9` | backend-regression |

Decision: keep this as negative pocket evidence, not a production route. The
handoff shape is exact and materially faster than current production multiply,
but the expanded transition pocket has no strict safe measured chunk in this
run. Future novelty work should use duplicate controls or repeated focused runs
before widening the window, because the same area can look attractive by median
while still failing pair-level safety.

## 2026-06-21: Focus Repeat Triage

The `bench_focus_repeat.py` helper was used to rerun the two cheap transition
focus lanes three times each and preserve raw TSV plus progress TSV artifacts:

- Handoff pocket repeat:
  `native-test-runs/20260621-072833-handoff-pocket-repeat3/summary.tsv`
- Transition controls repeat:
  `native-test-runs/20260621-072833-transition-controls-repeat3/summary.tsv`

Handoff-pocket aggregate rows:

| Run | Status | Candidate / Current | Worst Pair | Safe Sizes | Safe Chunk | Decision |
| ---: | --- | ---: | ---: | ---: | --- | --- |
| `1` | backend-regression | `0.770` | `1.620` | `0/7` | none | reject |
| `2` | backend-regression | `0.702` | `1.823` | `1/7` | `14831` | single-point noise |
| `3` | worst-pair-regression | `0.726` | `1.197` | `0/7` | none | reject |

Transition-control aggregate rows:

| Run | Row | Status | Worst Pair | Safe Sizes | Safe Chunk |
| ---: | --- | --- | ---: | ---: | --- |
| `1` | `mul-large-toom-cmb-tctrl` | duplicate-control-noise | `1.202` | `1/2` | `16384` |
| `1` | `mul-large-toom-cmb-gmptrans` | gmp-control-noise | `1.663` | `0/2` | none |
| `2` | `mul-large-toom-cmb-tctrl` | self-control-clean | `1.121` | `2/2` | `11717-16384` |
| `2` | `mul-large-toom-cmb-gmptrans` | gmp-control-noise | `1.252` | `0/2` | none |
| `3` | `mul-large-toom-cmb-tctrl` | duplicate-control-noise | `1.160` | `0/2` | none |
| `3` | `mul-large-toom-cmb-gmptrans` | worst-pair-regression | `1.210` | `0/2` | none |

Decision: do not spend the next implementation slice promoting or widening the
current handoff pocket. The repeat run showed no repeat-stable safe chunk, and
the duplicate-GMP control never produced a safe transition chunk. Use the focus
helper as a first-pass noise filter, then move novelty work toward a different
route shape or lower-level Toom arithmetic instead of rerunning this exact
handoff policy as if it were still promising.

The helper now writes `repeat_stable_chunks.tsv` beside the per-run
`summary.tsv`. That file groups rows by operation and intersects
`safeSizeChunks` across all repeats, so a pocket that appears in only one noisy
run is visibly different from a measured chunk that survives repeated focused
scouts. This is reporting only; it does not relax route promotion gates.

For broader novelty triage, `bench_focus_matrix.py` wraps several
`bench_focus_repeat.py` runs and writes a top-level `matrix.tsv` with
repeat-stable chunks by focus and operation. Use it to choose the next deeper
audit from cheap focus families; do not treat a matrix hit as promotion-ready
without the normal parity, worst-pair, stable-pair, and route-audit gates.

Local two-repeat novelty matrix artifact:
`native-test-runs/20260621-083300-novelty-matrix-repeat2/matrix.tsv`

| Focus | Operation | Runs With Safe Chunks | Repeat-Stable Chunk | Worst Pair Max | Status |
| --- | --- | ---: | --- | ---: | --- |
| `mul-toom5-smoke` | `mul-large-toom5-top-handoff` | `0/2` | none | `1.249` | reject |
| `mul-toom5-smoke` | `mul-large-toom5-top-reuse` | `0/2` | none | `1.469` | reject |
| `mul-toom-div-transition` | `mul-large-toom-div2-scout` | `0/2` | none | `1.774` | reject |
| `mul-toom-div-transition` | `mul-large-toom-div3-scout` | `0/2` | none | `1.320` | reject |
| `mul-toom-div-transition` | `mul-large-toom-div2-div3-scout` | `0/2` | none | `1.216` | reject |
| `mul-combo-handoff-pocket` | `mul-large-toom-cmb-hpocket` | `1/2` | none | `1.422` | reject |

Decision: do not spend the next implementation slice on Toom-5 smoke,
div2/div3 transition arithmetic, or the current handoff-pocket route. The
matrix produced no repeat-stable safe chunks, and the only single-run handoff
safe chunk disappeared under intersection. Move novelty work toward a different
route shape or a lower-level backend gap, then use the matrix helper again
before any wider route audit.

## 2026-06-21: Backend Gap Focus

Local repeat artifact:
`native-test-runs/20260621-090500-backend-gap-repeat3/repeat_stable_chunks.tsv`

This pass adds and exercises `--bench-focus mul-backend-gap`, a cheap MSVC x64
focus lane for the existing low-level multiply backend probes. It runs the
muladd unroll4/unroll8 and BMI2/ADX primitive rows, then checks unroll4
multiply against scratch and GMP/MPIR at
`4096`, `5639`, `8192`, `11717`, and `16384` digits. The new aggregate row
`mul-backend-gap-unroll4` reports `safeSizeChunks` so the same repeat-stable
helper can classify backend pockets before a broader route audit.

Three-repeat result:

| Operation | Runs With Safe Chunks | Repeat-Stable Chunk | Worst Pair Max | Decision |
| --- | ---: | --- | ---: | --- |
| `mul-backend-gap-unroll4` | `0/3` | none | `1.676684` | reject |
| `mul-unroll4-vs-gmp` | `0/3` | none | `1.676684` | reject |
| `mul-unroll4-vs-scratch` | `0/3` | none | `1.229143` | reject |
| `muladd-bmi2-adx` | `0/3` | none | `1.359695` | reject |
| `muladd-unroll4` | `0/3` | none | `1.324864` | reject |
| `muladd-unroll8` | `0/3` | none | `1.567086` | reject |

Decision: keep `mul-backend-gap` as a quick first-pass scout, but do not spend
the next novelty slice on the current unroll4/BMI2/ADX backend shape. The
backend gap did not produce a repeat-stable measured chunk, so the next
multiply novelty should look for a different route shape or a lower-level
change not covered by the existing unroll4 addmul probes.

## 2026-06-21: Toom Div Transition Focus

Local Release validation artifact
`native-test-runs/20260621-073742-mul-toom-div-transition-focus/benchmark.tsv`
runs the existing div2, div3, and div2+div3 Toom arithmetic scouts only over
the transition sizes `11717`, `16384`, and `24103`.

| Row | Sizes | Candidate / Leaf64 | Worst Pair | Safe Sizes | Decision |
| --- | --- | ---: | ---: | ---: | --- |
| `mul-large-toom-div2-scout` | `11717,16384,24103` | `1.025` | `1.380` | `0/3` | reject |
| `mul-large-toom-div3-scout` | `11717,16384,24103` | `1.010` | `1.213` | `0/3` | reject |
| `mul-large-toom-div2-div3-scout` | `11717,16384,24103` | `1.000` | `2.145` | `0/3` | reject |

Decision: the transition-window div shortcuts are exact but not promising as
the next novelty route. They regress against the leaf64 baseline in this
focused run and produce no safe measured sizes. Keep the new focus lane for
cheap reruns, but move implementation novelty away from div2/div3 arithmetic
unless a future CPU/build combination changes this result.

## 2026-06-21: Toom-5 Smoke Focus

Local Release validation artifact
`native-test-runs/20260621-075205-mul-toom5-smoke-focus/benchmark.tsv`
runs the existing Toom-5 top-level smoke rows only over `5639`, `8192`,
`11717`, and `16384` via `--bench-focus mul-toom5-smoke`.

| Row | Sizes | Candidate / Combo Baseline | Candidate / Current | Candidate / GMP | Worst Pair | Safe Sizes | Safe Chunk | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| `mul-large-toom5-top-reuse` | `5639,8192` | `1.201` | `1.059` | `1.050` | `1.213` | `0/2` | none | reject |
| `mul-large-toom5-top-handoff` | `11717,16384` | `1.110` | `0.773` | `1.013` | `1.171` | `0/2` | none | reject |

Decision: keep `mul-toom5-smoke` as a cheap topology check, but do not widen
Toom-5 top-level work next. The rows are exact (`hashSafe=12/12` per aggregate)
and useful as a fast novelty filter, but both regress against their combo
reuse baselines and produce no contiguous safe measured chunk.
