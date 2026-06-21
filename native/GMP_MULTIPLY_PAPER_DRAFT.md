# Verified Pocket Wins Against GMP/MPIR In App-Shaped Bigint Multiply

Draft status: internal paper outline. This is not a production promotion claim.

## Abstract

Number X-Ray includes a CPU-first bigint multiply campaign whose purpose is to
replace folklore-style benchmark claims with exact, reproducible, app-shaped
evidence. In Release measurements over balanced decimal-digit operands, a
benchmark-only Number X-Ray multiply route beat GMP/MPIR `mpz_mul` at five
measured sizes from `4096` through `16384` decimal digits, including
deterministic in-between sizes between powers of two. The strongest full-window
route audit showed exact parity over `162/162` product checks and
candidate/GMP ratios below `1.0` at `4096`, `5639`, `8192`, `11717`, and
`16384` digits. A later transition-window duplicate-control run independently
reconfirmed the `11717` and `16384` digit pocket against duplicate `mpz_mul`
lanes, reducing the chance that the win was only GMP timing noise.

The result is deliberately bounded. Number X-Ray has not globally outperformed
GMP/MPIR, and the upper measured sizes from `24103` through `65536` still block
promotion. The contribution is narrower and stronger: app-shaped internal
routes can beat a world-class general-purpose library in verified operand-size
pockets, and the harness can identify those pockets without hiding losses,
oracle mismatches, or noisy pairs.

## Claim Shape

The paper should make this claim:

> On a local Release CPU benchmark, Number X-Ray produced exact multiply results
> and beat GMP/MPIR `mpz_mul` in a contiguous measured lower/transition chunk
> covering `4096`, `5639`, `8192`, `11717`, and `16384` decimal digits.

The paper should not make these claims yet:

- Number X-Ray is globally faster than GMP/MPIR.
- The production multiply route has replaced GMP/MPIR or should be promoted.
- The measured chunk proves every unmeasured digit count between endpoints.
- The high-end window from `24103` through `65536` is solved.

## Why This Is Interesting

GMP is an aggressively tuned general-purpose arithmetic library. Beating it
even in bounded pockets is useful because Number X-Ray can specialize for its
own operand shapes, reporting needs, and route gates. The campaign also shows a
method for making small wins useful without turning them into broad claims:
measure powers of two plus deterministic in-between sizes, require exact product
parity, compare routes in the same run, keep worst-pair safety visible, and keep
production routing unchanged until the full policy window passes.

## Benchmark Method

The campaign measures decimal-digit multiply sizes:

`4096`, `5639`, `8192`, `11717`, `16384`, `24103`, `32768`, `52163`, `65536`

The non-power-of-two sizes are intentional. They prevent the route from looking
good only at familiar binary anchors and let the report track contiguous measured
chunks for piecemeal adoption work.

Core rules:

- Compare candidate, current production multiply, and GMP/MPIR in the same run.
- Use paired medians rather than independent best times.
- Preserve operand fingerprints through deterministic operand families.
- Require product equality against current production and GMP/MPIR.
- Record worst-pair ratios and stable-pair counts.
- Keep benchmark-only rows `noAutoRoute=1` and `replacementReady=false` until a
  later promotion audit clears every gate.

## Main Evidence: Full Route Audit

Recorded artifact path:

`native-test-runs/20260619-153624-c4b04caf/benchmark.tsv`

Operation:

`mul-large-toom-cmb-route-audit`

Candidate:

`full-ws-combo-depth2`

Aggregate:

- Exact product parity/hash: `hashSafe=162/162`
- Candidate versus current production max ratio: `0.924`
- Candidate versus GMP/MPIR max ratio: `1.298`
- Safe measured sizes versus GMP/MPIR: `5/9`
- Worst pair max ratio: `1.378`
- Decision: observe only

Per-size signal:

| Digits | Candidate / GMP | Candidate / Current | Current / GMP | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | --- |
| `4096` | `0.836` | `0.924` | `0.909` | `9/9` | GMP win |
| `5639` | `0.805` | `0.882` | `0.922` | `9/9` | GMP win |
| `8192` | `0.880` | `0.848` | `1.038` | `9/9` | GMP win |
| `11717` | `0.929` | `0.695` | `1.324` | `9/9` | GMP win |
| `16384` | `0.952` | `0.689` | `1.376` | `9/9` | GMP win |
| `24103` | `1.006` | `0.642` | `1.560` | `4/9` | high-end blocker |
| `32768` | `1.114` | `0.685` | `1.639` | `0/9` | high-end blocker |
| `52163` | `1.135` | `0.618` | `1.827` | `0/9` | high-end blocker |
| `65536` | `1.298` | `0.662` | `1.947` | `0/9` | high-end blocker |

Read this as a lower/transition measured chunk, not a global replacement. The
candidate is faster than GMP/MPIR by paired median at the first five measured
sizes, and it is faster than current production multiply at every measured size,
but it loses to GMP/MPIR in the upper half of the campaign.

## Control Evidence: Transition GMP Duplicate

