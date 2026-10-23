# Telemetry IPC v1

Status: v1.0 draft. Producer: native/plugin. Consumer: python. Object: `Local\ETS2AP_Telemetry_v1`, a 4096-byte Windows file mapping with one writer and potentially multiple readers. The v1.0 struct occupies its first 240 bytes; all remaining bytes are zero. A missing mapping means unavailable telemetry.

## Layout

The wire image is a fixed-size C struct under `#pragma pack(push, 8)`. Multi-byte values are little-endian; float64 is IEEE 754 binary64; uint8 is the only boolean representation. Offsets are absolute bytes from the mapping start. Every float64 field begins at an 8-byte-aligned offset. Padding is zero. A v1.0 producer sets `struct_size` to 240. Compatible 1.x producers may append fields within the 4096-byte mapping while preserving the first 240 bytes. Exceeding 4096 bytes requires a new major version and mapping name.

| Offset | Size | Field | Wire type | Unit | Valid value or range | SCS SDK source |
|---:|---:|---|---|---|---|---|
| 0 | 4 | magic | uint32 | — | ASCII bytes E2AP; numeric 0x50413245 | Protocol metadata; no SDK source |
| 4 | 2 | version_major | uint16 | — | 1 | Protocol metadata; no SDK source |
| 6 | 2 | version_minor | uint16 | — | 0 for this layout | Protocol metadata; no SDK source |
| 8 | 4 | struct_size | uint32 | bytes | 240 for v1.0; at least 240 for compatible 1.x | Protocol metadata; no SDK source |
| 12 | 4 | validity_flags | uint32 | bitmask | Bits 0–13 below; bits 14–31 zero in v1.0 | Protocol metadata; no SDK source |
| 16 | 8 | sequence | uint64 | count | Even when stable; wraps modulo 2^64 | Protocol metadata; no SDK source |
| 24 | 8 | timestamp_ns | uint64 | ns | QPC-derived monotonic publish time | Local clock; no SDK source |
| 32 | 1 | paused | uint8 | boolean | 0 or 1; validity bit 0 | `SCS_TELEMETRY_EVENT_paused` / `SCS_TELEMETRY_EVENT_started` events |
| 33 | 1 | moving_backward | uint8 | boolean | 0 or 1; validity bit 1 | `truck.speed` (hysteretic backward motion) |
| 34 | 2 | reserved_0 | 2 bytes | — | Zero | Padding; no SDK source |
| 36 | 4 | gear_displayed | int32 | gear index | Full int32 range; >0 forward, 0 neutral, <0 reverse; validity bit 3 | `truck.displayed.gear` |
| 40 | 4 | gear_engine | int32 | gear index | Full int32 range; >0 forward, 0 neutral, <0 reverse; validity bit 3 | `truck.engine.gear` |
| 44 | 4 | reserved_1 | 4 bytes | — | Zero | Padding; no SDK source |
| 48 | 8 | speed_mps | float64 | m/s | Finite, >= 0; validity bit 1 | `truck.speed` (absolute value) |
| 56 | 8 | engine_speed_rad_s | float64 | rad/s | Finite, >= 0; validity bit 2 | `truck.engine.rpm` (RPM × 2π/60) |
| 64 | 8 | steering_input | float64 | ratio | Finite, [-1,1]; positive left, negative right; validity bit 4 | `truck.input.steering` |
| 72 | 8 | steering_effective | float64 | ratio | Finite, [-1,1]; positive left, negative right; validity bit 4 | `truck.effective.steering` |
| 80 | 8 | throttle_input | float64 | ratio | Finite, [0,1]; validity bit 5 | `truck.input.throttle` |
| 88 | 8 | throttle_effective | float64 | ratio | Finite, [0,1]; validity bit 5 | `truck.effective.throttle` |
| 96 | 8 | brake_input | float64 | ratio | Finite, [0,1]; validity bit 6 | `truck.input.brake` |
| 104 | 8 | brake_effective | float64 | ratio | Finite, [0,1]; validity bit 6 | `truck.effective.brake` |
| 112 | 8 | world_x_m | float64 | m | Finite signed value; validity bit 7 | `truck.world.placement` |
| 120 | 8 | world_y_m | float64 | m | Finite signed value; validity bit 7 | `truck.world.placement` |
| 128 | 8 | world_z_m | float64 | m | Finite signed value; validity bit 7 | `truck.world.placement` |
| 136 | 8 | heading_rad | float64 | rad | Finite, [0,2π); counterclockwise from north viewed from above; validity bit 7 | `truck.world.placement` (heading × 2π, wrapped) |
| 144 | 8 | pitch_rad | float64 | rad | Finite, [-π/2,π/2]; positive up; validity bit 7 | `truck.world.placement` (pitch × 2π) |
| 152 | 8 | roll_rad | float64 | rad | Finite, [-π,π]; positive by the right-hand rule around local +Z; validity bit 7 | `truck.world.placement` (roll × 2π) |
| 160 | 8 | linear_velocity_local_x_mps | float64 | m/s | Finite signed value; validity bit 8 | `truck.local.velocity.linear` |
| 168 | 8 | linear_velocity_local_y_mps | float64 | m/s | Finite signed value; validity bit 8 | `truck.local.velocity.linear` |
| 176 | 8 | linear_velocity_local_z_mps | float64 | m/s | Finite signed value; validity bit 8 | `truck.local.velocity.linear` |
| 184 | 8 | angular_velocity_local_x_rad_s | float64 | rad/s | Finite signed value; validity bit 9 | `truck.local.velocity.angular` (revolutions/s × 2π) |
| 192 | 8 | angular_velocity_local_y_rad_s | float64 | rad/s | Finite signed value; validity bit 9 | `truck.local.velocity.angular` (revolutions/s × 2π) |
| 200 | 8 | angular_velocity_local_z_rad_s | float64 | rad/s | Finite signed value; validity bit 9 | `truck.local.velocity.angular` (revolutions/s × 2π) |
| 208 | 8 | navigation_distance_m | float64 | m | Finite, >= 0; validity bit 10 | `truck.navigation.distance` |
| 216 | 8 | navigation_time_s | float64 | s | Finite, >= 0; validity bit 11 | `truck.navigation.time` |
| 224 | 8 | speed_limit_mps | float64 | m/s | Finite, >= 0; validity bit 12 | `truck.navigation.speed.limit` |
| 232 | 8 | cruise_speed_mps | float64 | m/s | Finite, >= 0; 0 means disabled; validity bit 13 | `truck.cruise_control` |

