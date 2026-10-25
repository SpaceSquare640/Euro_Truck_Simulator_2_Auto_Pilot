# Control IPC v1

Status: v1.1 draft. Python is the client and native/input is the server of \\.\pipe\ETS2AP_Control_v1. This single-client, full-duplex Windows byte stream carries commands from Python to native/input and status packets in the opposite direction. native/input owns the virtual-controller output and remains safe when no client is connected. v1.0 packets remain valid for neutral handshake and status compatibility; v1.1 adds safety claims and a capability signal.

## Command packet and framing

Each v1.0 command is exactly 64 bytes. The v1.1 command appends a 4-byte safety_flags field at offset 64, making its known prefix 68 bytes. The header contains packet_size so a later 1.x packet can append fields while preserving the known prefix. The receiver reads the 16-byte initial header, validates packet_size (64–256 bytes), then reads exactly the remaining bytes of that packet. A minor-0 packet is exactly 64 bytes; a minor-1-or-later packet must be at least 68 bytes. Readers skip only tails beyond the prefix they understand. All multi-byte values are little-endian; float64 is IEEE 754 binary64. The field layout follows 8-byte packing; padding and reserved fields are zero.

| Offset | Size | Field | Wire type | Unit | Valid value or range |
|---:|---:|---|---|---|---|
| 0 | 4 | magic | uint32 | — | ASCII bytes C2AP; numeric 0x50413243 |
| 4 | 2 | version_major | uint16 | — | 1 |
| 6 | 2 | version_minor | uint16 | — | 0 for v1.0, 1 for the safety-claim extension |
| 8 | 4 | packet_size | uint32 | bytes | 64 for v1.0; 68–256 for v1.1 |
| 12 | 4 | reserved_header | uint32 | — | 0 |
| 16 | 8 | sequence | uint64 | count | Strictly increasing within a connection; first is 1; 0 and 2^64-1 invalid |
| 24 | 8 | timestamp_ns | uint64 | ns | QPC-derived monotonic command creation time |
| 32 | 8 | steering | float64 | ratio | Finite, [-1,1]; positive left (counterclockwise), negative right |
| 40 | 8 | throttle | float64 | ratio | Finite, [0,1] |
| 48 | 8 | brake | float64 | ratio | Finite, [0,1] |
| 56 | 1 | engage | uint8 | boolean | 0 or 1 |
| 57 | 3 | reserved_0 | 3 bytes | — | Zero |
| 60 | 4 | flags | uint32 | bitmask | Reserved; must be zero |
| 64 | 4 | safety_flags | uint32 | bitmask | Present in v1.1; bits 0–5 defined below; unsupported bits reject the command |

Safety flag bits are claims from the authenticated Python client, not substitutes for checks the native receiver can perform locally:

| Bit | Name | Meaning |
|---:|---|---|
| 0 | UI_AUTHORIZED | The Python runtime has a current explicit UI authorization |
| 1 | PERCEPTION_READY | Fresh lane estimate passed the configured confidence and validity checks |
| 2 | INPUT_MONITOR_HEALTHY | A live input-monitor health observation is available |
| 3 | GAME_FOCUSED | ETS2 was the foreground game window for the accepted perception frame |
| 4 | FRONT_CLEAR | The front-area detector reports clear; this bit must remain clear while that detector is unvalidated |
| 5 | MANUAL_INPUT_ACTIVE | Manual input is currently active; setting this bit forces neutral output and latches override |

An engaged v1.1 command requires bits 0–4 and requires bit 5 to be clear. A v1.0 receiver may ignore the unknown v1.1 tail, so a v1.1 sender MUST first receive a fresh v1.1 status advertising `SAFETY_FLAGS_V1_1`; otherwise it MUST NOT send an engaged command. A v1.1 receiver MUST refuse engagement when required claims are absent. This negotiation is a compatibility guard, not evidence that the detector or input monitor is accurate. The current native host does not publish an independent physical-input-monitor health gate, so engagement remains blocked until that gate is implemented and validated.

The receiver SHALL reject an unknown major, bad size or magic, nonzero reserved bytes or unsupported flags, NaN, ±Infinity, out-of-range controls, repeated/out-of-order sequence, or an invalid timestamp. It SHALL NOT clamp malformed inputs into range. Treat -0.0 as 0.0. If engage=0, all three control values SHALL be zero. If throttle and brake are both positive in an otherwise valid engaged command, brake takes priority and effective throttle is set to zero before slew limiting: this prevents propulsion against a requested stop. Only the slew limiter may clamp an otherwise valid output. Any rejection zeros output and disengages.

