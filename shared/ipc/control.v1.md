# Control IPC v1

Status: v1.0 draft. Python is the client and native/input is the server of \\.\pipe\ETS2AP_Control_v1. This single-client, full-duplex Windows byte stream carries commands from Python to native/input and status packets in the opposite direction. native/input owns the virtual-controller output and remains safe when no client is connected.

## Command packet and framing

Each v1.0 command is exactly 64 bytes. The header contains packet_size so a later 1.x packet can append fields while preserving the v1.0 prefix. The receiver reads the 16-byte initial header, validates packet_size (64–256 bytes), then reads exactly the remaining bytes of that packet. It consumes an unknown 1.x tail without applying it. version_minor is 0 in v1.0 and then packet_size SHALL be 64; a reader accepts any nonzero 1.x minor version when the known 64-byte prefix is valid and skips a larger unknown tail according to packet_size. All multi-byte values are little-endian; float64 is IEEE 754 binary64. The field layout follows 8-byte packing; padding and reserved fields are zero.

| Offset | Size | Field | Wire type | Unit | Valid value or range |
|---:|---:|---|---|---|---|
| 0 | 4 | magic | uint32 | — | ASCII bytes C2AP; numeric 0x50413243 |
| 4 | 2 | version_major | uint16 | — | 1 |
| 6 | 2 | version_minor | uint16 | — | 0 for this layout |
| 8 | 4 | packet_size | uint32 | bytes | 64 for v1.0; 64–256 for compatible 1.x |
| 12 | 4 | reserved_header | uint32 | — | 0 |
| 16 | 8 | sequence | uint64 | count | Strictly increasing within a connection; first is 1; 0 and 2^64-1 invalid |
| 24 | 8 | timestamp_ns | uint64 | ns | QPC-derived monotonic command creation time |
| 32 | 8 | steering | float64 | ratio | Finite, [-1,1]; positive left (counterclockwise), negative right |
| 40 | 8 | throttle | float64 | ratio | Finite, [0,1] |
| 48 | 8 | brake | float64 | ratio | Finite, [0,1] |
| 56 | 1 | engage | uint8 | boolean | 0 or 1 |
| 57 | 3 | reserved_0 | 3 bytes | — | Zero |
| 60 | 4 | flags | uint32 | bitmask | 0 in v1.0; any unsupported bit is an invalid command |

The receiver SHALL reject an unknown major, bad size or magic, nonzero reserved bytes or unsupported flags, NaN, ±Infinity, out-of-range controls, repeated/out-of-order sequence, or an invalid timestamp. It SHALL NOT clamp malformed inputs into range. Treat -0.0 as 0.0. If engage=0, all three control values SHALL be zero. If throttle and brake are both positive in an otherwise valid engaged command, brake takes priority and effective throttle is set to zero before slew limiting: this prevents propulsion against a requested stop. Only the slew limiter may clamp an otherwise valid output. Any rejection zeros output and disengages.

