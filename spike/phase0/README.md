# Phase 0 SCS Input API feasibility spike

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->

This experimental plugin probes whether the SCS telemetry and input APIs can coexist in one DLL and whether a semantical input device can drive ETS2 mixes without player-side axis binding. The vendored SCS SDK declares that semantical devices map to same-named game mixes without a binding UI. By default, the plugin registers both the semantical device and the generic `ETS2AP Spike` device so existing profiles recognize the generic device while the no-bind semantic path remains available. The offline host harness verifies registration and scripted values, but it cannot prove that a particular ETS2 profile supports the configured mix names.

## Build and test

Run from the source worktree root in a Visual Studio x64 developer environment:

```text
cmake -S spike/phase0 -B build/phase0-vs17 -G "Visual Studio 17 2022" -A x64
cmake --build build/phase0-vs17 --config Release
ctest --test-dir build/phase0-vs17 -C Release --output-on-failure
```

Repeat with `Visual Studio 18 2026` and `build/phase0-vs18`. The test DLL is `build/phase0-vs17/Release/ets2ap_spike.dll`. Copy it to `<game>\bin\win_x64\plugins\` only for a controlled experiment. Remove it after testing. Keep the truck stopped in a safe, open area with the parking brake applied. Neutral gear is preferable when the transmission mode permits it.

## One-command in-game steering probe

After installing the spike DLL and entering the driving view, run this from the source worktree root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File spike/phase0/scripts/run-game-probe.ps1
```

The probe checks for a fresh plugin CSV session, zero manual steering, parking brake, zero effective throttle, and a stopped, unpaused truck. It accepts a stationary truck left in gear by simple automatic transmission while the parking brake remains applied. It requires `script_speed=1`, waits 10 seconds for you to focus the driving view, then runs an 11-second steering probe that stops before the later ramp. The probe targets the semantic device, so it does not ask the player to bind an axis. It polls buffered telemetry for unsafe motion, throttle, parking brake, pause, and telemetry gaps; a violation stops the probe. It compares signed opposite input pulses against a neutral effective-steering baseline. It writes `off` on exit and, when a non-off command was issued, requires a subsequent neutral input frame before reporting `PASS`; a missing acknowledgement is a failure. CSV buffering can delay detection by approximately one second, so this is a controlled stationary experiment only. `PASS` does not establish autonomous driving safety or prove that every game profile supports the configured semantic mixes. Run `-SelfTest` for the probe's offline result-check fixtures. Pass `-OutputDirectory <absolute path>` when your shell's LocalAppData differs from the game user's profile; the probe validates fresh telemetry rather than relying on process enumeration.

In the controlled ETS2 session on 2026-10-24, selecting the generic device without binding its steering axis produced no driving-view input callbacks. After binding `Spike steering` in the game's control UI, the plugin emitted 189 positive and 187 negative steering events during the automated probe. Game `input_steering` moved to approximately -1 for a +0.5 logical script value and +1 for a -0.5 value; `effective_steering` changed with a slower response. Vehicle speed stayed within approximately -0.001 to +0.002 m/s, and the final `off` command produced an acknowledged neutral input frame. This demonstrates working input delivery in the tested profile, with steering polarity inverted relative to the requested logical values in that DLL. The probe reported failure for the mismatch. The generic device still requires player-side axis binding; an automatic setup path remains unverified.

The DLL used for that game run also produced a peak logged input-frame rate above 63,000/s during the continuously changing steering ramp. The current source negates the generic virtual-axis value to preserve the IPC positive-left convention and holds each ramp step for 0.4 script seconds. Both changes require a rebuilt DLL and another in-game check; the earlier game observation does not verify them. The CSV `steering` column records the logical script value, before generic-axis polarity conversion. Semantical polarity has not been tested.

Both devices are registered by default: the generic `ETS2AP Spike` device preserves existing ETS2 device assignments and exposes bindable axes, while the semantical device provides the no-bind path. Use `devices=semantical` to omit the generic device or `devices=generic` for bindable-axis diagnostics only. The default semantical mix names are `steering`, `aforward`, and `abackward`. SCS documents the no-binding behavior, while support for these specific mix names in the tested ETS2 profile remains **[VERIFY]**. The Spike is still a development harness; the production native control service and automatic connector installation are not implemented.

## Configuration and operation

The plugin writes only under `%LOCALAPPDATA%\ETS2AP_Spike\`. For the offline harness, `ETS2AP_SPIKE_DIR` may select an existing absolute directory. An invalid override is ignored. `config.txt` is read at each API init, limited to 4096 ASCII bytes. Use `key=value` lines; empty lines and lines beginning with `#` are ignored. Keys are case insensitive.

