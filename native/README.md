# Native Number X-Ray Workbench

The native workbench is a cross-platform C + GTK proof instrument that sits beside the existing browser demo. It uses GMP-compatible arbitrary precision arithmetic (`gmp.h` on Linux/macOS, MPIR via vcpkg on Windows) and treats every result as one of:

- `solved`: exact factors found and product verification passed
- `partial`: at least one factor found, with unresolved cofactors remaining
- `timeout` / `cancelled`: local budget ended before completion
- `unsolved`: no exact local factor proof found

Large challenge fixtures are intentionally not claimed solved. The native app may profile and attempt bounded methods, but it must keep a challenge number unresolved unless exact factors are found and the factor product equals the input.

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
`xray_bigint_route_config()` or the Python helper's `bigint_route_config()` to
record the Number X-Ray scratch route thresholds used beside that backend
baseline.

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

Rows with parity but without `replacementReady` are useful migration evidence, but the production path must remain GMP-backed.

Threshold and root-size policy scouts have an additional protection: a single
row cannot become route-ready. Any policy row with a nonzero size gate,
threshold, or depth limit must report
`thresholdSafety=requires-forced-neighbor`, `noAutoRoute=1`,
`replacementReady=false`, and `adoption=observe-only` until a dedicated
forced-neighbor safety row and a product-like `/GL` run both pass. This keeps a
one-pocket win from becoming a harmful global default.

Every benchmark run writes three benchmark artifacts in its run folder:

- `benchmark.json`: machine-readable rows, CPU features, timings, gates, and status labels
- `benchmark.tsv`: spreadsheet-friendly rows for sorting and comparison
- `benchmark_frontier.txt`: human-readable CPU feature summary, near wins, largest scratch gaps, and scratch/kernel timing tables

Kernel rows in `benchmark_frontier.txt` include compact route tags such as
`thr=`, `leaf=`, `depth=`, and `base=` when those values are present in the
underlying benchmark detail. The full JSON/TSV artifacts remain the canonical
machine-readable record.

The scratch-vs-GMP ladder currently measures 40, 150, 1000, 4096, and 8192 decimal digit operands so local changes have to keep scaling beyond tiny examples before they earn adoption labels. Parse and format rows are tracked separately because decimal ingestion and decimal serialization have very different bottlenecks. Multiplication and specialized square rows also have a 16384 digit discovery tier so larger-number arithmetic work can be observed before it is considered for routing. Multiplication rows aggregate two deterministic operand families because threshold-sensitive multiply code can look good on one number shape and lose on another. The tournament rows intentionally test several parse chunk sizes, decimal formatting handoff thresholds, multiply leaf thresholds, square thresholds, and Toom handoff candidates in one run; those rows are evidence-only until a bounded window wins with exact parity and stable same-run paired ratios. Decimal formatter route changes also need `format-dc-route` same-run evidence so a direct-output D&C pocket win cannot be mistaken for a global or root-size threshold.

Add/sub benchmark rows use a higher iteration floor at 1000+ digits because those operations are fast enough that sub-millisecond timing windows can flip adoption labels from scheduler noise rather than real algorithm behavior.

On MSVC x64 builds, the benchmark also emits evidence-only `mul-toom3-unroll4-*` rows. These combine the one-level Toom-3 split with the bounded `_umul128`/`_addcarry_u64` unroll4 leaf schedule so larger-number work can tell whether GMP's advantage is coming from algorithm selection, leaf scheduling, or both. Extra GMP-facing handoff scout rows test leaf thresholds near GMP's tuned Toom region (`24`, `48`, `96`) without adding production routes. The `mul-toom3-u4-rec-vs-gmp` rows test a depth-limited recursive Toom-3 variant at the 16384 digit frontier. Deep rows such as `mul-toom3-unroll4-deep-vs-gmp` and `mul-toom3-u4-rec-deep-vs-gmp` rerun selected promising GMP comparisons with 9 paired samples, including the recursive leaf `64` and `96` gates, so a noisy near-win cannot influence routing. They do not widen the production route unless parity and the same-run stability gate both pass.

