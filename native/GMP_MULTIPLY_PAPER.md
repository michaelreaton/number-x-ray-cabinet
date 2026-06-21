# Beating GMP In Pockets: Verified App-Shaped Bigint Multiply In Number X-Ray

Draft date: 2026-06-21

Status: research paper draft. This is a bounded benchmark result, not a
production-route promotion claim.

## Abstract

Number X-Ray includes a CPU-first bigint multiply campaign designed to separate
real algorithmic wins from noisy benchmark stories. In local Release
measurements over balanced decimal-digit operands, a benchmark-only Number X-Ray
multiply route produced exact products and beat GMP/MPIR `mpz_mul` at five
adjacent planned measurement points: `4096`, `5639`, `8192`, `11717`, and
`16384` decimal digits. The campaign deliberately included deterministic
in-between sizes between powers of two, same-run GMP/MPIR comparisons, paired
medians, product hash checks, stable-pair counts, and worst-pair safety
reporting.

The result is intentionally narrow. Number X-Ray has not globally outperformed
GMP/MPIR, and the measured upper window from `24103` through `65536` decimal
digits remains blocked. The contribution is the verified pocket itself and the
benchmark method around it: an app-shaped bigint engine can beat a world-class
general-purpose library where the operand shape, route policy, and measurement
gates are narrow enough, while still showing the losses that prevent promotion.

That makes this paper a proof of method as much as a speed result. The point is
not to replace GMP/MPIR everywhere. The point is to show how a specialized
application can find, verify, and preserve bounded pockets where its own bigint
route is better, then use those pockets piecemeal only when the evidence remains
clean.

## Claim

This paper makes one bounded claim:

Number X-Ray produced exact multiply results and beat GMP/MPIR `mpz_mul` by
paired median in a contiguous measured lower/transition chunk covering
`4096`, `5639`, `8192`, `11717`, and `16384` decimal digits on the recorded
local Release benchmark artifact.

This paper does not claim:

- Number X-Ray is globally faster than GMP/MPIR.
- The default production multiply route should change.
- The measured points prove every unmeasured digit length between them.
- The high-end `24103` through `65536` digit campaign is solved.

The word "contiguous" means contiguous in the benchmark plan. It is a measured
sequence of planned points, not a mathematical proof of all decimal lengths in
the interval.

## System Context

GMP and MPIR are mature general-purpose multiple-precision arithmetic
libraries. Their multiplication code is heavily tuned across operand sizes,
CPU families, and algorithmic stages. Number X-Ray has a narrower problem:
inspect, report, and compare app-shaped large integers inside a proof workbench.
That narrower workload permits routes that would be too specialized for a
general-purpose library but useful inside a bounded application policy.

The multiply campaign focuses on balanced decimal-digit operands in the range
where Number X-Ray currently needs better CPU behavior:

`4096`, `5639`, `8192`, `11717`, `16384`, `24103`, `32768`, `52163`, `65536`

The non-power-of-two sizes are deliberate. They keep the route from looking good
only at familiar binary anchors and allow the report to describe measured
chunks that could later be adopted piecemeal.

This matters because large-integer multiplication is thresholded and
shape-sensitive. GMP's own documentation describes a ladder of basecase,
Karatsuba, Toom, higher-degree Toom, and FFT algorithms selected by operand
size. A fair comparison therefore needs more than powers of two: it needs
random-looking deterministic spots between anchors, visible route labels, and
same-run comparisons so that step effects and threshold luck do not become a
false win.

## Candidate Route

The strongest audited candidate in the first full-window result was
`full-ws-combo-depth2`, reported as operation
`mul-large-toom-cmb-route-audit`. It is a benchmark-only route. It keeps
production multiply unchanged while measuring a more specialized internal
multiply shape against current production multiply and GMP/MPIR in the same
run.

The route's main ideas are:

- Use 64-bit scratch limbs for the app's large balanced operands.
- Use split views and reusable recursive workspaces to reduce copy and
  allocation overhead.
- Use a depth-limited Toom-3-style combination route above the large-multiply
  handoff.
- Keep the route diagnostic by default: `noAutoRoute=1`,
  `replacementReady=false`, parity required, and GMP/MPIR retained as oracle
  and baseline.

This is not a replacement for GMP as a general-purpose bigint engine. It is a
specialized internal route tested against the exact operand shapes the
application cares about.

## Benchmark Method

The benchmark records product correctness and timing evidence together. A row
is useful only when the product checks and route labels are visible from the
artifacts.

The key contracts are:

- Candidate, current production multiply, and GMP/MPIR are measured in the same
  run.
- Ratios use paired medians instead of unrelated best times.
- Operand families are deterministic and fingerprinted.
- Products are checked against current production multiply and GMP/MPIR.
- Worst-pair ratios and stable-pair counts remain visible.
- Benchmark-only routes stay out of production routing until a later promotion
  audit clears every gate.

