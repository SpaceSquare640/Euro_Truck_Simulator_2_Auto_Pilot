"""Control.v1 codec. Edited by: OpenAI-Codex_Responsible: Source_Code"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass
from enum import IntFlag, IntEnum

COMMAND_MAGIC = 0x50413243
STATUS_MAGIC = 0x50413253
CONTROL_MAJOR = 1
COMMAND_SIZE = 64
COMMAND_V11_SIZE = 68
STATUS_PREFIX_SIZE = 64
STATUS_V11_SIZE = 68
MAX_PACKET_SIZE = 256
MAX_STATUS_AGE_NS = 250_000_000
_COMMAND = struct.Struct("<IHHIIQQdddB3sI")
_COMMAND_SAFETY_FLAGS = struct.Struct("<I")
_STATUS = struct.Struct("<IHHIIQQII3d")
_UINT64_MAX = (1 << 64) - 1


class ControlState(IntEnum):
    DISENGAGED = 0
    ENGAGED = 1
    MANUAL_OVERRIDE_LATCHED = 2
    TIMEOUT = 3
    REJECTED = 4


class ControlReason(IntEnum):
    NONE = 0
    MANUAL_INPUT = 1
    COMMAND_TIMEOUT = 2
    MALFORMED_PACKET = 3
    VERSION_MISMATCH = 4
    CLIENT_DISCONNECTED = 5
    TELEMETRY_UNAVAILABLE = 6


class SafetyFlags(IntFlag):
    UI_AUTHORIZED = 1 << 0
    PERCEPTION_READY = 1 << 1
    INPUT_MONITOR_HEALTHY = 1 << 2
    GAME_FOCUSED = 1 << 3
    FRONT_CLEAR = 1 << 4
    MANUAL_INPUT_ACTIVE = 1 << 5


KNOWN_SAFETY_FLAGS = int(
    SafetyFlags.UI_AUTHORIZED | SafetyFlags.PERCEPTION_READY |
    SafetyFlags.INPUT_MONITOR_HEALTHY | SafetyFlags.GAME_FOCUSED |
    SafetyFlags.FRONT_CLEAR | SafetyFlags.MANUAL_INPUT_ACTIVE
)


class ControlCapabilities(IntFlag):
    SAFETY_FLAGS_V11 = 1 << 0


KNOWN_CONTROL_CAPABILITIES = int(ControlCapabilities.SAFETY_FLAGS_V11)
REQUIRED_ENGAGE_FLAGS = int(
    SafetyFlags.UI_AUTHORIZED | SafetyFlags.PERCEPTION_READY |
    SafetyFlags.INPUT_MONITOR_HEALTHY | SafetyFlags.GAME_FOCUSED |
    SafetyFlags.FRONT_CLEAR
)


@dataclass(frozen=True)
class ControlStatus:
    last_accepted_sequence: int
    timestamp_ns: int
    state: ControlState
    reason: ControlReason
    steering: float
    throttle: float
    brake: float
    capabilities: ControlCapabilities = ControlCapabilities(0)


def encode_command(sequence: int, timestamp_ns: int, steering: float,
                   throttle: float, brake: float, engage: bool,
                   safety_flags: SafetyFlags | int = 0) -> bytes:
    if isinstance(sequence, bool) or not isinstance(sequence, int):
        raise ValueError("sequence must be an integer")
    if sequence < 1 or sequence >= _UINT64_MAX:
        raise ValueError("sequence is outside the v1 range")
    if isinstance(timestamp_ns, bool) or not isinstance(timestamp_ns, int):
        raise ValueError("timestamp_ns must be an integer")
    if timestamp_ns < 1 or timestamp_ns > _UINT64_MAX:
        raise ValueError("timestamp_ns is outside the uint64 range")
    if type(engage) is not bool:
        raise ValueError("engage must be a bool")
    if isinstance(safety_flags, bool) or not isinstance(safety_flags, int):
        raise ValueError("safety_flags must be an integer bitmask")
    if safety_flags < 0 or safety_flags & ~KNOWN_SAFETY_FLAGS:
        raise ValueError("safety_flags contains unknown bits")

    values = (steering, throttle, brake)
    limits = ((-1.0, 1.0), (0.0, 1.0), (0.0, 1.0))
    normalized: list[float] = []
    for value, (minimum, maximum) in zip(values, limits):
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError("control values must be numeric")
        number = float(value)
        if not math.isfinite(number) or number < minimum or number > maximum:
            raise ValueError("control value is nonfinite or outside its range")
        normalized.append(0.0 if number == 0.0 else number)
    if not engage and any(normalized):
        raise ValueError("disengaged commands must be neutral")

    packet = bytearray(_COMMAND.pack(
        COMMAND_MAGIC,
        CONTROL_MAJOR,
        1 if safety_flags else 0,
        COMMAND_SIZE,
        0,
        sequence,
        timestamp_ns,
        *normalized,
        int(engage),
        b"\0\0\0",
        0,
    ))
    if safety_flags:
        struct.pack_into("<I", packet, 8, COMMAND_V11_SIZE)
        packet.extend(_COMMAND_SAFETY_FLAGS.pack(safety_flags))
    return bytes(packet)


def parse_status(packet: bytes, *, now_ns: int,
                 previous_timestamp_ns: int = 0) -> ControlStatus:
    if not isinstance(packet, (bytes, bytearray, memoryview)):
        raise ValueError("status packet must be bytes")
    if isinstance(now_ns, bool) or not isinstance(now_ns, int) or now_ns < 1:
        raise ValueError("now_ns must be a positive integer")
    if (isinstance(previous_timestamp_ns, bool)
            or not isinstance(previous_timestamp_ns, int)
            or previous_timestamp_ns < 0):
        raise ValueError("previous_timestamp_ns must be a nonnegative integer")
    if len(packet) < STATUS_PREFIX_SIZE or len(packet) > MAX_PACKET_SIZE:
        raise ValueError("status packet size is outside the compatible range")

    (magic, major, minor, packet_size, reserved, last_sequence, timestamp_ns,
     state_value, reason_value, steering, throttle, brake) = _STATUS.unpack_from(packet)
    if magic != STATUS_MAGIC or major != CONTROL_MAJOR:
        raise ValueError("status magic or major version mismatch")
    if packet_size != len(packet) or packet_size < STATUS_PREFIX_SIZE:
        raise ValueError("status packet_size does not match the frame")
    if minor == 0 and packet_size != STATUS_PREFIX_SIZE:
        raise ValueError("v1.0 status packets must use the fixed prefix size")
    if minor > 0 and packet_size < STATUS_V11_SIZE:
        raise ValueError("v1.1 status packet is missing its capability field")
    if reserved != 0:
        raise ValueError("status reserved header must be zero")
    raw_capabilities = (struct.unpack_from("<I", packet, STATUS_PREFIX_SIZE)[0]
                        if minor > 0 else 0)
    if raw_capabilities & ~KNOWN_CONTROL_CAPABILITIES:
        raise ValueError("status contains unknown capabilities")

    try:
        state = ControlState(state_value)
        reason = ControlReason(reason_value)
    except ValueError as error:
        raise ValueError("unknown status state or reason") from error

    legal_reasons = {
        ControlState.DISENGAGED: {
            ControlReason.NONE,
            ControlReason.MANUAL_INPUT,
            ControlReason.CLIENT_DISCONNECTED,
            ControlReason.TELEMETRY_UNAVAILABLE,
        },
        ControlState.ENGAGED: {ControlReason.NONE},
        ControlState.MANUAL_OVERRIDE_LATCHED: {
            ControlReason.MANUAL_INPUT,
            ControlReason.CLIENT_DISCONNECTED,
        },
        ControlState.TIMEOUT: {ControlReason.COMMAND_TIMEOUT},
        ControlState.REJECTED: {
            ControlReason.MALFORMED_PACKET,
            ControlReason.VERSION_MISMATCH,
        },
    }
    if reason not in legal_reasons[state]:
        raise ValueError("illegal status state/reason pair")

    ranges = ((-1.0, 1.0), (0.0, 1.0), (0.0, 1.0))
    for value, (minimum, maximum) in zip((steering, throttle, brake), ranges):
        if not math.isfinite(value) or value < minimum or value > maximum:
            raise ValueError("status control is nonfinite or outside its range")
    if state is not ControlState.ENGAGED and any((steering, throttle, brake)):
        raise ValueError("non-engaged status must report neutral outputs")
    if timestamp_ns <= previous_timestamp_ns:
        raise ValueError("status timestamp did not increase")
    if timestamp_ns > now_ns:
        raise ValueError("status timestamp is in the future")
    if now_ns - timestamp_ns > MAX_STATUS_AGE_NS:
        raise ValueError("status timestamp is stale")

    return ControlStatus(
        last_sequence,
        timestamp_ns,
        state,
        reason,
        0.0 if steering == 0.0 else steering,
        0.0 if throttle == 0.0 else throttle,
        0.0 if brake == 0.0 else brake,
        ControlCapabilities(raw_capabilities),
    )
