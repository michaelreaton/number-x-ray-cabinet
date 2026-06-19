# Native Number X-Ray Workbench

The native workbench is a cross-platform C + GTK proof instrument that sits beside the existing browser demo. It uses GMP-compatible arbitrary precision arithmetic (`gmp.h` on Linux/macOS, MPIR via vcpkg on Windows) and treats every result as one of:

- `solved`: exact factors found and product verification passed
- `partial`: at least one factor found, with unresolved cofactors remaining
- `timeout` / `cancelled`: local budget ended before completion
- `unsolved`: no exact local factor proof found

Large challenge fixtures are intentionally not claimed solved. The native app may profile and attempt bounded methods, but it must keep a challenge number unresolved unless exact factors are found and the factor product equals the input.

For the next high-end CPU benchmark pass, see
[`FASTER_MACHINE_HANDOFF.md`](FASTER_MACHINE_HANDOFF.md). It records the latest
merged bigint evidence, exact validation commands, and promotion rules for
route changes.

## Build

Windows with vcpkg MPIR:

```powershell
cmake -S native -B native/build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
```

Windows with the GTK workbench enabled:

```powershell
C:\vcpkg\vcpkg.exe install gtk:x64-windows
cmake -S native -B native/build-gtk -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DXRAY_BUILD_GTK=ON
cmake --build native/build-gtk --config Release
ctest --test-dir native/build-gtk -C Release --output-on-failure
native\build-gtk\Release\xray_workbench.exe
```

On Windows, CMake also auto-detects the common vcpkg install prefix
`C:/vcpkg/installed/x64-windows`, its bundled `pkgconf.exe`, and `gtk4.pc`.
That keeps GUI target discovery working for local build folders that were not
configured with the vcpkg toolchain:

```powershell
cmake -S native -B native/build-gtk-autodetect -DXRAY_BUILD_GTK=ON
cmake --build native/build-gtk-autodetect --config Release --target xray_workbench
native\build-gtk-autodetect\Release\xray_workbench.exe
```

Use `-DXRAY_GTK_ROOT=C:/path/to/gtk-prefix` when GTK4 is installed somewhere
other than vcpkg. Windows GTK builds copy `*.dll` files from
`<XRAY_GTK_ROOT>/bin` beside `xray_workbench.exe` by default so the executable
can be launched directly from the build folder; set
`-DXRAY_GTK_COPY_RUNTIME_DLLS=OFF` when another packaging step owns runtime DLLs.

Linux/macOS with GMP and GTK4 development packages:

```bash
cmake -S native -B native/build
cmake --build native/build
ctest --test-dir native/build --output-on-failure
```

If GTK4 development headers are unavailable, CMake skips the `xray_workbench` GUI target and still builds the reusable core, CLI, and tests.

## Importing The Number Library

Other C/CMake tools can use the native number core directly. From a monorepo or
checkout-based integration, add the native project and link the namespaced
target:

```cmake
add_subdirectory(path/to/number-x-ray-cabinet/native)
target_link_libraries(my_tool PRIVATE NumberXRay::core)
```

`NumberXRay::core` is the static library target. Builds enable the shared
library by default as `NumberXRay::core_shared`; disable it with
`-DXRAY_BUILD_SHARED=OFF` when a static-only SDK is desired.

For an installed SDK-style integration, install the native package first:

```powershell
cmake --install native/build --config Release --prefix C:/number-xray-sdk
```

Then consume it from another CMake project:

```cmake
find_package(NumberXRay CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE NumberXRay::core)
# Or, when the SDK was installed with XRAY_BUILD_SHARED=ON:
target_link_libraries(my_tool PRIVATE NumberXRay::core_shared)
```

Include `number_xray.h` for the public API. The older `xray_workbench.h`
header remains installed, but `number_xray.h` is the stable tool-facing entry
point.

Every exported `XRAY_API` function is documented directly in the installed
headers with Doxygen-style comments. Use `number_xray.h` as the public entry
header and `xray_workbench.h` as the full function reference. Returned `char *`
values are owned by the caller and must be released with `xray_free()` unless
the function documentation says the pointer is borrowed. Report structs that
receive heap fields have matching `*_clear()` functions, and those clearers are
part of the documented API contract.

