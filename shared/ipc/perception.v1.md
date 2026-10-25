# Perception IPC v1

Status: v1.0 draft. The WPF app publishes a best-effort image of the ETS2 client area and input/focus observations; Python reads the mapping. The mapping is local to the Windows session and is not game memory access. It does not establish that any detector is accurate or that automatic control is safe.

## Mapping and snapshot

The mapping name is `Local\ETS2AP_Perception_v1`. The fixed mapping size is 43,264 bytes: a 64-byte header followed by one 160 × 90 RGB24 image (43,200 bytes). The binary layout is little-endian. Sequence is an aligned uint64 seqlock at offset 16. The writer changes it to an odd value before updating the frame and publishes the next even value after the full image is written. The reader copies the entire mapping only when the first and second sequence reads are equal and even; otherwise it retries for at most 10 ms, then treats the frame as unavailable. A frame must be no older than 150 ms.

| Offset | Size | Field | Type | Unit | Range / meaning |
|---:|---:|---|---|---|---|
| 0 | 4 | magic | byte[4] | — | ASCII `E2PF` |
| 4 | 2 | version_major | uint16 | — | 1 |
| 6 | 2 | version_minor | uint16 | — | 0 |
| 8 | 4 | header_size | uint32 | bytes | 64 |
| 12 | 4 | flags | uint32 | bitmask | Only bits 0–3 are defined; all other bits are zero |
| 16 | 8 | sequence | uint64 | count | Stable snapshots are nonzero, even, and strictly increasing; odd means write in progress |
| 24 | 8 | timestamp_ns | uint64 | ns | Monotonic QPC nanoseconds; nonzero, not future, age ≤150 ms |
| 32 | 2 | width | uint16 | pixels | 160 |
| 34 | 2 | height | uint16 | pixels | 90 |
| 36 | 4 | pixel_format | uint32 | enum | 1 = tightly packed RGB24, top row first |
| 40 | 4 | payload_size | uint32 | bytes | 43,200 |
| 44 | 20 | reserved | byte[20] | — | All zero |
| 64 | 43,200 | pixels | byte[43,200] | RGB bytes | Row-major R, G, B; each component 0–255 |

Flags are:

| Bit | Name | Meaning |
|---:|---|---|
| 0 | GAME_FOCUSED | The ETS2 top-level window is the foreground window at capture time |
| 1 | INPUT_MONITOR_HEALTHY | False until physical-device coverage is validated. Raw Input registration and XInput API availability alone do not prove that every wheel, pedal, remapper, or input path is covered |
| 2 | MANUAL_INPUT_ACTIVE | A monitored keyboard, mouse, XInput, or changed Raw HID report was observed during the current 250 ms activity window, or a monitored button remains held |
| 3 | FRAME_VALID | The game window was focused and either a fresh native game frame or a GDI desktop capture succeeded |

The producer publishes at approximately 20 Hz; consumers must use timestamps and must not assume a fixed rate. Failed capture publishes a frame with `FRAME_VALID` clear. Loss of the producer, invalid header, stale/future timestamp, repeated sequence, unknown flags, or nonzero reserved bytes means the frame is unavailable and all dependent control outputs must be neutral.

The image is either a 160 × 90 downsampled native D3D11 back-buffer frame from [gameframe v1](gameframe.v1.md), or a GDI desktop capture of the ETS2 client rectangle when a fresh native frame is unavailable. Neither source is an SDK camera feed. Exclusive-fullscreen, overlays, capture permissions, DPI scaling, DXGI hook compatibility, and other display modes may affect or prevent capture and require runtime verification. Pixel contents are untrusted observations. The current lane-marking estimator is a development prototype; its lane confidence is not a validated road/lane classification. The front-clear heuristic is unvalidated and is disabled by default. It must not authorize control until a detector is validated against representative gameplay and failure cases.

The mapping is created by the WPF app and opened read-only by Python; a missing mapping is never created by the reader. Access is limited to the current Windows user session. The named mutex `Local\ETS2AP_Perception_v1.lock` serializes the producer's writes; readers use the sequence counter and do not wait on the mutex. The current WPF implementation deliberately leaves INPUT_MONITOR_HEALTHY clear because it does not establish calibrated coverage for physical wheel and pedal devices. An unclassified HID report also keeps the monitor unavailable for the process lifetime.

## Safety use

Python derives its control candidate from fresh telemetry and a fresh, valid frame. It sends `PERCEPTION_READY`, `GAME_FOCUSED`, `INPUT_MONITOR_HEALTHY`, `FRONT_CLEAR`, and `MANUAL_INPUT_ACTIVE` claims in Control.v1.1 only when corresponding evidence passes its checks. The native receiver independently requires the Control.v1.1 capability and safety claims, its local telemetry/speed-limit gates, and its manual-input latch. A WPF health flag does not prove the input monitor covers the player's wheel; hardware coverage and focus/capture behavior remain unverified. Automatic control remains disabled until the front detector and manual-input path have been validated; the shipped runtime uses `front_detector_validated = false`.

This channel is observational only. It does not write to ETS2, change game bindings, or inject input.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