The first command on a connection SHALL have sequence=1, engage=0, and neutral controls. Every later accepted sequence and timestamp SHALL be strictly greater than the previous accepted value; sequence 0 and 2^64-1 are invalid, so the connection must be renewed before exhaustion. Timestamps use the [QPC conversion](telemetry.v1.md#time-and-publication). A timestamp in the future has zero tolerance because peers use the same machine QPC clock; a command older than 100 ms is rejected. The sender sends a new packet at least every 50 ms while engaged **[TUNE]**; repetitions need new sequence and timestamp values.

## Status packet (native/input to Python)

The server sends a fixed 64-byte status packet immediately whenever its state or reason changes and at a 20 Hz interval while connected **[TUNE]**. The status is a post-limiter observation, never an authorization to skip Python's own safety checks. The header follows command framing, with magic ASCII S2AP (uint32 LE 0x50413253). version_minor is 0 in v1.0 and then packet_size SHALL be 64; a client accepts any nonzero 1.x minor version with a valid known prefix, using packet_size to skip an unknown compatible tail up to 256 bytes. A client first reads the 16-byte header and then exactly `packet_size - 16` bytes.

| Offset | Size | Field | Wire type | Unit | Valid value or range |
|---:|---:|---|---|---|---|
| 0 | 4 | magic | uint32 | — | ASCII bytes S2AP; numeric 0x50413253 |
| 4 | 2 | version_major | uint16 | — | 1 |
| 6 | 2 | version_minor | uint16 | — | 0 for this layout |
| 8 | 4 | packet_size | uint32 | bytes | 64 for v1.0; 64–256 for compatible 1.x |
| 12 | 4 | reserved_header | uint32 | — | 0 |
| 16 | 8 | last_accepted_sequence | uint64 | count | 0 before the first accepted command; otherwise its sequence |
| 24 | 8 | timestamp_ns | uint64 | ns | QPC-derived status publication time |
| 32 | 4 | state | uint32 | enum | 0 disengaged; 1 engaged; 2 manual_override_latched; 3 timeout; 4 rejected |
| 36 | 4 | reason | uint32 | enum | 0 none; 1 manual_input; 2 command_timeout; 3 malformed_packet; 4 version_mismatch; 5 client_disconnected; 6 telemetry_unavailable |
| 40 | 8 | applied_steering | float64 | ratio | Finite, [-1,1]; post-limiter |
| 48 | 8 | applied_throttle | float64 | ratio | Finite, [0,1]; post-limiter |
| 56 | 8 | applied_brake | float64 | ratio | Finite, [0,1]; post-limiter |

Status enum values are protocol integers, not localized text. The server sets actual output fields to zero in every state other than engaged. A rejected packet yields a rejected status if framing still allows a reply; a framing error then closes the connection. `telemetry_unavailable` is reserved for a native integration with independently verified telemetry availability and is not emitted by v1.0 native/input. Python derives UI TELEMETRY_UNAVAILABLE from telemetry IPC. See [UI state mapping](ui.v1.md#state-and-error-mapping).

The Python client SHALL close the connection for invalid status magic, major version, packet_size, or nonzero reserved_header. A status packet not completed within 100 ms of its first byte is a framing error and closes the connection. Unknown state or reason values, nonfinite or out-of-range applied controls, or a status timestamp that does not strictly increase, lies in the future, or is more than 250 ms old **[TUNE]** make control unavailable; Python SHALL NOT present engaged. Validate these after the complete frame and header. Unknown 1.x tail bytes are ignored only when the known prefix is valid.

## State machine

The state is server-owned; a reconnect never clears the manual latch. The table gives entry and exit conditions:

| State | Enter | Leave |
|---|---|---|
| disengaged | Startup, accepted neutral command, valid disengage, or peer disconnect | Explicit safe engage → engaged; watchdog → timeout; rejected command → rejected |
| engaged | Explicit safe 0→1 engage after neutral handshake | Valid neutral → disengaged; manual input → manual_override_latched; watchdog → timeout; rejected command → rejected |
| manual_override_latched | Manual input while engaged or while engage is pending | Only native/input clears after physical input release and a fresh neutral engage=0 command → disengaged |
| timeout | 100 ms without a fresh valid command | Fresh neutral command → disengaged; a later explicit safe engage may then enter engaged |
| rejected | Invalid command or version | Fresh neutral command after a recoverable rejection → disengaged; framing/version failure closes the connection |

Manual input while merely disengaged updates the reported physical-input condition without setting the latch. A disconnected physical device never clears a set latch. The legal (state, reason) combinations are:

| State | Legal reason values |
|---|---|
| disengaged | none; manual_input (physical input while already disengaged); client_disconnected (first status after reconnect only); telemetry_unavailable (reserved, not emitted by v1.0) |
| engaged | none |
| manual_override_latched | manual_input; client_disconnected (first status after reconnect only, while latch remains set) |
| timeout | command_timeout |
| rejected | malformed_packet; version_mismatch |

Every other pair is a protocol error; for example, (engaged, manual_input) is invalid. On reconnect, the first status packet carries retained client_disconnected reason exactly once, preserving the current state; the server then clears the retained reason to none, except that an active manual latch resumes manual_input. If the client cannot validate a legal pair, it treats control as unavailable.

## Pipe access and peer identity

The server SHALL create the pipe with `FILE_FLAG_FIRST_PIPE_INSTANCE`, `PIPE_REJECT_REMOTE_CLIENTS`, and `FILE_FLAG_OVERLAPPED`. Python opens it with `FILE_FLAG_OVERLAPPED`, `SECURITY_SQOS_PRESENT`, and `SECURITY_IDENTIFICATION`. Overlapped handles let an outstanding ReadFile coexist with WriteFile on the same pipe handle. If the name already exists, the server refuses to start and reports an error; it never attaches to a pre-existing server. Its DACL grants only the current user SID, with no access for Everyone, Anonymous, or Network. The status writer runs separately from the command reader and watchdog. Every status write has a finite 20 ms deadline **[TUNE]**; a write timeout closes the connection and zeros output. No pipe I/O may delay the independent watchdog.

After connection, native/input calls `GetNamedPipeClientProcessId` and immediately opens a process handle for that PID. It uses `QueryFullProcessImageNameW` on the handle for the image path and the process token for the SID. It opens the image file and uses `GetFinalPathNameByHandle` to resolve 8.3 names and junctions, then compares canonical paths without case sensitivity. Python performs the symmetric check with `GetNamedPipeServerProcessId`. Compare against a trusted canonical full path recorded at installation or trusted launch in a configuration location ordinary users cannot modify **[Need decision: Kingsley]**; the endpoint's process model remains [Need decision: ADR pending]. If a launcher or venv python.exe starts another interpreter process, register the actual connecting process's image path, not the launcher's. A missing PID, failed query, path/SID mismatch, or unverifiable peer closes the connection, zeros outputs, and disengages. No basename-only or peer-claimed path is sufficient. Path and SID checks are weak identifiers against other same-user programs: they cannot stop code injected into the trusted interpreter or another script run by that interpreter.

## Output limits and override

Python and native/input independently limit continuous output changes by maximum absolute change per elapsed second after strict command validation. The receiver uses its monotonic elapsed time between accepted packets, not an assumed packet rate. Initial output after engagement starts at zero. On the first engage 0→1 transition, the slew elapsed interval is zero, so applied controls remain zero; later accepted commands use time since that transition. native/input also applies an independent speed-dependent steering amplitude cap **[TUNE]**; the curve must be set by in-game testing. The v1.0 command packet carries no speed, so native/input needs a trusted speed source independent of Python or must refuse engagement until one is available **[Need decision: Kingsley/Claude]**.

native/input translates this steering convention to the virtual controller's axis direction.

| Output | Suggested maximum change per second | Status |
|---|---:|---|
| steering | 2.0 ratio/s | **[TUNE]** Kingsley confirms after in-game testing |
| throttle | 1.0 ratio/s | **[TUNE]** Kingsley confirms after in-game testing |
| brake | 2.0 ratio/s | **[TUNE]** Kingsley confirms after in-game testing; a separate faster emergency brake rise may be needed |

engage is a state transition and flags are reserved, so they have no continuous slew rate. Disengagement, manual override, malformed input, and timeout are emergency transitions: the receiver immediately sets steering, throttle, and brake outputs to zero and sets engaged=false, regardless of slew limits. A valid command received more than 100 ms after the last new valid command does not retroactively prevent a timeout; the receiver requires a new neutral packet and later explicit 0→1 engage transition before applying output again. A repeated sequence never refreshes the watchdog. Python applies the same zero/disengage rule when it cannot produce a fresh safe command.

native/input SHALL detect manual steering, throttle, or brake input independently of Python and independently of its own virtual injection device. The SDK `truck.input.*` values can include injected output and SHALL NOT alone trigger takeover. Possible physical sources include XInput or Raw Input devices (direct physical identity but device-specific behavior) and keyboard steering keys (common but may be intercepted or remapped). The source and thresholds remain **[VERIFY] [Need decision: Kingsley]**; no source is selected here. Detected manual input immediately zeros autopilot output and disengages. Set manual_override_latched only if engaged or an engage request is pending; manual input while already disengaged is reported without latching. The latch survives client disconnect and reconnect for the server process's lifetime. Only native/input may clear it after physical input is released and a fresh neutral engage=0 packet is accepted; Python flags cannot clear it. A physical device disconnect leaves the latch set or forces disengagement, never silently clears it.

The watchdog SHALL run on a timer independent of blocking pipe reads. If 100 ms passes without a new valid command, it zeros outputs and disengages even when a read is blocked. A packet not completed within 100 ms of its first byte is malformed. Every framing error closes the pipe immediately because byte-stream framing cannot be safely resynchronized. Nonzero reserved fields or unknown flags remain strict rejection conditions; any future meaning for them requires a new major version or a separately, explicitly negotiated capability.

## Connection lifecycle

1. On server start or after any disconnect, native/input sets all virtual outputs to zero and engaged=false, then listens on the named pipe. Python connects as client; both peers verify identity before accepting protocol traffic.
2. Each new connection starts a new sequence domain. Python first sends a neutral packet (engage=0, all controls zero, sequence=1). The server validates it and starts the freshness timer. It does not auto-engage from prior state.
3. Python sends fresh packets with increasing sequence and timestamp. A transition to engage=1 may activate output only after the neutral handshake, no manual input, valid telemetry, and an explicit UI request. The receiver limits continuous outputs on every accepted packet and reports the applied output in status packets.
4. If 100 ms elapses without a new valid packet, native/input zeros all outputs and disengages. Broken pipe, end of stream, malformed packet, producer crash, or failed version check does the same immediately. Closing a connection never retains the previous command.
5. Python may reconnect after a backoff **[TUNE]**. The server requires a new neutral handshake and explicit re-engagement. A reconnect never resumes the previous engaged state.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