Bindings and plugin-style loaders should check the runtime library they loaded:
`xray_version()` returns the borrowed version string, and `xray_abi_version()`
returns the C ABI version. The stable wrapper header also exposes
`NUMBER_XRAY_VERSION` and `NUMBER_XRAY_ABI_VERSION` for compile-time checks.

Tools that only need basic non-negative integer arithmetic can start with the
decimal-string convenience helpers:

```c
char *sum = xray_bigint_add_decimal("10,000_000", "1");
if (sum) {
  puts(sum);
  xray_free(sum);
}
```

These helpers parse the same messy decimal separators as the struct-based
scratch bigint API and return newly allocated decimal strings, which makes them
easier to import from FFI layers before a binding owns the full struct lifecycle.

Tools that repeatedly divide by the same large divisor can use
`XrayBigIntDivisorContext` and `XrayBigIntDivisionWorkspace` to make that reuse
explicit:

```c
XrayBigIntDivisorContext divisor_context;
XrayBigIntDivisionWorkspace division_workspace;
xray_bigint_divisor_context_init(&divisor_context);
xray_bigint_division_workspace_init(&division_workspace);

if (xray_bigint_divisor_context_set(&divisor_context, &divisor)) {
  xray_bigint_divmod_precomputed_workspace(
    &quotient,
    &remainder,
    &numerator,
    &divisor_context,
    &division_workspace);
}

xray_bigint_division_workspace_clear(&division_workspace);
xray_bigint_divisor_context_clear(&divisor_context);
```

This is an explicit importer API, not an automatic production route. Benchmark
rows with `noAutoRoute=1` remain `observe-only` even when a local run finds an
interesting median.

Report-producing tools can also start with one-shot JSON helpers before they
model the C report structs:

```c
char *json = xray_factor_solve_json("10_403");
if (json) {
  puts(json);
  xray_free(json);
}
```

`xray_factor_solve_json()` and `xray_cyclotomic_scan_json()` use the default
bounded configs and return self-contained reports. `xray_workbench_run_json()`
runs the standard proof stages with the benchmark ladder disabled so scripts do
not accidentally trigger a long local benchmark; use `xray_workbench_run()` with
a custom `XrayRunConfig` when benchmark evidence is part of the run.

Shared-library installs also include a zero-dependency Python ctypes helper for
tools that want to call the C ABI before writing a full binding:

```python
from number_xray_ctypes import load

xray = load("C:/number-xray-sdk")
print(xray.backend_info())
print(xray.bigint_route_config())
print(xray.bigint_route_summary())
print(xray.add_decimal("10,403", "1"))
print(xray.factor_solve("10_403")["status"])
```

The helper lives under `<prefix>/share/number-xray/python/`; add that directory
to `PYTHONPATH` before importing it. It reads the SDK manifest to locate the
shared library. On Windows, set
`NUMBER_XRAY_EXTRA_DLL_DIRS` when the GMP/MPIR runtime DLL lives outside the SDK
`bin` directory.

For speed comparisons, GMP and MPIR are treated as compatible oracle backends,
not as identical performance baselines. Windows vcpkg builds usually benchmark
against MPIR, while Linux/macOS builds usually benchmark against GMP. Benchmark
JSON records `baselineBackend`, `baselineBackendVersion`, and
`baselineBackendLibrary`, and human-readable frontier text prints the same
backend identity so local speed claims are not over-generalized. Consumers can
also call `xray_bignum_backend_name()`, `xray_bignum_backend_version()`, and
`xray_bignum_backend_library()` to label their own benchmark reports. Call
`xray_bigint_route_config()` or the Python helper's `bigint_route_config()` for
the ABI-stable route struct. Call `xray_bigint_route_config_json()` or the
Python helper's `bigint_route_summary()` when a tool needs the full
machine-readable route map, including decimal conversion thresholds, sparse
arithmetic gates, diagnostic probe families, and MPIR/GMP formatter clues. That
JSON summary is also embedded in benchmark and workbench reports as
`scratchRouteConfig`, so archived runs carry the exact local route context
beside their timing evidence.

