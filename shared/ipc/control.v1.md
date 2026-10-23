# Control IPC v1

Status: v1.0 draft. Python is the client and native/input is the server of \\.\pipe\ETS2AP_Control_v1. The pipe is a single-client, full-duplex Windows byte stream, although v1.0 sends commands only from Python to native/input. The server restricts the pipe to the current user and rejects remote clients. It owns the virtual-controller output and remains safe when no client is connected.

## Packet and framing

Each v1.0 command is exactly 64 bytes. The header contains packet_size so a later 1.x packet can append fields while preserving the v1.0 prefix. The receiver reads the 16-byte initial header, validates packet_size (64–256 bytes), then reads exactly the remaining bytes of that packet. It consumes an unknown 1.x tail without applying it. All multi-byte values are little-endian; float64 is IEEE 754 binary64. The field layout follows 8-byte packing; padding and reserved fields are zero.

| Offset | Size | Field | Wire type | Unit | Valid value or range |
|---:|---:|---|---|---|---|
| 0 | 4 | magic | uint32 | — | ASCII bytes C2AP; numeric 0x50413243 |
| 4 | 2 | version_major | uint16 | — | 1 |
| 6 | 2 | version_minor | uint16 | — | 0 for this layout |
| 8 | 4 | packet_size | uint32 | bytes | 64 for v1.0; 64–256 for compatible 1.x |
| 12 | 4 | reserved_header | uint32 | — | 0 |
| 16 | 8 | sequence | uint64 | count | Strictly increasing within a connection, starting at 1 |
| 24 | 8 | timestamp_ns | uint64 | ns | QPC-derived monotonic command creation time |
| 32 | 8 | steering | float64 | ratio | Finite, [-1,1]; positive left (counterclockwise), negative right |
| 40 | 8 | throttle | float64 | ratio | Finite, [0,1] |
| 48 | 8 | brake | float64 | ratio | Finite, [0,1] |
| 56 | 1 | engage | uint8 | boolean | 0 or 1 |
| 57 | 3 | reserved_0 | 3 bytes | — | Zero |
| 60 | 4 | flags | uint32 | bitmask | 0 in v1.0; any unsupported bit is an invalid command |

The receiver rejects an unknown major, bad size or magic, nonzero reserved bytes or unsupported flags, nonfinite or out-of-range controls, a repeated/out-of-order sequence, or a timestamp in the future or older than 100 ms. Rejection leaves or places the output at zero and disengages. The 100 ms age is measured with the same machine QPC clock as timestamp_ns. The sender sends a new packet at least every 50 ms while engaged **[TUNE]**; repeated packets must have new sequence and timestamp values.

## Output limits and override

Both Python and native/input clamp the command range and independently limit continuous output changes by maximum absolute change per elapsed second. The receiver uses its monotonic elapsed time between accepted packets, not an assumed packet rate. Initial output after engagement starts at zero.
native/input translates this steering convention to the virtual controller's axis direction.

| Output | Suggested maximum change per second | Status |
|---|---:|---|
| steering | 2.0 ratio/s | **[TUNE]** Kingsley confirms after in-game testing |
| throttle | 1.0 ratio/s | **[TUNE]** Kingsley confirms after in-game testing |
| brake | 2.0 ratio/s | **[TUNE]** Kingsley confirms after in-game testing |

engage is a state transition and flags are reserved, so they have no continuous slew rate. Disengagement, manual override, malformed input, and timeout are emergency transitions: the receiver immediately sets steering, throttle, and brake outputs to zero and sets engaged=false, regardless of slew limits. A valid command received more than 100 ms after the last new valid command does not retroactively prevent a timeout; the receiver requires a new neutral packet and later explicit 0→1 engage transition before applying output again. A repeated sequence never refreshes the watchdog. Python applies the same zero/disengage rule when it cannot produce a fresh safe command.

Any detected manual steering, throttle, or brake input immediately takes priority, zeros autopilot output, and disengages. The receiver latches manual override until the manual input is released and a fresh neutral packet with engage=0 has been accepted; a later explicit 0→1 engage transition is then required. The concrete source and threshold for detecting physical manual input while excluding the app's own virtual-controller output require integration confirmation **[VERIFY]**.

## Connection lifecycle

1. On server start or after any disconnect, native/input sets all virtual outputs to zero and engaged=false, then listens on the named pipe. Python connects as client.
2. Each new connection starts a new sequence domain. Python first sends a neutral packet (engage=0, all controls zero, sequence=1). The server validates it and starts the freshness timer. It does not auto-engage from prior state.
3. Python sends fresh packets with increasing sequence and timestamp. A transition to engage=1 may activate output only after the neutral handshake, no manual input, valid telemetry, and an explicit UI request. The receiver limits continuous outputs on every accepted packet.
4. If 100 ms elapses without a new valid packet, native/input zeros all outputs and disengages. Broken pipe, end of stream, malformed packet, producer crash, or failed version check does the same immediately. Closing a connection never retains the previous command.
5. Python may reconnect after a backoff **[TUNE]**. The server requires a new neutral handshake and explicit re-engagement. A reconnect never resumes the previous engaged state.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
