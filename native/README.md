<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
# Native modules

This directory contains the Windows x64 telemetry writer and the production SCS semantic input-device registration. `telemetry_core` owns the telemetry IPC layout, conversions, validity groups, seqlock, and shared mapping. `plugin` connects SCS Telemetry SDK callbacks to that core and builds `ets2ap_plugin.dll`; its semantic input device exposes `steering`, `aforward`, and `abackward` mixes without a player-side binding screen.

`input_core` validates Control.v1 command packets and owns the safe command state machine, 100 ms watchdog expiry logic, neutral re-arm, manual-override latch, output slew limits, and status-packet encoding. The native plugin's `ControlHost` calls `Tick` on an independent 10 ms scheduler and forwards only an engaged, safe snapshot to the semantic input device; all other states are explicitly published as neutral. A braking command cuts applied throttle immediately, throttle stays zero while applied brake remains nonzero, and a lowered steering cap is enforced immediately even when either safety bound exceeds the normal slew rate. No Named Pipe transport, Python client, production command source, or production manual-input detector is connected. The host therefore remains disconnected and emits neutral output; a future transport and manual-input source must supply commands and override events.

The production input device fails closed to neutral. It accepts only finite, in-range commands stamped by the same monotonic clock and returns neutral when a command is disengaged or older than 100 ms. No production [control.v1](../shared/ipc/control.v1.md) named-pipe server or Python control client currently submits these commands, so this change does not enable automatic driving. Support for the semantic mix names depends on the player's ETS2 controls profile and remains **[VERIFY]** in game. The WPF Engage action remains disabled.

The wire contract is [telemetry.v1.md](../shared/ipc/telemetry.v1.md). The vendored SDK headers are under `../third_party/scs_sdk/include`; they are not modified by this build.

## Build and test

In a Visual Studio x64 developer environment:

```powershell
cmake -S native -B build/native -G "Visual Studio 17 2022" -A x64
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure
```

Use the corresponding Visual Studio 18 generator for VS 2026. To check Debug, build with `--config Debug` and run CTest with `-C Debug`. The top-level CMake file uses the static MSVC runtime.

## Test framework

`ETS2AP_TEST(Name)` registers a test. Use `CHECK`, `CHECK_EQ`, `CHECK_NE`, and `CHECK_NEAR` for nonfatal assertions. `ETS2AP_TEST_MAIN()` supplies the runner. Test executables accept `--list` and `--filter=<substring>`; the final line reports tests, checks, and failures. A run fails when an assertion or test throws, or when no tests or checks run. CTest runs the framework selftest, telemetry core tests, mapping tests, plugin tests, and Control.v1 input-core tests separately.
