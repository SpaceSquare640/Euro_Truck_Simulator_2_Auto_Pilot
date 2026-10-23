# Shared IPC contract

Status: v1.0 draft. This directory is the contract between the native, Python, and WPF modules. Implementations must not depend on another module's source code.

| Channel | Producer | Consumer | Transport | Specification |
|---|---|---|---|---|
| Telemetry | native/plugin | python | Windows shared memory | [telemetry v1](ipc/telemetry.v1.md) |
| Control | python | native/input | Windows named pipe | [control v1](ipc/control.v1.md) |
| UI | python and ui | ui and python | Windows named pipe | [ui v1](ipc/ui.v1.md), [JSON Schema](ipc/ui.v1.schema.json) |

## Common representation

- The supported platform is Windows 10/11 x64. Binary integers and IEEE 754 binary64 values use little-endian byte order. Binary structures use 8-byte packing. Protocol booleans are uint8 values 0 or 1, never a language-native bool.
- Physical quantities use SI units: m, m/s, rad, rad/s, and s. Normalized controls use [0,1] or [-1,1]. Engine angular speed is rad/s; SDK RPM is converted by 2π/60.
- A timestamp is an unsigned 64-bit integer count of nanoseconds from the machine's monotonic QueryPerformanceCounter (QPC) clock, using its reported frequency for conversion. It is not Unix time. The JSON representation is a base-10 string to avoid integer precision loss. Values are comparable only on the same Windows boot.
- The world frame follows the SCS Telemetry SDK frame: +X east, +Y up, +Z south (right-handed), as confirmed against the official SCS Telemetry SDK 1.16 headers. The local vehicle frame is +X right, +Y up, +Z back; the world origin is game-defined. Telemetry placement uses the world frame, while linear and angular velocity use the local frame.
- Steering and angles use counterclockwise as positive. Positive steering turns left; native/input translates this convention to the virtual controller axis.
- Any value that cannot be produced or validated is marked unavailable by the channel-specific validity mechanism. Zero is not a substitute for a valid reading.

## Versions and extensions

The version is major.minor, starting at 1.0. A reader rejects an unknown major version. A new optional field increments minor; old readers of the same major must ignore the new field. Removing a field, changing its meaning, unit, offset, type, framing, or required behavior increments major. A minor release cannot make an existing optional field mandatory.

For binary channels, append new fields after the existing layout and increase struct_size or packet_size; never move or reinterpret existing bytes. The v1.0 prefix remains readable. For JSON, add optional members and allow unknown members; a v1 reader accepts any 1.x message it otherwise understands. New message types may be ignored if they have no safety effect. Reserved bytes and flags must be zero unless a later version defines them. Safety-critical unknown flags are rejected.

To add a field:

1. Define its producer, consumer, name, type, size or JSON type, unit, range, unavailable behavior, and source. For binary data, assign an aligned offset after the current end and update the size and any validity bit.
2. Define its default when an older producer omits it; keep existing fields and semantics unchanged.
3. Increment minor and update the relevant specification and JSON Schema where applicable. State how an older reader skips it.
4. Validate a new producer with an old reader and an old producer with a new reader before release. A change that cannot satisfy both directions needs a new major.

## Safety ownership

Python applies range and rate limits before sending control. native/input independently enforces them and immediately disengages on manual input, malformed commands, or command timeout as specified in [control v1](ipc/control.v1.md). The UI displays status and requests actions; it never runs the control loop.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