The benchmark also reports `safeSizeChunks`, `longestSafeSizeChunk`, and repeat
intersections from focused runs. These fields are not mathematical interval
proofs. They are measured-point coverage summaries: a chunk such as
`4096-16384` means every adjacent planned measurement point in that sequence was
safe under the row's policy. A repeat-stable chunk means the same measured-point
intersection survived repeated focused runs. That framing is what makes
piecemeal route work possible without overstating what has been tested.

## Contiguous Measured Chunks

Number X-Ray treats contiguous safe size chunks as first-class evidence because
production route policy does not have to be all-or-nothing. A route that is safe
from `4096` through `16384` planned measurement points can be useful even if it
fails at `24103` and above, provided the implementation can enforce that window
and the route audit proves the exact policy.

The initial full-window result found a lower/transition measured chunk:

`4096 -> 5639 -> 8192 -> 11717 -> 16384`

The later repeat tooling tightened the meaning of such chunks. A single run can
suggest a pocket; repeated focused runs can reject it if the intersection
disappears. This is why the paper keeps both positive and negative chunks in
view. Piecemeal is welcome, but only measured, repeatable, route-audited
piecemeal wins should affect production behavior.

The full-window evidence artifact is:

`native-test-runs/20260619-153624-c4b04caf/benchmark.tsv`

The duplicate GMP/MPIR transition-control artifact is:

`native-test-runs/20260620-143501-c4b04caf/benchmark.tsv`

Follow-up dense-pocket and repeat-control artifacts are recorded in
`native/PERFORMANCE.md`; they are important because they show where tempting
pockets failed stricter safety checks.

## Main Result

Operation:

`mul-large-toom-cmb-route-audit`

Candidate:

`full-ws-combo-depth2`

Aggregate result:

- Exact product parity/hash: `hashSafe=162/162`
- Candidate versus current production max ratio: `0.924`
- Candidate versus GMP/MPIR max ratio: `1.298`
- Safe measured sizes versus GMP/MPIR: `5/9`
- Worst pair max ratio: `1.378`
- Decision: observe only

Per-size signal:

| Digits | Candidate / GMP | Candidate / Current | Current / GMP | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | --- |
| `4096` | `0.836` | `0.924` | `0.909` | `9/9` | GMP pocket win |
| `5639` | `0.805` | `0.882` | `0.922` | `9/9` | GMP pocket win |
| `8192` | `0.880` | `0.848` | `1.038` | `9/9` | GMP pocket win |
| `11717` | `0.929` | `0.695` | `1.324` | `9/9` | GMP pocket win |
| `16384` | `0.952` | `0.689` | `1.376` | `9/9` | GMP pocket win |
| `24103` | `1.006` | `0.642` | `1.560` | `4/9` | high-end blocker |
| `32768` | `1.114` | `0.685` | `1.639` | `0/9` | high-end blocker |
| `52163` | `1.135` | `0.618` | `1.827` | `0/9` | high-end blocker |
| `65536` | `1.298` | `0.662` | `1.947` | `0/9` | high-end blocker |

The row supports a strong but bounded interpretation. The candidate beat
GMP/MPIR at the first five planned measured sizes and beat current production
multiply at every measured size. It did not beat GMP/MPIR at the larger sizes,
and its worst-pair behavior blocks promotion.

## Duplicate GMP/MPIR Control

The transition pocket was rerun with duplicate GMP/MPIR lanes to reduce the risk
that the apparent win was simply a noisy `mpz_mul` measurement.

Operation:

`mul-large-toom-cmb-gmptrans`

Candidate:

`full-ws-combo-reuse-map-l64d2-l48d4-l48d3`

Aggregate result:

- Exact product parity/hash: `hashSafe=36/36`
- Candidate versus primary GMP/MPIR max ratio: `0.924`
- Candidate versus duplicate GMP/MPIR max ratio: `0.908`
- Duplicate GMP/MPIR control max ratio: `1.025`
- Duplicate GMP/MPIR control worst max: `1.073`
- Safe measured sizes versus GMP/MPIR: `2/2`
- Decision: observe only

Per-size signal:

| Digits | Candidate / GMP | Candidate / GMP Duplicate | Current / GMP | GMP Control | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `0.924` | `0.903` | `1.347` | `1.025` | `9/9` | duplicate-control clean |
| `16384` | `0.906` | `0.908` | `1.401` | `0.991` | `9/9` | duplicate-control clean |

This control does not prove global superiority. It does strengthen the local
transition-pocket evidence by showing that the candidate beat both the primary
and duplicate GMP/MPIR lanes while the duplicate GMP/MPIR lane stayed close to
itself.

## Why The Pocket Exists

The likely mechanism is specialization, not a general replacement of GMP's
engineering. Number X-Ray can narrow the workload in ways a general-purpose
library cannot assume:

- It measures balanced decimal-digit operands that match the app's reporting
  and proof workload.