CTest enforces that documentation contract with `xray_api_doc_coverage`, which
fails the build if a new exported `XRAY_API` function is added without a
preceding Doxygen block.

Non-CMake tools can discover compiler and linker flags from the installed
pkg-config files on platforms that use pkg-config. `number-xray` links the
static core library:

```bash
cc my_tool.c $(pkg-config --cflags --libs number-xray)
```

When the SDK was installed with `XRAY_BUILD_SHARED=ON`, `number-xray-shared`
links the shared library for plugin loaders, FFI bindings, and tools that want
the runtime ABI probes from a DLL/shared object:

```bash
cc my_tool.c $(pkg-config --cflags --libs number-xray-shared)
```

Tools that need package metadata without running CMake or pkg-config can read
the installed SDK manifest:

```text
<prefix>/share/number-xray/number-xray-sdk.json
<prefix>/share/number-xray/number-xray-api.json
<prefix>/share/number-xray/number-xray-api.md
<prefix>/share/number-xray/python/number_xray_ctypes.py
```

It records the public header, library name, CMake package/target, pkg-config
names for installed static/shared libraries, documented header locations,
ABI/runtime/backend probe functions, install-relative include/lib/bin
directories, and GMP/MPIR dependency. The generated API catalog lists every
exported function name, declaration, category, return type, parameter list,
short summary, full cleaned Doxygen text, and ownership hint. Binding
generators should read `apiDocumentation.catalog` first, then use
`apiDocumentation.referenceMarkdown` for a generated human reference, or
`apiDocumentation.functionReferenceHeader` when they need the original comments
and struct definitions. Shared-library installs also advertise the Python
ctypes helper in `languageBindings.pythonCtypes`.

Consumers still need GMP or MPIR available at configure/build time; the CMake
package recreates the `GMP::GMP` dependency target from `GMP_ROOT`, vcpkg, or
system paths, and the pkg-config metadata records `-lgmp` for static/private
linking.

The app includes Fermat F12, `2^4096 + 1 = Phi_8192(2)`, as a large exact cyclotomic example. It is meant to test discovery, reporting, and UI handling for much larger structured numbers; it must not be reported as factored unless exact factors are found and product verification passes.

## GMP Parity And Performance Bar

GMP/MPIR is the temporary oracle while the from-scratch integer core grows up. Replacing any GMP-backed path requires two gates:

- exact oracle parity against GMP/MPIR for serialization, arithmetic, modular arithmetic, roots, and factor accounting
- a benchmark record showing the native path matches or outperforms the GMP/MPIR baseline for the operation and operand size being replaced

Until both gates pass, reports and documentation must label the scratch bigint layer as a migration foothold, not as the production solver engine.

Benchmark JSON exposes that rule per operation-size row:

- `parityVerified`: scratch output exactly matched the GMP/MPIR oracle
- `replacementReady`: parity passed, scratch paired-median timing was less than or equal to GMP/MPIR, and at least 4 of 5 same-run paired samples also favored scratch
- `scratchUs` and `gmpUs`: median local timings from the current machine
- `speedRatio`: median of paired scratch/GMP sample ratios, not a cherry-picked independent best-time division; a ratio below `1.0` means scratch was faster for that row
- `stableSampleCount`, `sampleCount`, and `worstPairRatio`: the stability guard that keeps a noisy benchmark row from being promoted on one lucky median
- `measurableStatus.betterNow[]`: the fastest exact-parity scratch rows that are currently allowed to replace the backend, with `speedup`, `speedRatio`, `worstPairRatio`, and stability counts
- `measurableStatus.stillWorking[]`: the largest exact-parity scratch gaps that are not route-ready, with `slowdown`, `speedRatio`, `worstPairRatio`, and stability counts

Rows with parity but without `replacementReady` are useful migration evidence, but the production path must remain GMP-backed.