The 14 contiguous validity groups are: 0 paused; 1 speed and moving_backward; 2 engine speed; 3 displayed and engine gears; 4 steering input and effective; 5 throttle input and effective; 6 brake input and effective; 7 world placement and orientation; 8 all local linear velocity components; 9 all local angular velocity components; 10 navigation distance; 11 navigation time; 12 speed limit; 13 cruise speed. A channel-backed group bit is set only when the most recent callback for every channel in that group supplied a non-NULL, finite, in-range value. If the most recent callback for any channel supplies NULL, a nonfinite value, or an out-of-range value, clear its group bit and zero every field in that group in the next publication. Reset clears all bits at plugin initialization and when a game configuration event requires channel re-registration; a paused/started transition does not reset channel validity. Event-backed paused becomes valid after a paused or started event. If a bit is unset, the consumer ignores its zeroed fields. No NaN or infinity is transmitted. A speed limit of zero is valid when bit 12 is set, and a cruise value of zero means disabled when bit 13 is set; neither zero value determines availability. The plugin updates and publishes `paused` immediately upon a paused or started event, without waiting for the next frame callback.

Bits 14–31 provide 18 more validity groups for future 1.x extensions. If those are exhausted, a future 1.x layout appends `validity_flags2` (uint32 or uint64) in the tail; that minor-version specification SHALL define its exact aligned offset, width, and bits. Readers SHALL check `struct_size` and mapping length before accessing any appended flag or field; data beyond `struct_size` is invalid. A reader validates bits it understands and ignores unknown bits in a compatible 1.x producer.

## Frames, precision, and conversions

The world frame is +X east, +Y up, +Z south (right-handed), as confirmed against the official SCS Telemetry SDK 1.16 headers. `truck.world.placement` is one dplacement channel: position x/y/z are source float64, while heading/pitch/roll are source float32. The local frame is +X right, +Y up, +Z back. Both local velocity channels use that local frame; their x/y/z components preserve their SDK signs. `truck.speed` is signed (negative while moving backward); only `speed_mps` takes its absolute value. `moving_backward` describes motion, not reverse gear; intended gear is read from `gear_displayed`. After reset, start `moving_backward` at zero and apply the thresholds to the first valid speed sample: set it below -0.05 m/s, clear it above -0.02 m/s, otherwise keep zero **[TUNE]**. Later samples retain the preceding value between those thresholds. Treat -0.0 as zero.

Source speed, engine RPM, controls, local velocity components, navigation values, and cruise speed are float32. They are widened to float64 on the wire; widening does not increase source precision. Position is sourced as float64. SDK heading is in [0,1], with 0 = north, 0.25 = west, 0.5 = south, and 0.75 = east; convert by ×2π and wrap to [0,2π). SDK pitch is in [-0.25,0.25], positive up; SDK roll is in [-0.5,0.5], positive by the right-hand rule around local +Z. Convert both by ×2π, retaining their signs. SDK angular velocity is revolutions/s; multiply by 2π to obtain rad/s. Effective brake does not include retarder, parking brake, or engine brake. Navigation time is ETA seconds; navigation speed limit depends on the Route Advisor speed-limit option.

