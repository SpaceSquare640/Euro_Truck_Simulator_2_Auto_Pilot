# Phase 0 SCS Input API feasibility spike

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->

This experimental plugin probes whether the SCS telemetry and input APIs can coexist in one DLL, whether a generic input device can be bound in Euro Truck Simulator 2, and how injected and manual inputs combine. The offline host harness exercises the callbacks without starting the game. Game behavior must be observed separately; the harness cannot prove that a bind or injection works in game.

## Build and test

Run from the source worktree root in a Visual Studio x64 developer environment:

```text
cmake -S spike/phase0 -B build/phase0-vs17 -G "Visual Studio 17 2022" -A x64
cmake --build build/phase0-vs17 --config Release
ctest --test-dir build/phase0-vs17 -C Release --output-on-failure
```

Repeat with `Visual Studio 18 2026` and `build/phase0-vs18`. The test DLL is `build/phase0-vs17/Release/ets2ap_spike.dll`. Copy it to `<game>\bin\win_x64\plugins\` only for a controlled experiment. Remove it after testing. Keep the truck stopped in a safe, open area with parking brake applied and the gearbox in neutral.

## Configuration and operation

The plugin writes only under `%LOCALAPPDATA%\ETS2AP_Spike\`. For the offline harness, `ETS2AP_SPIKE_DIR` may select an existing absolute directory. An invalid override is ignored. `config.txt` is read at each API init, limited to 4096 ASCII bytes. Use `key=value` lines; empty lines and lines beginning with `#` are ignored. Keys are case insensitive.

| Key | Values | Default |
| --- | --- | --- |
| `devices` | `generic`, `semantical`, `both`, `none` | `generic` |
| `idle` | `zero`, `silent` | `zero` |
| `script_speed` | Decimal from `0.01` through `100` | `1` |
| `semantical_inputs` | Up to 16 comma-separated `name:float` or `name:bool` entries | Empty |

The semantical names must use lowercase letters, digits, and underscores. The semantical device is not registered when the list is empty. Generic inputs are `steering`, `throttle`, `brake`, and `test_button` in that order. The actual axis mapping and whether the UI can bind the device remain game-test questions.

`mode.txt` contains `<mode> [token]`, at most 256 bytes. The input callback checks it every 0.5 seconds. A changed valid line restarts the mode timer. Use `scripts\set-mode.cmd steer`, `throttle`, `brake`, `buttons`, or `off` to write a tokenized line. A missing file or invalid mode selects `off`.

| Mode | Output and time in script seconds | End |
| --- | --- | --- |
| `steer` | `steering=+0.5` at [2,5), `-0.5` at [7,10), then linear 0 to +1 at [12,16) | 18 s |
| `throttle` | `0.3` at [2,5), `0.6` at [7,10) | 12 s |
| `brake` | `0.3` at [2,5), `0.6` at [7,10) | 12 s |
| `buttons` | true at [1,2), [4,5), [7,8) | 10 s |

`script_speed` scales script time. Outside the listed intervals, values are zero or false. With `idle=zero`, every input is emitted each active frame. With `idle=silent`, a completed script emits one neutral frame and then no further events until another script starts.

## Output

`spike.log` is append-only UTF-8 with local timestamp, monotonic `qpc_ns`, level, and message. CSV files are per initialization session and use buffered writes, flushed at shutdown and at most once per second. Each CSV stops at 50 MiB. Blank telemetry values mean unavailable, not zero. SCS frame time fields are **microseconds**; `qpc_ns` uses a process-local monotonic QPC conversion.

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

The [vendored SDK](../../third_party/scs_sdk/README.md) contains the official headers and MIT license. This plugin uses no network connection and creates no background threads. It is a feasibility instrument, not production control software. Callback timing, manual-input arbitration, game loading, and in-game binding need actual game verification.

## Known limitations

If an input callback throws, the plugin records the fault once when possible and emits neutral values for every input on subsequent frames; the exception path has not been exercised by an injected-fault test. The `ETS2AP_SPIKE_DIR` directory override is only for the offline host harness; a local junction could still redirect a chosen test directory. Whether the game retains the previous injected value with `idle=silent` requires the in-game manual-input comparison.