Threshold and root-size policy scouts have an additional protection: a single
row cannot become route-ready. Any policy row with a nonzero size gate,
threshold, or depth limit must report
`thresholdSafety=requires-forced-neighbor`, `noAutoRoute=1`,
`replacementReady=false`, and `adoption=observe-only` until a dedicated
forced-neighbor safety row and a product-like `/GL` run both pass. This keeps a
one-pocket win from becoming a harmful global default.

Every benchmark run writes five benchmark artifacts in its run folder:

- `benchmark.json`: machine-readable rows, CPU features, timings, gates, and status labels
- `benchmark.tsv`: spreadsheet-friendly rows for sorting and comparison
- `benchmark_frontier.txt`: human-readable CPU feature summary, measurable status, near wins, largest scratch gaps, and scratch/kernel timing tables
- `benchmark_progress.txt`: human-readable completed/open/noisy digest for status updates and PR review
- `benchmark_progress.tsv`: machine-readable progress lanes and booleans for external tools

Kernel rows in `benchmark_frontier.txt` include compact route tags such as
`thr=`, `leaf=`, `depth=`, and `base=` when those values are present in the
underlying benchmark detail. The full JSON/TSV artifacts remain the canonical
machine-readable record.

The `MEASURABLE STATUS` section is the quick progress readout: `Better now`
lists the fastest scratch rows that are currently allowed to replace the
backend on this machine, and `Still working` lists the largest exact-parity
scratch gaps with ratio, worst-pair, and stable-sample evidence.

`benchmark_progress.txt` and the matching `--bench-progress` CLI mode turn a
TSV artifact into the after-PR status view: completed product/backend route
candidates, open/noisy route rows, product-gated evidence, safety rejections,
setup/warmup context, warmup-review rows, lower-bound/incomplete rows,
run-failed rows, baseline/current rows, and controls excluded from candidate totals. This
mirrors the MFastFermat progress-digest lesson: duplicate controls,
`noisy-control` rows, current-default baseline rows, and rows tagged with
`noAutoRoute=1`, forced-neighbor safety, or required deep confirmation stay out
of route-completed progress even when their median ratio looks favorable.
The matching `benchmark_progress.tsv` artifact and `--bench-progress-tsv` CLI
mode expose the same row classification with importable booleans such as
`routeCandidate`, `routeCompleted`, `productGated`, `hasSetupContext`,
`warmupReview`, `lowerBound`, `runFailed`, `baseline`, `control`, and
`noisyControl`.
It also includes `setupSeconds`, a measured setup/warmup duration in seconds
when tags such as `setupUs`, `setupMs`, `warmup_s`, `WarmupSecondsMedian`, or
`HelperWarmupSeconds` are present. An explicit `SetupSeconds` or
`setupSeconds` tag is trusted as-is; otherwise the exporter reports the largest
measured setup/warmup tag it finds. A row can have `hasSetupContext=true` with
`setupSeconds=0` when the row carries setup metadata but no positive measured
setup cost of its own.
The TSV also includes `attemptedRuns` and `completedRuns` when row details
provide `Runs=` and `CompletedRuns=` tags. Missing tags are reported as `0`
rather than guessed from benchmark sample counts.
Rows tagged with ordinary setup metadata such as
`setupPolicy=reported-not-scored`, `setupUs=`, `warmup_s=`, or
`WarmupSecondsMedian=` are listed in `setupContextRows`. They can still appear
in the normal completed/open/product-gated lanes because setup context is an
audit attribute, not a route verdict.
Rows tagged with `WarmupPolicy=review-warmup`, `warmupPolicy=review-warmup`, or
`setupPolicy=review-warmup` are listed in their own warmup-review lane and stay
out of route candidate, route-completed, and route-open totals. Ordinary
`setupPolicy=reported-not-scored` rows remain visible setup context, but
review-warmup means the setup cost is large enough to require human review
before any route claim.
Rows tagged with `timeout`, `lower-bound`, `no-complete-run`, `incomplete`, or
`CompletedRuns=0` are listed in their own lower-bound lane and also stay out of
route candidate, route-completed, and route-open totals. They are still visible
because a timed-out large probe can be useful evidence, but it is not a win.
Rows tagged with `run failed`, `run-failed`, or `RunFailed=true` are listed in
their own run-failed lane, even when no timing ratio was recorded. This keeps a
bad helper/process run visible without confusing it with a timeout lower bound
or ordinary open/noisy evidence. A row explicitly marked as run-failed takes
precedence over a generic `CompletedRuns=0` tag, so process failures and
timeout/incomplete lower bounds stay separable.

