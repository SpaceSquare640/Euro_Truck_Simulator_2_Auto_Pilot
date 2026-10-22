# UI IPC v1

Status: v1.0 draft. Python creates \\.\pipe\ETS2AP_UI_v1 as a single-client Windows byte-stream pipe restricted to the current user and rejecting remote clients; the WPF UI connects as client. Both directions use the same framing and [draft 2020-12 JSON Schema](ui.v1.schema.json). The UI requests actions and presents localized status; Python owns the control state.

## Frame and envelope

Each frame is a 4-byte unsigned little-endian payload length followed by exactly that many bytes of UTF-8 JSON, without a BOM. The JSON root is one object. Length must be 1–65,536 bytes. A peer reads the complete prefix and payload, rejects an oversized/zero length, invalid UTF-8, invalid JSON, or invalid envelope, and closes the connection on a framing error. One frame carries exactly one message. This limit includes JSON bytes but not the 4-byte prefix.

Every message has:

| Member | Type | Meaning |
|---|---|---|
| type | string | One of the message types below |
| version | string | major.minor, beginning with 1.0; accept compatible 1.x and ignore unknown optional members |
| id | string | 1–64 ASCII letters, digits, dots, underscores, or hyphens; unique for each new request/event within one connection |
| timestamp | string | Base-10 uint64 QPC nanoseconds, with no sign or leading zero; same-boot monotonic time, never a wall-clock date |
| payload | object | Type-specific data; empty object for a message without fields |

A response uses the request's id; an unsolicited status, config, log, or error event receives a new id. The recipient does not infer ordering from id; it uses message order and timestamp. Unknown members of a known 1.x type are ignored. An unknown type is ignored unless it would be needed for a safety decision; the UI must not assume engagement from an unknown message.

## Handshake and flow

1. On connection, the UI sends hello as its first frame with supported_versions containing 1.0. Python replies with hello_ack, choosing a shared 1.x version. If no major version is shared, Python sends error with UNSUPPORTED_VERSION if the envelope can be decoded, then closes.
2. Python sends a config snapshot and status after hello_ack. The UI may request another status with get_status. Python also sends status whenever the state or summarized telemetry changes; it may coalesce high-frequency telemetry.
3. UI set_config requests an update. Python validates it and returns config with the same id containing the applied snapshot, or error with the same id. UI engage/disengage requests receive status with the same id on acceptance or error on rejection. A UI request never directly controls native/input.
4. On pipe disconnection, the UI marks state offline and reconnects. Python's safety behavior follows [control v1](control.v1.md); a reconnect starts a new hello and does not imply re-engagement. The UI must wait for fresh status before presenting an engaged state.

## Message types

| Direction | type | Required payload | Response / behavior |
|---|---|---|---|
| UI → Python | hello | supported_versions: nonempty array of major.minor strings | hello_ack or error |
| UI → Python | get_status | empty object | status with matching id |
| UI → Python | set_config | config: object with target_speed_mps | config or error with matching id |
| UI → Python | engage | empty object | status or error with matching id |
| UI → Python | disengage | empty object | status with matching id |
| Python → UI | hello_ack | selected_version: major.minor string | Completes handshake |
| Python → UI | status | state, error_code, telemetry | Current state and summary |
| Python → UI | config | config: object with target_speed_mps | Applied configuration |
| Python → UI | log | level, message_key, args | Localized diagnostic event |
| Python → UI | error | code | Rejected request or asynchronous error |

state is one of offline, ready, engaged, manual_override, or fault. status.error_code is null when there is no active error, otherwise one of the codes below. telemetry contains speed_mps, speed_limit_mps, navigation_distance_m, and telemetry_age_s. Each is a nonnegative number or null when the corresponding telemetry validity bit is clear. The summary is informational and never overrides the control safety checks.

config.target_speed_mps is a nonnegative number at most 40 m/s **[TUNE]**. Python must also cap the effective speed according to valid speed-limit and safety policy; the UI value is only a requested ceiling. A set_config request replaces this field atomically. Future 1.x config fields may be added as optional members. The current schema requires target_speed_mps in a complete config snapshot and set_config request.

log.message_key is an i18n key, and log.args is an object of scalar substitution values. The UI obtains display text from its resources. No protocol field carries hard-coded English display text. The schema validates structure and ranges; application logic checks id uniqueness, uint64 overflow, request/response matching, monotonic order, and whether a requested state transition is allowed.

## Error codes

The i18n key is the suggested WPF resource identifier, not display text. A Python producer emits the code; the UI chooses localized text.

| Code | Meaning | Suggested i18n key |
|---|---|---|
| GAME_NOT_RUNNING | ETS2 process is unavailable | error.game_not_running |
| TELEMETRY_UNAVAILABLE | Mapping or required telemetry is unavailable | error.telemetry_unavailable |
| TELEMETRY_STALE | Required telemetry exceeded its freshness limit | error.telemetry_stale |
| CONTROL_UNAVAILABLE | Native input pipe or device is unavailable | error.control_unavailable |
| MANUAL_OVERRIDE | Driver input took priority | error.manual_override |
| COMMAND_TIMEOUT | No fresh valid control command within 100 ms | error.command_timeout |
| INVALID_CONFIG | Configuration failed validation | error.invalid_config |
| UNSUPPORTED_VERSION | No compatible protocol major version | error.unsupported_version |
| MALFORMED_MESSAGE | JSON envelope or payload is invalid | error.malformed_message |
| INTERNAL_ERROR | Unclassified application failure | error.internal_error |

When a request fails, error.id matches that request. For a framing error, the peer may be unable to send an error and closes the pipe. Error codes do not themselves authorize engagement. The UI's display resources must provide localized text for every code and log key.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
