# Physical input v1

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->

Status: v1.0 draft. The WPF UI is the client and `native/input` is the server of `\\.\pipe\ETS2AP_PhysicalInput_v1`. This dedicated local pipe carries the latest physical-input state from WPF Raw Input capture to the native safety monitor. It is independent of Control.v1 command traffic.

## Peer identity and lifetime

The server creates a protected pipe for the current user's SID, rejects remote clients, and owns the first pipe instance. It accepts one client at a time only after verifying the client's canonical executable path is the installed `ETS2AP.Ui.exe` and that its user SID matches the native process. The WPF client independently verifies that the server image matches the configured `eurotrucks2.exe` path and that both processes use the same user SID. Path and SID checks identify the expected local processes; they are not a binary-signature check.

The WPF Dispatcher publishes one frame every 50 ms, including unchanged state. The native server timestamps frames when received using its own steady clock; clients do not send timestamps. A report is available for less than 250 ms after receipt. Missing, stale, malformed, replayed, or disconnected input is unavailable and treated as manual input active. Each new connection starts without an accepted report, and its sequence restarts at 1.

## Frame layout

Each frame is exactly 24 bytes. All integers are unsigned little-endian. The pipe is a byte stream, so the receiver reads exactly 24 bytes per frame and closes the connection on a partial-frame timeout or invalid frame.

| Offset | Size | Field | Valid value |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `E2PI` |
| 4 | 2 | version | 1 |
| 6 | 2 | flags | Bit 0: Raw Input source registered; bit 1: manual input active; all other bits zero |
| 8 | 4 | frame_size | 24 |
| 12 | 8 | sequence | Starts at 1 and strictly increases within a connection |
| 20 | 4 | reserved | 0 |

If bit 0 is clear, bit 1 must be set. An unavailable source cannot claim an idle state. Unknown flags, zero or non-increasing sequence, invalid magic/version/size, nonzero reserved bytes, and a contradictory unavailable-idle frame are rejected.

## Safety semantics

WPF registers Raw Input on its own window and tracks keyboard state by device and virtual key. It processes key up/down events even when ETS2 is not foreground, so losing focus cannot leave a held key latched; device removal clears that device's held keys. Mouse buttons are tracked by device and their releases are also processed while ETS2 is unfocused; device removal clears that device's held buttons. Manual activity is reported to native only while ETS2 is foreground. Mouse movement, XInput, and observed HID activity are included in the manual-input flag. The native DLL does not register Raw Input.

Raw Input registration is only a source-availability claim. It does not prove complete physical-device coverage. Native still requires independent keyboard, XInput, and device-inventory probes; `coverage_validated` remains false until in-game coverage is verified, and unclassified HID devices keep the monitor unhealthy. This channel can detect and latch manual override but does not authorize engagement or enable automatic driving.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