The scratch-vs-GMP ladder currently measures 40, 150, 1000, 4096, and 8192 decimal digit operands so local changes have to keep scaling beyond tiny examples before they earn adoption labels. Parse and format rows are tracked separately because decimal ingestion and decimal serialization have very different bottlenecks. Multiplication and specialized square rows also have a 16384 digit discovery tier so larger-number arithmetic work can be observed before it is considered for routing. Multiplication rows aggregate two deterministic operand families because threshold-sensitive multiply code can look good on one number shape and lose on another. The tournament rows intentionally test several parse chunk sizes, decimal formatting handoff thresholds, multiply leaf thresholds, square thresholds, and Toom handoff candidates in one run; those rows are evidence-only until a bounded window wins with exact parity and stable same-run paired ratios. Decimal formatter route changes also need `format-dc-route` same-run evidence so a direct-output D&C pocket win cannot be mistaken for a global or root-size threshold; that route row uses chunked interleaved timing and alternates which side runs first to reduce scheduler and cache-warmth bias. The matching `format-dc-route-safety` row reruns the direct16-versus-ladder8 route over 4096, 8192, and 16384 digits with 9 paired samples, hash-verifies candidate/baseline/GMP output strings, and keeps `noAutoRoute=1`; it is a deep confirmation artifact, not a production formatter switch.
The formatter route tournament also emits `format-route-tournament-detail`
control rows for every tested contender on the same input and in the same run.
Those rows expose each route's current-format and GMP ratios for external
review tools, while `controlSafety=tournament-detail` keeps them out of
route-completed progress totals.
The matching `benchmark_progress.tsv` file appends import columns such as
`digitBand`, `workloadShape`, `policy`, `candidate`, `featureGate`, `gmpClue`,
and safety gates so external optimizer tools can group rows without scraping
free-form detail text.

Formatter policy probes also include explicit endpoint tournaments for the
GMP-inspired base-`1e19` preinverse route. The `preinv10e19-window*` and
`preinv10e19-pairs-window*` rows measure candidate-only endpoints such as
768/896, 768/960, and 896/1000 digits, then emit matching
`format-policy-safety` gates. These rows exist to prove or reject a
root-size-gated decimal serialization pocket before it can influence the
production formatter; they remain `observe-only` unless both endpoint timing
and worst-pair safety pass.
When a five-sample endpoint gate looks clean, matching
`format-policy-deep-safety` rows rerun that exact window with 9 paired samples
and require 8 stable wins before the route is treated as promotion evidence.
The deep audit covers the current promotion-candidate `1e19` windows as well
as nearby pair-writer windows that looked tempting but noisy. These stricter
rows are benchmark evidence, not a production route change, until repeat runs
show the same exact window clearing the deep gate and the production-shaped
scratch rows at the window endpoints.

The D&C formatter tournament also tests static power-table precompute with
leaf sizes `8`, `16`, `32`, and `64` for both recursive string assembly and
direct-buffer output. These rows are default-off probes; they exist to measure
whether a larger static leaf changes the 8k/16k formatter gap before any route
policy can be considered.

Division precompute rows also report one-time setup separately from scored
work. `divmod-precomputed`, `divmod-workspace`, and `divmod-preinv-qhat` rows
include `setupUs`, `setupSamples`, `setupIterations`,
`setupPolicy=reported-not-scored`, and `cacheRole=divisor-context` so
cached-context wins cannot hide the cost of building that context.

Add/sub benchmark rows use a higher iteration floor at 1000+ digits because those operations are fast enough that sub-millisecond timing windows can flip adoption labels from scheduler noise rather than real algorithm behavior.