The first command on a connection SHALL have sequence=1, engage=0, and neutral controls. Every later accepted sequence and timestamp SHALL be strictly greater than the previous accepted value; sequence 0 and 2^64-1 are invalid, so the connection must be renewed before exhaustion. Timestamps use the [QPC conversion](telemetry.v1.md#time-and-publication). A timestamp in the future has zero tolerance because peers use the same machine QPC clock; a command older than 100 ms is rejected. The sender sends a new packet at least every 50 ms while engaged **[TUNE]**; repetitions need new sequence and timestamp values.

## Status packet (native/input to Python)

The server sends a 68-byte v1.1 status packet immediately whenever its state or reason changes and at a 20 Hz interval while connected **[TUNE]**. The status is a post-limiter observation, never an authorization to skip Python's own safety checks. The header follows command framing, with magic ASCII S2AP (uint32 LE 0x50413253). A compatible v1.0 status packet is exactly 64 bytes; v1.1 adds a 4-byte capabilities field at offset 64. A client accepts newer 1.x packets only when the known prefix is present and uses packet_size to skip unknown compatible tails up to 256 bytes. A client first reads the 16-byte header and then exactly `packet_size - 16` bytes.

| Offset | Size | Field | Wire type | Unit | Valid value or range |
|---:|---:|---|---|---|---|
| 0 | 4 | magic | uint32 | — | ASCII bytes S2AP; numeric 0x50413253 |
| 4 | 2 | version_major | uint16 | — | 1 |
| 6 | 2 | version_minor | uint16 | — | 0 for v1.0, 1 for the capability extension |
| 8 | 4 | packet_size | uint32 | bytes | 64 for v1.0; 68–256 for v1.1 |
| 12 | 4 | reserved_header | uint32 | — | 0 |
| 16 | 8 | last_accepted_sequence | uint64 | count | 0 before the first accepted command; otherwise its sequence |
| 24 | 8 | timestamp_ns | uint64 | ns | QPC-derived status publication time |
| 32 | 4 | state | uint32 | enum | 0 disengaged; 1 engaged; 2 manual_override_latched; 3 timeout; 4 rejected |
| 36 | 4 | reason | uint32 | enum | 0 none; 1 manual_input; 2 command_timeout; 3 malformed_packet; 4 version_mismatch; 5 client_disconnected; 6 telemetry_unavailable |
| 40 | 8 | applied_steering | float64 | ratio | Finite, [-1,1]; post-limiter |
| 48 | 8 | applied_throttle | float64 | ratio | Finite, [0,1]; post-limiter |
| 56 | 8 | applied_brake | float64 | ratio | Finite, [0,1]; post-limiter |
| 64 | 4 | capabilities | uint32 | bitmask | Present in v1.1; bit 0 means the server parses v1.1 safety claims and gates engagement on them |

Status capability bit 0 is `SAFETY_FLAGS_V1_1`. It confirms protocol behavior only; it does not confirm that the local input monitor, perception algorithm, or game capture has passed validation. Unknown capability bits are rejected.

Status enum values are protocol integers, not localized text. The server sets actual output fields to zero in every state other than engaged. A rejected packet yields a rejected status if framing still allows a reply; a framing error then closes the connection. `telemetry_unavailable` is reserved for a native integration with independently verified telemetry availability and is not emitted by v1.1 native/input. Python derives UI TELEMETRY_UNAVAILABLE from telemetry IPC. See [UI state mapping](ui.v1.md#state-and-error-mapping).

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
| disengaged | none; manual_input (physical input while already disengaged); client_disconnected (first status after reconnect only); telemetry_unavailable (reserved, not emitted by v1.1) |
| engaged | none |
| manual_override_latched | manual_input; client_disconnected (first status after reconnect only, while latch remains set) |
| timeout | command_timeout |
| rejected | malformed_packet; version_mismatch |

Every other pair is a protocol error; for example, (engaged, manual_input) is invalid. On reconnect, the first status packet carries retained client_disconnected reason exactly once, preserving the current state; the server then clears the retained reason to none, except that an active manual latch resumes manual_input. If the client cannot validate a legal pair, it treats control as unavailable.

## Pipe access and peer identity

The server SHALL create the pipe with `FILE_FLAG_FIRST_PIPE_INSTANCE`, `PIPE_REJECT_REMOTE_CLIENTS`, and `FILE_FLAG_OVERLAPPED`. Python opens it with `FILE_FLAG_OVERLAPPED`, `SECURITY_SQOS_PRESENT`, and `SECURITY_IDENTIFICATION`. Overlapped handles let an outstanding ReadFile coexist with WriteFile on the same pipe handle. If the name already exists, the server refuses to start and reports an error; it never attaches to a pre-existing server. Its DACL grants only the current user SID, with no access for Everyone, Anonymous, or Network. The status writer runs separately from the command reader and watchdog. Every status write has a finite 20 ms deadline **[TUNE]**; a write timeout closes the connection and zeros output. No pipe I/O may delay the independent watchdog.

After connection, native/input calls `GetNamedPipeClientProcessId` and immediately opens a process handle for that PID. It uses `QueryFullProcessImageNameW` on the handle for the image path and the process token for the SID. It opens the image file and uses `GetFinalPathNameByHandle` to resolve 8.3 names and junctions, then compares canonical paths without case sensitivity. Python performs the symmetric check with `GetNamedPipeServerProcessId`. Compare against a trusted canonical full path recorded at installation or trusted launch in a configuration location ordinary users cannot modify **[Need decision: Kingsley]**; the endpoint's process model remains [Need decision: ADR pending]. If a launcher or venv python.exe starts another interpreter process, register the actual connecting process's image path, not the launcher's. A missing PID, failed query, path/SID mismatch, or unverifiable peer closes the connection, zeros outputs, and disengages. No basename-only or peer-claimed path is sufficient. Path and SID checks are weak identifiers against other same-user programs: they cannot stop code injected into the trusted interpreter or another script run by that interpreter.

## Output limits and override

Python and native/input independently limit continuous output changes by maximum absolute change per elapsed second after strict command validation. The receiver uses its monotonic elapsed time between accepted packets, not an assumed packet rate. Initial output after engagement starts at zero. On the first engage 0→1 transition, the slew elapsed interval is zero, so applied controls remain zero; later accepted commands use time since that transition. native/input also applies a fixed independent steering amplitude cap of 0.30. A speed-dependent steering curve is not implemented and remains **[TUNE]**; it must be calibrated before any driving-control release. The v1.0 command packet carries no speed, so native/input requires the local trusted telemetry/speed-limit callback; without it, engagement remains blocked.

native/input translates this steering convention to the virtual controller's axis direction.

| Output | Suggested maximum change per second | Status |
|---|---:|---|
| steering | 2.0 ratio/s | **[TUNE]** Kingsley confirms after in-game testing |
| throttle | 1.0 ratio/s | **[TUNE]** Kingsley confirms after in-game testing |
| brake | 2.0 ratio/s | **[TUNE]** Kingsley confirms after in-game testing; a separate faster emergency brake rise may be needed |

engage is a state transition and safety flags are claims, so they have no continuous slew rate. Disengagement, manual override, malformed input, and timeout are emergency transitions: the receiver immediately sets steering, throttle, and brake outputs to zero and sets engaged=false, regardless of slew limits. A valid command received more than 100 ms after the last new valid command does not retroactively prevent a timeout; the receiver requires a new neutral packet and later explicit 0→1 engage transition before applying output again. A repeated sequence never refreshes the watchdog. Python applies the same zero/disengage rule when it cannot produce a fresh safe command.

native/input SHALL detect manual steering, throttle, or brake input independently of Python and independently of its own virtual injection device. The SDK `truck.input.*` values can include injected output and SHALL NOT alone trigger takeover. Possible physical sources include XInput or Raw Input devices (direct physical identity but device-specific behavior) and keyboard steering keys (common but may be intercepted or remapped). The source and thresholds remain **[VERIFY]**. Detected manual input immediately zeros autopilot output and disengages. Set manual_override_latched only if engaged or an engage request is pending; manual input while already disengaged is reported without latching. The latch survives client disconnect and reconnect for the server process's lifetime. Only native/input may clear it after a healthy monitor confirms physical input is released and a fresh neutral engage=0 packet is accepted; a Python claim alone cannot clear it. A physical device disconnect leaves the latch set or forces disengagement, never silently clears it. The current plugin has not connected a physical-input monitor to its local safety callback, so its input-monitor gate remains false and engagement is blocked.

The watchdog SHALL run on a timer independent of blocking pipe reads. If 100 ms passes without a new valid command, it zeros outputs and disengages even when a read is blocked. A packet not completed within 100 ms of its first byte is malformed. Every framing error closes the pipe immediately because byte-stream framing cannot be safely resynchronized. Nonzero reserved fields or unknown flags remain strict rejection conditions; any future meaning for them requires a new major version or a separately, explicitly negotiated capability.

## Connection lifecycle

1. On server start or after any disconnect, native/input sets all virtual outputs to zero and engaged=false, then listens on the named pipe. Python connects as client; both peers verify identity before accepting protocol traffic.
2. Each new connection starts a new sequence domain. Python first sends a neutral v1.0 packet (engage=0, all controls zero, sequence=1). The server validates it and starts the freshness timer. It does not auto-engage from prior state.
3. Before any engage request, Python reads and validates a fresh status packet. It may send an engaged v1.1 command only when the status advertises `SAFETY_FLAGS_V1_1`, the Python authorization callback is true, all required claims are present, and manual-input-active is clear. A v1.0 server status never authorizes an engage command. The receiver applies its local telemetry/speed limits, manual-input latch, and every claimed safety gate before output can change.
4. If 100 ms elapses without a new valid packet, native/input zeros all outputs and disengages. Broken pipe, end of stream, malformed packet, producer crash, or failed version check does the same immediately. Closing a connection never retains the previous command.
5. Python may reconnect after a backoff **[TUNE]**. The server requires a new neutral handshake and explicit re-engagement. A reconnect never resumes the previous engaged state.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
