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

The app includes Fermat F12, `2^4096 + 1 = Phi_8192(2)`, as a large exact cyclotomic example. It is meant to test discovery, reporting, and UI handling for much larger structured numbers; it must not be reported as factored unless exact factors are found and product verification passes.

## GMP Parity And Performance Bar

GMP/MPIR is the temporary oracle while the from-scratch integer core grows up. Replacing any GMP-backed path requires two gates:

- exact oracle parity against GMP/MPIR for serialization, arithmetic, modular arithmetic, roots, and factor accounting
- a benchmark record showing the native path matches or outperforms the GMP/MPIR baseline for the operation and operand size being replaced

Until both gates pass, reports and documentation must label the scratch bigint layer as a migration foothold, not as the production solver engine.

Benchmark JSON exposes that rule per operation-size row:

- `parityVerified`: scratch output exactly matched the GMP/MPIR oracle
- `replacementReady`: parity passed and scratch timing was less than or equal to the GMP/MPIR timing for that row
- `scratchUs` and `gmpUs`: median local timings from the current machine
- `speedRatio`: median of paired scratch/GMP sample ratios, not a cherry-picked independent best-time division; a ratio below `1.0` means scratch was faster for that row

Rows with parity but without `replacementReady` are useful migration evidence, but the production path must remain GMP-backed.

## GMP Source Clues

GMP is used as an oracle and as a design map, not as copied implementation. Notes from the official GMP 6.3.0 source and manual:

- x86_64 GMP uses 64-bit limbs (`GMP_LIMB_BITS 64`), while the current scratch layer still uses 32-bit limbs.
- GMP's `mpn_mul_basecase` starts with `mul_1` and accumulates with `addmul_1`/multi-limb variants, avoiding an initial zeroing loop.
- GMP keeps CPU-family-specific `mpn/x86_64` kernels and tuned thresholds for Broadwell/Skylake-class chips.
- GMP's tuned Skylake/Broadwell thresholds move from basecase multiplication to Toom around 26 limbs, then to larger Toom/FFT stages later.
- AVX/ADX/BMI2 ideas must be proven on this laptop with local parity tests and paired benchmark ratios before adoption.

Primary references: [GMP multiplication algorithms](https://gmplib.org/manual/Multiplication-Algorithms.html), [GMP FFT multiplication](https://gmplib.org/manual/FFT-Multiplication.html), and the official GMP 6.3.0 source tarball from [gmplib.org](https://gmplib.org/download/gmp/gmp-6.3.0.tar.xz).

## CLI

```powershell
native\build\Release\xray_cli.exe --bench 10403
native\build\Release\xray_cli.exe --rsa260
```

The CLI emits the same reproducible JSON shape used by the GTK app:

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
