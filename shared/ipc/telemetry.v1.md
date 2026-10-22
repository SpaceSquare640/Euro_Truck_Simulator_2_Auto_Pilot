# Telemetry IPC v1

Status: v1.0 draft. Producer: native/plugin. Consumer: python. Object: Local\ETS2AP_Telemetry_v1, a Windows file mapping with a single writer and potentially multiple readers. The v1.0 mapping is 208 bytes. The producer creates and initializes it before publishing a snapshot; a consumer treats a missing mapping as unavailable telemetry.

## Layout

The wire image is a fixed-size C struct under #pragma pack(push, 8). All multi-byte values are little-endian. float64 means IEEE 754 binary64, uint8 is the only boolean representation, and all padding is zero. Offsets are absolute bytes from the start of the mapping. A v1.0 producer sets struct_size to 208. Future 1.x producers may append fields, set a larger size, and preserve the first 208 bytes.

| Offset | Size | Field | Wire type | Unit | Valid value or range | SCS SDK channel candidate / source |
|---:|---:|---|---|---|---|---|
| 0 | 4 | magic | uint32 | — | ASCII bytes E2AP; numeric 0x50413245 | Protocol metadata; no SDK channel |
| 4 | 2 | version_major | uint16 | — | 1 | Protocol metadata; no SDK channel |
| 6 | 2 | version_minor | uint16 | — | 0 for this layout | Protocol metadata; no SDK channel |
| 8 | 4 | struct_size | uint32 | bytes | 208 for v1.0; at least 208 for compatible 1.x | Protocol metadata; no SDK channel |
| 12 | 4 | validity_flags | uint32 | bitmask | Bits 0–11 below; bits 12–31 zero in v1.0 | Protocol metadata; no SDK channel |
| 16 | 8 | sequence | uint64 | count | Even when stable; wraps modulo 2^64 | Protocol metadata; no SDK channel |
| 24 | 8 | timestamp_ns | uint64 | ns | QPC-derived monotonic publish time | Local clock; no SDK channel |
| 32 | 1 | paused | uint8 | boolean | 0 or 1; validity bit 0 | Candidate: game.paused event/channel **[VERIFY]** |
| 33 | 3 | reserved_0 | 3 bytes | — | Zero | Padding; no SDK channel |
| 36 | 4 | gear | int32 | gear index | Full int32 range; negative reverse, zero neutral; validity bit 3 | Candidate: truck.gear.dashboard / truck.gear **[VERIFY]** |
| 40 | 8 | speed_mps | float64 | m/s | Finite, >= 0; validity bit 1 | Candidate: truck.speed **[VERIFY]** |
| 48 | 8 | engine_speed_rad_s | float64 | rad/s | Finite, >= 0; validity bit 2 | Candidate: truck.engine.rpm, converted from RPM **[VERIFY]** |
| 56 | 8 | steering_actual | float64 | ratio | Finite, [-1,1]; negative left, positive right; validity bit 4 | Candidate: truck.input.steering / truck.effective.steering **[VERIFY]** |
| 64 | 8 | throttle_actual | float64 | ratio | Finite, [0,1]; validity bit 4 | Candidate: truck.input.throttle / truck.effective.throttle **[VERIFY]** |
| 72 | 8 | brake_actual | float64 | ratio | Finite, [0,1]; validity bit 4 | Candidate: truck.input.brake / truck.effective.brake **[VERIFY]** |
| 80 | 8 | world_x_m | float64 | m | Finite signed value; validity bit 5 | Candidate: truck.world.placement.position.x **[VERIFY]** |
| 88 | 8 | world_y_m | float64 | m | Finite signed value; validity bit 5 | Candidate: truck.world.placement.position.y **[VERIFY]** |
| 96 | 8 | world_z_m | float64 | m | Finite signed value; validity bit 5 | Candidate: truck.world.placement.position.z **[VERIFY]** |
| 104 | 8 | heading_rad | float64 | rad | Finite, [0,2π); validity bit 5 | Candidate: truck.world.placement.orientation.heading; SDK scale/sign conversion **[VERIFY]** |
| 112 | 8 | pitch_rad | float64 | rad | Finite, [-π,π]; validity bit 5 | Candidate: truck.world.placement.orientation.pitch; SDK scale/sign conversion **[VERIFY]** |
| 120 | 8 | roll_rad | float64 | rad | Finite, [-π,π]; validity bit 5 | Candidate: truck.world.placement.orientation.roll; SDK scale/sign conversion **[VERIFY]** |
| 128 | 8 | velocity_x_mps | float64 | m/s | Finite signed value; validity bit 6 | Candidate: truck.velocity.linear.x; otherwise derive from placement **[VERIFY]** |
| 136 | 8 | velocity_y_mps | float64 | m/s | Finite signed value; validity bit 6 | Candidate: truck.velocity.linear.y; otherwise derive from placement **[VERIFY]** |
| 144 | 8 | velocity_z_mps | float64 | m/s | Finite signed value; validity bit 6 | Candidate: truck.velocity.linear.z; otherwise derive from placement **[VERIFY]** |
| 152 | 8 | angular_velocity_x_rad_s | float64 | rad/s | Finite signed value; validity bit 7 | Candidate: truck.velocity.angular.x; otherwise derive from orientation **[VERIFY]** |
| 160 | 8 | angular_velocity_y_rad_s | float64 | rad/s | Finite signed value; validity bit 7 | Candidate: truck.velocity.angular.y; otherwise derive from orientation **[VERIFY]** |
| 168 | 8 | angular_velocity_z_rad_s | float64 | rad/s | Finite signed value; validity bit 7 | Candidate: truck.velocity.angular.z; otherwise derive from orientation **[VERIFY]** |
| 176 | 8 | navigation_distance_m | float64 | m | Finite, >= 0; validity bit 8 | Candidate: navigation.distance **[VERIFY]** |
| 184 | 8 | navigation_time_s | float64 | s | Finite, >= 0; validity bit 9 | Candidate: navigation.time **[VERIFY]** |
| 192 | 8 | speed_limit_mps | float64 | m/s | Finite, >= 0; validity bit 10 | Candidate: navigation.speed.limit **[VERIFY]** |
| 200 | 8 | cruise_speed_mps | float64 | m/s | Finite, >= 0; validity bit 11 | Candidate: truck.cruise_control.speed **[VERIFY]** |