Recorded artifact path:

`native-test-runs/20260620-143501-c4b04caf/benchmark.tsv`

Operation:

`mul-large-toom-cmb-gmptrans`

Candidate:

`full-ws-combo-reuse-map-l64d2-l48d4-l48d3`

Aggregate:

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

This is the paper's noise-control story. The transition pocket still beats the
primary and duplicate GMP/MPIR lanes, while the duplicate GMP/MPIR lane remains
stable against itself.

## Promotion Blocker: Forced Route Audit

Recorded artifact path:

`native-test-runs/20260620-162931-c4b04caf/benchmark.tsv`

Operation:

`mul-large-toom-cmb-troute`

Aggregate:

- Exact product parity/hash: `hashSafe=36/36`
- Candidate versus current production max ratio: `0.660`
- Candidate versus GMP/MPIR max ratio: `0.971`
- Candidate versus nonreuse baseline max ratio: `1.041`
- Worst pair max ratio: `1.642`
- Safe measured sizes under the full strict gate: `0/2`
- Decision: observe only

This protects the paper from overclaiming. The transition route still beats GMP
at the measured sizes, but it fails the nonreuse baseline, stable-pair, and
worst-pair gates required for a default route change.

## Follow-Up: Dense Handoff Pocket Scout

Recorded artifact path:

`native-test-runs/20260621-065905-handoff-pocket/benchmark.tsv`

Operation:

`mul-large-toom-cmb-hpocket`

Aggregate:

- Exact product parity/hash: `hashSafe=126/126`
- Candidate versus current production max ratio: `0.771`
- Candidate versus GMP/MPIR max ratio: `1.015`
- Safe measured sizes under the strict gate: `0/7`
- Contiguous safe measured chunks: `none`
- Worst pair max ratio: `1.531`
- Decision: observe only

This follow-up is important because it tests the tempting transition handoff
more densely at `10007`, `10733`, `11717`, `12553`, `13649`, `14831`, and
`16384` digits. The route remains exact and faster than current production
multiply at every measured point, but it does not clear the GMP-facing,
worst-pair, or contiguous-safety gates. It should be cited as a guardrail:
piecemeal adoption is welcome, but only measured safe chunks count.

## Contiguous Safe Size Reporting

The paper should report contiguous measured chunks as evidence units. In the
full route audit, the GMP-facing win chunk is:

`4096 -> 5639 -> 8192 -> 11717 -> 16384`

That means five adjacent measured points in the benchmark plan, not a formal
proof that every digit count from `4096` through `16384` is safe. The language
matters because piecemeal adoption is useful only when the measured coverage is
visible.

## Proposed Paper Structure

1. Introduction
   Explain why beating GMP in any honest pocket matters, and why broad claims
   are risky for bigint benchmarks.

2. System Context
   Describe Number X-Ray's app-shaped multiply workload, decimal-digit windows,
   operand families, and the use of GMP/MPIR as oracle plus baseline.

3. Benchmark Design
   Cover paired medians, rotating same-run measurement, deterministic random
   spots, product hashes, stable-pair gates, worst-pair gates, and
   artifact-visible rows.

4. Candidate Routes
   Describe split views, reusable workspaces, recursive Toom-3 combo routes, and
   why routes remain benchmark-only until promoted.

5. Results
   Lead with the `4096` through `16384` measured pocket, then show the upper
   blockers. Include the duplicate GMP control table.

6. Discussion
   Explain why specialization can win in bounded app-shaped pockets, why the
   high-end gap remains, and how contiguous measured chunks allow piecemeal
   follow-up audits.

7. Threats To Validity
   Include CPU-specific tuning, compiler flags, GMP versus MPIR differences,
   benchmark noise, deterministic operand family limits, and the lack of
   production promotion.

8. Future Work
   Broaden handoff structure, test deeper multiplication families, rerun on the
   faster CPU, and promote only if exact parity, stable pairs, worst pairs, and
   same-run route gates all pass.

## Working Title Options

- Beating GMP In Pockets: Verified App-Shaped Bigint Multiply For Number X-Ray
- Small Honest Wins Against GMP: A Paired-Median Bigint Multiply Study
- App-Shaped Bigint Multiply Can Beat GMP, But Only Where The Evidence Says So

## Publication Readiness Checklist

- Reproduce the key rows on the faster CPU.
- Record compiler, `/GL` or IPO state, CPU features, and raw artifact paths.
- Add at least one more transition self-control or rerun to confirm the
  `11717` and `16384` pocket.
- Keep deterministic in-between sizes in the paper tables.
- Report upper-window losses with the same visibility as lower-window wins.
- Avoid the phrase "faster than GMP" unless the exact size window and route are
  named.

## References To Carry Into The Paper

- GMP multiplication algorithms:
  https://gmplib.org/manual/Multiplication-Algorithms.html
- GMP FFT multiplication:
  https://gmplib.org/manual/FFT-Multiplication.html
- GMP source release:
  https://gmplib.org/download/gmp/gmp-6.3.0.tar.xz
