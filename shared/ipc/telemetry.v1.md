# Telemetry IPC v1

Status: v1.0 draft. Producer: native/plugin. Consumer: python. Object: `Local\ETS2AP_Telemetry_v1`, a Windows file mapping with one writer and potentially multiple readers. The v1.0 mapping is 240 bytes. A missing mapping means unavailable telemetry.

## Layout

The wire image is a fixed-size C struct under `#pragma pack(push, 8)`. Multi-byte values are little-endian; float64 is IEEE 754 binary64; uint8 is the only boolean representation. Offsets are absolute bytes from the mapping start. Every float64 field begins at an 8-byte-aligned offset. Padding is zero. A v1.0 producer sets `struct_size` to 240. Compatible 1.x producers may append fields while preserving the first 240 bytes.

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
| 33 | 1 | reverse | uint8 | boolean | 0 or 1; validity bit 1 | `truck.speed` (< 0 means 1) |
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
| 152 | 8 | roll_rad | float64 | rad | Finite, [-π,π]; counterclockwise viewed along roll axis; validity bit 7 | `truck.world.placement` (roll × 2π) |
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

The 14 contiguous validity groups are: 0 paused; 1 speed and reverse; 2 engine speed; 3 displayed and engine gears; 4 steering input and effective; 5 throttle input and effective; 6 brake input and effective; 7 world placement and orientation; 8 all local linear velocity components; 9 all local angular velocity components; 10 navigation distance; 11 navigation time; 12 speed limit; 13 cruise speed. A bit is set only when every field in its group is current and valid. If unset, the producer writes zero to those fields and the consumer ignores them. No NaN or infinity is transmitted. The plugin updates `paused` when it receives the paused or started event; this is an event-derived state, not a telemetry channel.

## Frames, precision, and conversions

The world frame is +X east, +Y up, +Z south (right-handed), as confirmed against the official SCS Telemetry SDK 1.16 headers. `truck.world.placement` is one dplacement channel: position x/y/z are source float64, while heading/pitch/roll are source float32. The local frame is +X right, +Y up, +Z back. Both local velocity channels use that local frame; their x/y/z components preserve their SDK signs. `truck.speed` is signed (negative in reverse); only `speed_mps` takes its absolute value, while `reverse` records the sign.

Source speed, engine RPM, controls, local velocity components, navigation values, and cruise speed are float32. They are widened to float64 on the wire; widening does not increase source precision. Position is sourced as float64. SDK heading is in [0,1], with 0 = north, 0.25 = west, 0.5 = south, and 0.75 = east; convert by ×2π and wrap to [0,2π). SDK pitch is in [-0.25,0.25], positive up; SDK roll is in [-0.5,0.5], counterclockwise viewed along its axis. Convert both by ×2π, retaining their signs. SDK angular velocity is revolutions/s; multiply by 2π to obtain rad/s. Effective brake does not include retarder, parking brake, or engine brake. Navigation time is ETA seconds; navigation speed limit depends on the Route Advisor speed-limit option.

## Publication and reading

The mapping starts with sequence 0 and a zero validity mask. For each game-frame update, the single writer atomically increments sequence to an odd value, writes all fields and the validity mask, then publishes with a release barrier and atomically increments sequence to an even value. It must not write the payload outside this interval. uint64 sequence is aligned at offset 16. The reader uses acquire semantics: read sequence, retry if odd, copy the known prefix, read sequence again, and accept only if both reads match and are even. A reader bounds its retries and treats a permanently odd mapping as unavailable.

After a stable copy, a reader checks magic, major version, minimum struct_size, reserved padding bytes, known validity bits, finite ranges, and timestamp before using data. A 1.x reader accepts a larger struct_size, ignores unknown validity bits and an appended tail, and never reads beyond the mapped length. The producer updates once per SDK game-frame callback when available. A consumer must not assume a fixed frame rate. Python should disengage when required telemetry is invalid, paused, or older than 250 ms **[TUNE]**; this threshold needs in-game validation.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