| Key | Values | Default |
| --- | --- | --- |
| `devices` | `generic`, `semantical`, `both`, `none` | `both` |
| `idle` | `zero`, `silent` | `zero` |
| `script_speed` | Decimal from `0.01` through `100` | `1` |
| `semantical_inputs` | Up to 16 comma-separated `name:float` or `name:bool` entries | `steering:float,aforward:float,abackward:float` |

The semantical names must use lowercase letters, digits, and underscores. Generic inputs are `steering`, `throttle`, `brake`, and `test_button` in that order. Generic mode requires player-side binding and is not the intended player setup path.

`mode.txt` contains `<mode> [token]`, at most 256 bytes. The input callback checks it every 0.5 seconds. A changed valid line restarts the mode timer. Use `scripts\set-mode.cmd steer`, `throttle`, `brake`, `buttons`, or `off` to write a tokenized line. A missing file or invalid mode selects `off`.

| Mode | Output and time in script seconds | End |
| --- | --- | --- |
| `steer` | `steering=+0.5` at [2,5), `-0.5` at [7,10), then linear 0 to +1 at [12,16) | 18 s |
| `throttle` | `0.3` at [2,5), `0.6` at [7,10) | 12 s |
| `brake` | `0.3` at [2,5), `0.6` at [7,10) | 12 s |
| `buttons` | true at [1,2), [4,5), [7,8) | 10 s |

`script_speed` scales script time. Outside the listed intervals, values are zero or false. With `idle=zero`, every input is emitted each active frame. With `idle=silent`, a completed script emits one neutral frame and then no further events until another script starts.

## Output

`spike.log` is append-only UTF-8 with local timestamp, monotonic `qpc_ns`, level, and message. CSV files are per initialization session and use buffered writes, flushed at shutdown and at most once per second. A failed CSV write is logged with its Win32 error; a successful short write is logged separately with requested and written byte counts. Either condition disables that CSV stream. Each CSV stops at 50 MiB. Blank telemetry values mean unavailable, not zero. SCS frame time fields are **microseconds**; `qpc_ns` uses a process-local monotonic QPC conversion.

| `frames_*.csv` column | Meaning / unit |
| --- | --- |
| `frame` | Telemetry frame counter |
| `qpc_ns` | QPC-derived monotonic nanoseconds |
| `render_time_us`, `sim_time_us`, `paused_sim_time_us` | SDK frame-start timestamps, microseconds |
| `paused` | 0 or 1 |
| `speed` | Truck speed, m/s |
| `steer_in`, `steer_eff` | Input and effective steering, SDK float |
| `thr_in`, `thr_eff`, `brk_in`, `brk_eff`, `clu_in`, `clu_eff` | Input and effective pedals, SDK float |
| `parking_brake` | SDK boolean |
| `gear_displayed` | Displayed gear, signed integer |
| `cruise` | Cruise speed, m/s |
| `rpm` | Engine revolutions/minute |
| `inj_steering`, `inj_throttle`, `inj_brake`, `inj_test_button` | Latest computed generic injection values |
| `inj_age_us` | Microseconds since those values were computed |

| `inputs_*.csv` column | Meaning / unit |
| --- | --- |
| `device` | Generic or semantical device name |
| `input_frame` | Per-device render-frame counter |
| `qpc_ns` | QPC-derived monotonic nanoseconds |
| `active` | Device activity, 0 or 1 |
| `mode` | Current script mode |
| `script_t_s` | Scaled elapsed script seconds |
| `steering`, `throttle`, `brake`, `test_button` | Values computed for this input frame |
| `events_reported` | Number of events emitted that frame |
| `steering_event_reported` | 1 when the generic steering or semantical `steering:float` event was actually emitted |

The [vendored SDK](../../third_party/scs_sdk/README.md) contains the official headers and MIT license. This plugin uses no network connection and creates no background threads. It is a feasibility instrument, not production control software. Callback timing, manual-input arbitration, game loading, and in-game binding need actual game verification.

## Known limitations

If an input callback throws, the plugin records the fault once when possible and emits neutral values for every input on subsequent frames; the exception path has not been exercised by an injected-fault test. The `ETS2AP_SPIKE_DIR` directory override is only for the offline host harness; a local junction could still redirect a chosen test directory. Whether the game retains the previous injected value with `idle=silent` requires the in-game manual-input comparison.