- It can select route policies around measured decimal-size windows.
- It can reuse recursive temporaries inside benchmark-only probes.
- It can keep candidate, production, and GMP/MPIR routes in the same run and
  reject the candidate when pair-level evidence degrades.

In short, the route wins where less generic work is needed and loses where GMP's
broader multiplication machinery remains stronger.

## Why It Was Not Promoted

The same evidence that makes the pocket credible also prevents overclaiming.
Promotion requires more than median wins at selected points. The route must pass
exact parity, same-run route audits, worst-pair safety, stable-pair gates, and
the full policy window under product-like compiler settings.

The forced transition-route audit still beat GMP/MPIR at `11717` and `16384`,
but failed the nonreuse baseline, stable-pair, and worst-pair gates. Later dense
handoff-pocket repeats also failed to produce a repeat-stable safe chunk. The
result remains observe-only until a route clears the promotion bar.

This is the central methodological point: measured pocket wins are useful, and
piecemeal adoption is welcome, but only measured safe chunks can drive route
changes.

## Negative Follow-Up Evidence

The campaign did not stop after the first win. Later focused scouts asked
whether the transition pocket could be widened or whether neighboring route
families deserved a deeper audit. A two-repeat novelty matrix over the current
Toom-5 smoke rows, div2/div3 transition arithmetic, and handoff-pocket route
produced no repeat-stable safe chunks:

| Focus | Operation | Repeat-Stable Chunk | Worst Pair Max | Decision |
| --- | --- | --- | ---: | --- |
| `mul-toom5-smoke` | `mul-large-toom5-top-handoff` | none | `1.249` | reject |
| `mul-toom5-smoke` | `mul-large-toom5-top-reuse` | none | `1.469` | reject |
| `mul-toom-div-transition` | `mul-large-toom-div2-scout` | none | `1.774` | reject |
| `mul-toom-div-transition` | `mul-large-toom-div3-scout` | none | `1.320` | reject |
| `mul-toom-div-transition` | `mul-large-toom-div2-div3-scout` | none | `1.216` | reject |
| `mul-combo-handoff-pocket` | `mul-large-toom-cmb-hpocket` | none | `1.422` | reject |

Artifact:

`native-test-runs/20260621-083300-novelty-matrix-repeat2/matrix.tsv`

This negative result strengthens the paper rather than weakening it. It shows
that the method can preserve a real lower/transition win while refusing to turn
nearby noisy median wins into a route story. The next research slice should
look for a different route shape or a lower-level backend gap instead of
repeating these rejected families.

## Threats To Validity

The benchmark is local Release evidence, not a universal hardware result.
Compiler flags, `/GL` or IPO state, CPU cache behavior, GMP versus MPIR build
configuration, and operating-system noise can change ratios. The operand
families are deterministic and app-shaped; they are not a full distribution of
all possible big integers. The safe chunk is contiguous only across measured
planned points. The route is benchmark-only and has not been promoted to
production multiply.

These limits are why the paper reports raw artifact paths, upper-window losses,
duplicate-control rows, and rejected follow-up scouts alongside the wins.

## Reproducibility Notes

Readers should reproduce the result by rebuilding a fresh Release folder,
recording compiler and CPU features, then running the large multiply focus or
full benchmark ladder with TSV output. Every run should preserve the raw
`benchmark.tsv` and human-readable progress/frontier output. Paper claims should
use same-run paired ratios, not ratios copied from unrelated runs.

Useful commands in the current workbench are documented in `native/README.md`,
including focused multiply runs, progress TSV summaries, and repeated focus
checks.

A publication-ready version should add the faster-machine reproduction table:
compiler version, `/GL` or IPO state, CPU feature flags, GMP/MPIR backend
identity, raw artifact paths, and a repeat-stable chunk summary. Until that
table exists, this document remains a bounded project paper rather than a broad
external performance claim.

## Conclusion

Number X-Ray has not beaten GMP/MPIR globally. It has shown something smaller
and still valuable: a specialized, benchmark-only, app-shaped multiply route can
produce exact products and outperform GMP/MPIR in a measured lower/transition
pocket from `4096` through `16384` planned decimal-digit points. The same
campaign also shows where the route loses and why production routing must remain
unchanged until the strict gates pass.

That is the honest win: not a sweeping replacement claim, but a verified pocket
of superiority with enough reporting discipline to make the result reproducible
and useful for future piecemeal route work. If the next backend gap produces a
larger repeat-stable chunk, the same paper can grow naturally: it will add the
new chunk beside the old one instead of rewriting a local win into a global
claim.

## References

- GMP multiplication algorithms:
  https://gmplib.org/manual/Multiplication-Algorithms.html
- GMP FFT multiplication:
  https://gmplib.org/manual/FFT-Multiplication.html
- GMP source release:
  https://gmplib.org/download/gmp/gmp-6.3.0.tar.xz