On MSVC x64 builds, the benchmark also emits evidence-only `mul-toom3-unroll4-*` rows. These combine the one-level Toom-3 split with the bounded `_umul128`/`_addcarry_u64` unroll4 leaf schedule so larger-number work can tell whether GMP's advantage is coming from algorithm selection, leaf scheduling, or both. Extra GMP-facing handoff scout rows test leaf thresholds near GMP's tuned Toom region (`24`, `48`, `96`) without adding production routes. The `mul-toom3-u4-rec-vs-gmp` rows test a depth-limited recursive Toom-3 variant at the 16384 digit frontier. Deep rows such as `mul-toom3-unroll4-deep-vs-gmp` and `mul-toom3-u4-rec-deep-vs-gmp` rerun selected promising GMP comparisons with 9 paired samples, including the recursive leaf `64` and `96` gates, so a noisy near-win cannot influence routing. They do not widen the production route unless parity and the same-run stability gate both pass.

The benchmark also emits `kernel-probe` rows up through 8192-digit multiply threshold cases and bounded one-level Toom-3 multiply probes. These are not production replacements; they are local proof drills for GMP-inspired primitive choices. A probe row compares a candidate carry-chain kernel, multi-operand Karatsuba threshold, CPU-specific multiply-add primitive, square basecase, or Toom-3 stage to a baseline using the same-run paired median ratio, records the CPU/compiler feature gate, and labels the candidate `promote-candidate`, `observe-only`, or `blocked-output-mismatch`. A promotion candidate must also win enough paired samples under the probe gate: ordinary five-sample rows require 4 wins, while deep nine-sample rows require 8 wins. On MSVC x64 machines, `muladd-unroll4` and `muladd-unroll8` rows compare unrolled `_umul128`/`_addcarry_u64` multiply-accumulate loops with the scalar loop; when BMI2 and ADX are present, `muladd-bmi2-adx` rows compare `_mulx_u64`/`_addcarryx_u64` against the same scalar baseline. Full-shape probes name their baseline explicitly: `square-vs-mul` compares specialized square against the forced generic threshold self-multiply path, `square-karatsuba-vs-mul` compares recursive Karatsuba square against the same generic self-multiply baseline, `square-karatsuba-vs-gmp` compares that candidate directly against `mpz_mul` across multiple handoff thresholds, `mul-unroll4-vs-scratch` compares the route candidate against the scalar threshold multiply path, `mul-toom3-vs-scratch` compares Toom-3 against the current scratch multiply path, and GMP-gate rows such as `mul-unroll4-vs-gmp` compare the candidate directly against `mpz_mul`. The `mul-large-cpu-campaign` rows compare current production multiply, split-view Karatsuba, split-view plus reusable workspace, and `mpz_mul` from 4096 through 65536 digits, including deterministic in-between sizes between the power-of-two anchors; on MSVC x64, matching `mul-large-cpu-toom-branch` rows time the recursive Toom-3 plus unroll4 branch against current, workspace, and `mpz_mul` in the same window, `mul-large-cpu-toom-view-branch` rows compare the copied Toom branch with read-only Toom split views, `mul-large-cpu-toom-ws-branch` rows compare the split-view Toom branch with reusable per-depth Toom temporaries, `mul-large-cpu-toom-full-ws` rows reuse Karatsuba temporaries on the Toom handoff path too, and `mul-large-cpu-toom-full-audit` rows compare that full-workspace route directly against current production multiply. These rows are `noAutoRoute=1` until a later forced route audit passes. The `mul-unroll4-deep-vs-gmp` rows rerun the high-risk 4096/8192 digit GMP gate with 9 paired samples so noisy CPU-feature claims have to survive a stricter local check before they can influence routing. Both gates must pass across the target sizes before an internal route can be considered for adoption. This is the path for AVX/ADX/BMI2 work and algorithm-stage tuning: no primitive moves into the scratch bigint layer until the GUI benchmark can show exact parity and a stable local speed win.

Measured keep/drop decisions are recorded in [`PERFORMANCE.md`](PERFORMANCE.md) so rejected scouts do not get repeated as if they were still unexplored.

