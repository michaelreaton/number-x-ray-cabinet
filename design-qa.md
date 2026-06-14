# Native Workbench Visual QA

Final result: passed

Reference image:
- `C:\Users\mike\AppData\Local\Temp\codex-clipboard-f8cbc8d8-63f0-4049-95ce-cc0f52715c51.png`

Rendered implementation:
- `C:\Users\mike\Documents\PayamAnalysis\artifacts\native-workbench-generic-target.png`
- Captured with DPI-aware Windows window bounds and `PrintWindow` from `native\build-gtk\Release\xray_workbench.exe`.

Comparison checks:
- Layout: Matches the target three-panel workbench with full-height input/config rail, central X-Ray scan chamber, proof inspector, bottom run log, and status strip.
- Chrome: Matches the dark title/menu surface, centered X-Ray/Solver/Bench/JSON tab row, and right-side engine/precision indicators.
- Left rail: Matches line-numbered integer input, digit/bit/parity metadata, preset row, proof controls, and prominent cyan Run Proof control. Reset Workspace is visible below the primary action.
- Center stage: Matches the pipeline stepper, candidate residue rings, factor proof ladder, and evidence timeline using cyan/yellow status colors.
- Right inspector: Matches product verification, unresolved status, reference/structure panels, red unresolved/not-verified emphasis, and details affordance.
- Generality: RSA-260 is no longer presented as a dedicated proof pane; the inspector treats it as one possible current input alongside Fermat F12, benchmark composites, or arbitrary pasted integers.
- Bottom region: Matches a dense run log table and workspace/system status strip.

Intentional deviations:
- Payam source link points to `https://amathz.com/my_gfn.html` rather than the mock's placeholder archive URL.
- A compact `FA` language control remains in the status strip to preserve Persian/English support.
- Charts are code-native GTK/Cairo drawings backed by deterministic sample evidence; they are not screenshots.
- The right inspector uses generic current-input wording instead of the RSA-specific sample copy shown in the mock.

Verification:
- `cmake --build native/build-gtk --config Release`
- `ctest --test-dir native/build-gtk -C Release --output-on-failure`
- `C:\Users\mike\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe --test tests/*.test.js`
- `git diff --check`

Capture note:
- Use `tools\capture-native-window.ps1` for Windows visual QA. It marks the PowerShell process DPI-aware before reading the native window rectangle, so the screenshot reflects the physical 1920x1080 desktop instead of scaled logical coordinates.
