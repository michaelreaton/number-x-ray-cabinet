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

## CLI

```powershell
native\build\Release\xray_cli.exe --bench 10403
native\build\Release\xray_cli.exe --rsa260
```

The CLI emits the same reproducible JSON shape used by the GTK app:

- `factorReport`
- `cyclotomicReport`
- `benchmarkReport`
- source notes linking behavior back to Payam's paper and the local-solver limit for large composites

## GUI Taste Direction

The GTK shell follows the generated "precision proof-lab" concept saved at [`design/native-workbench-concept.png`](design/native-workbench-concept.png):

- dark graphite/green-black instrument surface
- cyan for verified proof, amber for caution, red for unresolved/failed proof
- left input rail, central notebook, right proof inspector, bottom run log
- tabs: `Cyclotomic X-Ray`, `Factor Solver`, `Benchmarks`, `Report JSON`
- compact laptop layout that keeps all regions visible, with a wider profile for desktop-class monitors
- compact research density without nested cards or marketing copy

The UI should feel like a serious lab instrument for Payam/research users, not a toy demo.