## GMP Source Clues

GMP is used as an oracle and as a design map, not as copied implementation. Notes from the official GMP 6.3.0 source and manual:

- x86_64 GMP uses 64-bit limbs (`GMP_LIMB_BITS 64`), and the scratch layer now uses 64-bit limbs too.
- GMP's `mpn_mul_basecase` starts with `mul_1` and accumulates with `addmul_1`/multi-limb variants, avoiding an initial zeroing loop.
- GMP keeps CPU-family-specific `mpn/x86_64` kernels and tuned thresholds for Broadwell/Skylake-class chips.
- GMP's tuned Skylake/Broadwell thresholds move from basecase multiplication to Toom around 26 limbs, then to larger Toom/FFT stages later.
- AVX/ADX/BMI2 ideas must be proven on this laptop with local parity tests and paired benchmark ratios before adoption.

Current native probes test that first clue directly by comparing exact 64-bit limb add/sub carry chains against exact 32-bit limb carry chains for the same bit widths. On MSVC/x86 builds they also test `_addcarry_u32` and `_subborrow_u32` against the scalar 32-bit baseline, because previous carry-intrinsic experiments were noisy and must remain evidence-only until they win reproducibly.

The 64-bit scratch migration is intentionally gated per operation. Add/sub now use a 64-bit carry/borrow helper with MSVC x64 intrinsics where available and portable C elsewhere; subtraction also copies unchanged high limbs after the borrow has cleared, but only the benchmark may mark those rows replacement-ready. The production decimal parser keeps the 19-digit chunk route below 2048 decimal digits and switches to the locally proven 15-digit chunk route for 2048+ digit inputs; `xray_bigint_set_decimal_chunk_probe` still exposes 1-to-19 digit diagnostic chunking for local tournaments and importing tools. `mul` can become replacement-ready where the benchmark proves it; large balanced operands now take a bounded Karatsuba path with a difference-form middle product and explicit GMP-oracle sign tests, and MSVC x64 uses an unroll4 multiply leaf only inside the measured safe window (`8 <= min limbs`, `max limbs <= 512`, and not strongly unbalanced). Pointer-identical self-multiply calls such as `xray_bigint_mul(out, a, a)` route through the production square dispatcher, which keeps exact product semantics while using the already-gated square engine. `square` uses self-multiply for tiny operands (`<= 8` limbs), where the local paired benchmark showed the specialized square loop losing, then routes through the Karatsuba square dispatcher for larger operands. Separate Toom-3 probes test the next algorithmic stage without changing production routing. Any multiply or square row that fails the local speed gate stays `oracle-only` or `observe-only`. Decimal formatting emits existing 9 digit chunks with a small manual writer, a formatter-private base-1e9 in-place division loop, conservative chunk pre-reservation, and a larger-limb Horner converter for binary-limb-to-decimal chunk generation. The production formatter now uses the two-digit pair-table text writer only in bounded limb ranges where local paired benchmarks found a stable win (`<= 8` limbs, or the Horner band from `48` through `54` limbs), and it keeps the large decimal divide-and-conquer route on the ladder formatter at leaf `8`; direct-output and preinv-qhat D&C remain required benchmark probes after Release evidence showed neighbor-regression and product-gated rows. The legacy Horner-threshold probe stays available so future benchmark rows can still compare candidate and baseline honestly. `mod-u32`/`divmod-u32`/`powmod-u32` use reciprocal multiply-high division with exact correction to avoid slow hardware division in the hot path. `gcd-u32` reduces through that modulo path, then uses a division-free binary GCD tail; the 65537 case uses an exact Fermat-prime limb fold because `2^64 == 1 (mod 65537)`. Rows that do not beat GMP remain evidence only; evidence is not permission to route production solver accounting away from GMP.

