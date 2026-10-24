<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
# Native modules

This directory contains the Windows x64 telemetry writer. `telemetry_core` owns the IPC layout, conversions, validity groups, seqlock, and shared mapping. `plugin` connects SCS Telemetry SDK callbacks to that core and builds `ets2ap_plugin.dll`. `test_support` provides the repository's small C++ test framework. Input injection belongs to a later task.

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

`ETS2AP_TEST(Name)` registers a test. Use `CHECK`, `CHECK_EQ`, `CHECK_NE`, and `CHECK_NEAR` for nonfatal assertions. `ETS2AP_TEST_MAIN()` supplies the runner. Test executables accept `--list` and `--filter=<substring>`; the final line reports tests, checks, and failures. A run fails when an assertion or test throws, or when no tests or checks run. CTest runs the framework selftest, telemetry core tests, mapping tests, and plugin tests separately.