The data fields have the following validity bits: 0 paused; 1 speed; 2 engine speed; 3 gear; 4 all three actual controls; 5 world position and orientation; 6 all linear velocity components; 7 all angular velocity components; 8 navigation distance; 9 navigation time; 10 speed limit; 11 cruise speed. A bit is set only when all fields in its group are current and valid. If unset, the producer writes zero to those fields; the consumer ignores them. No NaN or infinity is transmitted. In particular, navigation values and derived velocities remain invalid until their SDK availability or derivation has been confirmed.

## World frame and conversions

The world frame is the SCS Telemetry SDK world frame: candidate +X east, +Y up, +Z south **[VERIFY]**. World velocities and angular velocities use these axes. Heading is wrapped to [0,2π), pitch and roll to [-π,π]. The SDK angle unit, reference direction, and positive rotation for heading/pitch/roll require official confirmation **[VERIFY]**. Do not set validity bit 5 until the conversion is verified. A derived velocity uses the difference between two valid placements and their monotonic timestamps; it is invalid on the first sample, on nonpositive elapsed time, or across a teleport/discontinuity.

## Publication and reading

The mapping starts with sequence 0 and a zero validity mask. For each game-frame update, the single writer atomically increments sequence to an odd value, writes all fields and the validity mask, then publishes with a release barrier and atomically increments sequence to an even value. It must not write the payload outside this interval. uint64 sequence is aligned at offset 16. The reader uses acquire semantics: read sequence, retry if odd, copy the known prefix, read sequence again, and accept only if both reads match and are even. A reader bounds its retries and treats a permanently odd mapping as unavailable.

After a stable copy, a reader checks magic, major version, minimum struct_size, reserved padding bytes, known validity bits, finite ranges, and timestamp before using data. A 1.x reader accepts a larger struct_size, ignores unknown validity bits and an appended tail, and never reads beyond the mapped length. The producer updates once per SDK game-frame callback when available. A consumer must not assume a fixed frame rate. Python should disengage when required telemetry is invalid, paused, or older than 250 ms **[TUNE]**; this threshold needs in-game validation.

All candidate SDK names above are intentionally unverified because this specification was drafted offline. Claude must check each against the official SCS Telemetry SDK and either confirm a channel, document a different source, or mark the field as derived before implementation.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