Division research now includes evidence-only qhat probes. The 32-bit-limb
Knuth estimator lost to direct `_udiv128`, while the normalized pre-inverted
top-limb estimator won the qhat micro-kernel under Release and `/GL`. That row
is still `noAutoRoute=1`. A full `divmod-preinv-qhat` quotient/remainder probe
now tests the same estimator inside the normalized division loop against the
real context+workspace path, with exact `mpz_tdiv_qr` parity. It remains an
explicit diagnostic/importer probe only until product-like `/GL` rows and
neighboring sizes show stable wins without worst-pair regressions. The
`divmod-preinv-qhat-safety` policy-gate row aggregates the measured 4096,
8192, and 16384 digit rows so a tempting large-size pocket win cannot become a
global or root-size threshold unless the neighboring window is clean.
Multiply policy scouts use the same discipline: `mul-policy-safety` forces the
Toom-3+unroll4 candidate at adjacent sizes and keeps the policy
`observe-only`/`noAutoRoute=1` when Release or `/GL` shows a regression.

Primary references: [GMP multiplication algorithms](https://gmplib.org/manual/Multiplication-Algorithms.html), [GMP FFT multiplication](https://gmplib.org/manual/FFT-Multiplication.html), [GMP single-limb division](https://gmplib.org/manual/Single-Limb-Division.html), and the official GMP 6.3.0 source tarball from [gmplib.org](https://gmplib.org/download/gmp/gmp-6.3.0.tar.xz).

## CLI

```powershell
native\build\Release\xray_cli.exe --help
native\build\Release\xray_cli.exe --bench 10403
native\build\Release\xray_cli.exe --bench-frontier 10403
native\build\Release\xray_cli.exe --bench-progress native\build\native-test-runs\<run>\benchmark.tsv
native\build\Release\xray_cli.exe --bench-min-digits 768 --bench-max-digits 1000 --bench-progress native\build\native-test-runs\<run>\benchmark.tsv
native\build\Release\xray_cli.exe --bench-filter mul --bench-compare native\build-release\native-test-runs\<run>\benchmark.tsv native\build-ltcg\native-test-runs\<run>\benchmark.tsv
native\build\Release\xray_cli.exe --bench-progress-tsv native\build\native-test-runs\<run>\benchmark.tsv
native\build\Release\xray_cli.exe --bench-compare native\build-release\native-test-runs\<run>\benchmark.tsv native\build-ltcg\native-test-runs\<run>\benchmark.tsv
native\build\Release\xray_cli.exe --rsa260
```

The CLI emits the same reproducible JSON shape used by the GTK app. Use
`--bench-frontier` when you want stdout to show the human-readable benchmark
frontier while still writing the full run artifacts. Use `--bench-progress` for
a one-artifact digest of what is completed, still open, noisy, or excluded as
control evidence. Use `--bench-progress-tsv` when another tool needs the same
progress classification as a TSV table instead of prose. Use `--bench-compare`
to review two benchmark TSV artifacts, such as Release versus `/GL`, and
surface rows that are ready in both builds, ready in only one build, or rejected
by worst-pair safety. Add `--bench-min-digits N` and/or `--bench-max-digits N`
plus `--bench-filter TEXT` to `--bench-progress`, `--bench-progress-tsv`, or
`--bench-compare` when you want a focused size-window or operation/policy review
without editing or truncating the full benchmark artifact. Filters compose, so a
review can isolate, for example, only `mul` rows inside the 768-to-1000 digit
window.

- `factorReport`
- `cyclotomicReport`
- `benchmarkReport`
- source notes linking behavior back to Payam's MY GFN2 page and the local-solver limit for large composites

## GUI Taste Direction

The GTK shell follows the generated "precision proof-lab" concept saved at [`design/native-workbench-concept.png`](design/native-workbench-concept.png):

- dark graphite/green-black instrument surface
- cyan for verified proof, amber for caution, red for unresolved/failed proof
- left input rail, central notebook, right proof inspector, bottom run log
- tabs: `Cyclotomic X-Ray`, `Factor Solver`, `Benchmarks`, `Report JSON`
- compact laptop layout that keeps all regions visible, with a wider profile for desktop-class monitors
- compact research density without nested cards or marketing copy

The UI should feel like a serious lab instrument for Payam/research users, not a toy demo.