## Mapping ownership and lifecycle

Only one plugin writer is permitted. Before creating the mapping, the ETS2 plugin SHALL acquire the named mutex `Local\ETS2AP_Telemetry_v1.lock`, whose DACL grants access only to the current user SID, like the mapping. Failure to acquire it or an unexpected owner means publication is refused and an error is reported. Any second writer, including an ATS plugin, SHALL refuse to publish to this ETS2 mapping. This prevents two games from interleaving seqlock writes. ATS support would require its own mapping name and contract. A process under the same SID can still occupy the mutex and deny service; that is an accepted residual risk.

The plugin creates a 4096-byte mapping with a DACL restricted to the current user SID. The plugin opens it for read/write; Python under that SID SHALL open it read-only. A DACL cannot distinguish processes with the same SID, so it cannot enforce Python-only read access against a compromised same-user process. Strong process-specific write isolation needs a distinct token/SID or broker **[Need decision: Kingsley]**. On `ERROR_ALREADY_EXISTS`, the plugin SHALL call `GetSecurityInfo` on that mapping handle and verify that owner and DACL match the current user SID before publishing; a mismatch is an error. A mapped view is page-rounded and does not reveal the mapping object's true allocation size. A reader SHALL verify that its view spans 4096 bytes (or at least the declared `struct_size`), that `struct_size` is at least 240 and no larger than that view, and that the known header is valid. The reader SHALL only open an existing mapping with `OpenFileMapping` and `FILE_MAP_READ`. Python's `mmap(tagname=...)` can create an absent mapping and SHALL NOT be used directly for this reader; use `ctypes` calls to `OpenFileMapping` and `MapViewOfFile`, then close the view and handle. Game and Python are assumed to run at the same privilege level; a game elevated above Python may make opening fail, which is treated as unavailable telemetry.

When the plugin starts and finds an authorized compatible mapping, it holds the named mutex, moves any abandoned odd sequence to the next even value, and performs an ordinary seqlock reset publication: rewrite the header, clear the payload and `validity_flags`, set `timestamp_ns` to the current QPC-derived time, zero the bytes after `struct_size` through byte 4095, and finish on an even sequence before normal updates. A new mapping starts with sequence zero, a valid header, zero payload and validity, and a current QPC timestamp. QPC is monotonic within one boot, so reset does not regress the timestamp; a named mapping cannot survive a reboot. A reader treating an odd sequence as continuous for more than 10 ms SHALL mark the mapping unavailable **[TUNE]**. Recovery requires a stable even sequence that advances after the failed observation, followed by full header, size, validity, and timestamp validation. A sequence or timestamp regression invalidates a snapshot and requires reopening or reinitializing the reader; an unchanged even sequence alone does not prove recovery.

## Time and publication

All binary timestamps use QPC ticks and `QueryPerformanceFrequency`. To convert integer ticks to uint64 nanoseconds without overflowing an intermediate multiplication, compute `(ticks / freq) * 1_000_000_000 + ((ticks % freq) * 1_000_000_000) / freq` with checked arithmetic or a wider intermediate; reject an unrepresentable result. C++ uses `QueryPerformanceCounter`; Python uses `time.perf_counter_ns`; C# uses `Stopwatch.GetTimestamp` and `Stopwatch.Frequency`. The Python and C# clocks SHALL be verified to share QPC's origin on the supported Windows runtime **[VERIFY]** before cross-language timestamp comparison.

For every game-frame update, and immediately for a paused/started event, the writer stores an odd sequence with relaxed ordering, executes a release fence, writes every payload field and `validity_flags`, then release-stores the next even sequence. It SHALL not write payload outside this interval. uint64 sequence is 8-byte aligned at offset 16. A reader acquire-loads sequence, retries if odd, copies the known prefix, executes an acquire fence, then acquire-loads sequence again. It accepts only matching even sequence values. On Windows x64, Python and C# readers rely on the platform's memory ordering and aligned 8-byte sequence access. A plain C++ `memcpy` racing with writer stores is a C++ data race; implementations need atomic reads or an equivalent documented interprocess snapshot mechanism. `volatile` alone does not make a C++ data race safe.

After a stable copy, a reader checks magic, major version, minimum `struct_size`, known validity bits, finite ranges, and timestamp before using data; it ignores reserved bytes and unknown validity bits in a compatible 1.x producer. It never reads beyond the mapped length or `struct_size`. A regressed sequence or timestamp is invalid. The producer updates once per SDK game-frame callback when available. A consumer SHALL NOT assume a fixed frame rate. Python SHALL disengage when required telemetry is invalid, paused, or older than 250 ms **[TUNE]**.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