The benchmark also emits `kernel-probe` rows up through 8192-digit multiply threshold cases and bounded one-level Toom-3 multiply probes. These are not production replacements; they are local proof drills for GMP-inspired primitive choices. A probe row compares a candidate carry-chain kernel, multi-operand Karatsuba threshold, CPU-specific multiply-add primitive, square basecase, or Toom-3 stage to a baseline using the same-run paired median ratio, records the CPU/compiler feature gate, and labels the candidate `promote-candidate`, `observe-only`, or `blocked-output-mismatch`. A promotion candidate must also win enough paired samples under the probe gate: ordinary five-sample rows require 4 wins, while deep nine-sample rows require 8 wins. On MSVC x64 machines, `muladd-unroll4` and `muladd-unroll8` rows compare unrolled `_umul128`/`_addcarry_u64` multiply-accumulate loops with the scalar loop; when BMI2 and ADX are present, `muladd-bmi2-adx` rows compare `_mulx_u64`/`_addcarryx_u64` against the same scalar baseline. Full-shape probes name their baseline explicitly: `square-vs-mul` compares specialized square against the current self-multiply path, `square-karatsuba-vs-mul` compares recursive Karatsuba square against current self-multiply, `square-karatsuba-vs-gmp` compares that candidate directly against `mpz_mul` across multiple handoff thresholds, `mul-unroll4-vs-scratch` compares the route candidate against the scalar threshold multiply path, `mul-toom3-vs-scratch` compares Toom-3 against the current scratch multiply path, and GMP-gate rows such as `mul-unroll4-vs-gmp` compare the candidate directly against `mpz_mul`. The `mul-unroll4-deep-vs-gmp` rows rerun the high-risk 4096/8192 digit GMP gate with 9 paired samples so noisy CPU-feature claims have to survive a stricter local check before they can influence routing. Both gates must pass across the target sizes before an internal route can be considered for adoption. This is the path for AVX/ADX/BMI2 work and algorithm-stage tuning: no primitive moves into the scratch bigint layer until the GUI benchmark can show exact parity and a stable local speed win.

Measured keep/drop decisions are recorded in [`PERFORMANCE.md`](PERFORMANCE.md) so rejected scouts do not get repeated as if they were still unexplored.

## GMP Source Clues

GMP is used as an oracle and as a design map, not as copied implementation. Notes from the official GMP 6.3.0 source and manual:

- x86_64 GMP uses 64-bit limbs (`GMP_LIMB_BITS 64`), and the scratch layer now uses 64-bit limbs too.
- GMP's `mpn_mul_basecase` starts with `mul_1` and accumulates with `addmul_1`/multi-limb variants, avoiding an initial zeroing loop.
- GMP keeps CPU-family-specific `mpn/x86_64` kernels and tuned thresholds for Broadwell/Skylake-class chips.
- GMP's tuned Skylake/Broadwell thresholds move from basecase multiplication to Toom around 26 limbs, then to larger Toom/FFT stages later.
- AVX/ADX/BMI2 ideas must be proven on this laptop with local parity tests and paired benchmark ratios before adoption.

Current native probes test that first clue directly by comparing exact 64-bit limb add/sub carry chains against exact 32-bit limb carry chains for the same bit widths. On MSVC/x86 builds they also test `_addcarry_u32` and `_subborrow_u32` against the scalar 32-bit baseline, because previous carry-intrinsic experiments were noisy and must remain evidence-only until they win reproducibly.

The 64-bit scratch migration is intentionally gated per operation. Add/sub now use a 64-bit carry/borrow helper with MSVC x64 intrinsics where available and portable C elsewhere; subtraction also copies unchanged high limbs after the borrow has cleared, but only the benchmark may mark those rows replacement-ready. The production decimal parser keeps the 19-digit chunk route, while `xray_bigint_set_decimal_chunk_probe` exposes 1-to-19 digit diagnostic chunking for local tournaments and importing tools. `mul` can become replacement-ready where the benchmark proves it; large balanced operands now take a bounded Karatsuba path with a difference-form middle product and explicit GMP-oracle sign tests, and MSVC x64 uses an unroll4 multiply leaf only inside the measured safe window (`8 <= min limbs`, `max limbs <= 512`, and not strongly unbalanced). `square` uses self-multiply for tiny operands (`<= 8` limbs), where the local paired benchmark showed the specialized square loop losing, then routes through the Karatsuba square dispatcher for larger operands. Separate Toom-3 probes test the next algorithmic stage without changing production routing. Any multiply or square row that fails the local speed gate stays `oracle-only` or `observe-only`. Decimal formatting emits existing 9 digit chunks with a small manual writer, a formatter-private base-1e9 in-place division loop, conservative chunk pre-reservation, and a larger-limb Horner converter for binary-limb-to-decimal chunk generation. The production formatter now uses the two-digit pair-table text writer only in bounded limb ranges where local paired benchmarks found a stable win (`<= 8` limbs, or the Horner band from `48` through `54` limbs), and it keeps the large decimal divide-and-conquer route on the ladder formatter at leaf `8`; direct-output D&C remains a required benchmark probe after Release and `/GL` disagreed on its safe size window. The legacy Horner-threshold probe stays available so future benchmark rows can still compare candidate and baseline honestly. `mod-u32`/`divmod-u32`/`powmod-u32` use reciprocal multiply-high division with exact correction to avoid slow hardware division in the hot path. `gcd-u32` reduces through that modulo path, then uses a division-free binary GCD tail; the 65537 case uses an exact Fermat-prime limb fold because `2^64 == 1 (mod 65537)`. Rows that do not beat GMP remain evidence only; evidence is not permission to route production solver accounting away from GMP.

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
native\build\Release\xray_cli.exe --rsa260
```

The CLI emits the same reproducible JSON shape used by the GTK app. Use
`--bench-frontier` when you want stdout to show the human-readable benchmark
frontier while still writing the full run artifacts:

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
