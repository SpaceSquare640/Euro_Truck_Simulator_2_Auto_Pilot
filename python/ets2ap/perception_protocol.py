"""Bounded binary frame protocol for the local UI-to-Python perception stream.

Edited by: OpenAI-Codex_Responsible: Source_Code
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntFlag

MAGIC = b"E2PF"
VERSION_MAJOR = 1
VERSION_MINOR = 0
HEADER = struct.Struct("<4sHHIIQQHHII20s")
HEADER_SIZE = HEADER.size
WIDTH = 160
HEIGHT = 90
PIXEL_BYTES = WIDTH * HEIGHT * 3
MAX_PACKET_SIZE = HEADER_SIZE + PIXEL_BYTES
MAX_FRAME_AGE_NS = 150_000_000
PIXEL_FORMAT_RGB24 = 1
_KNOWN_FLAGS = 0xF


class FrameFlags(IntFlag):
    GAME_FOCUSED = 1 << 0
    INPUT_MONITOR_HEALTHY = 1 << 1
    MANUAL_INPUT_ACTIVE = 1 << 2
    FRAME_VALID = 1 << 3


@dataclass(frozen=True)
class PerceptionFrame:
    sequence: int
    timestamp_ns: int
    flags: FrameFlags
    rgb: bytes

    @property
    def game_focused(self) -> bool:
        return bool(self.flags & FrameFlags.GAME_FOCUSED)

    @property
    def input_monitor_healthy(self) -> bool:
        return bool(self.flags & FrameFlags.INPUT_MONITOR_HEALTHY)

    @property
    def manual_input_active(self) -> bool:
        return bool(self.flags & FrameFlags.MANUAL_INPUT_ACTIVE)

    @property
    def frame_valid(self) -> bool:
        return bool(self.flags & FrameFlags.FRAME_VALID)


def encode_frame(sequence: int, timestamp_ns: int, rgb: bytes,
                 *, flags: FrameFlags) -> bytes:
    if type(sequence) is not int or not 1 <= sequence < (1 << 64) - 1:
        raise ValueError("sequence is outside the perception v1 range")
    if type(timestamp_ns) is not int or not 1 <= timestamp_ns < (1 << 64):
        raise ValueError("timestamp_ns is outside the uint64 range")
    if type(flags) is not FrameFlags or int(flags) & ~_KNOWN_FLAGS:
        raise ValueError("unsupported perception frame flags")
    if not isinstance(rgb, bytes) or len(rgb) != PIXEL_BYTES:
        raise ValueError("perception frame must be tightly packed RGB24 at 160x90")
    return HEADER.pack(MAGIC, VERSION_MAJOR, VERSION_MINOR, HEADER_SIZE,
                       int(flags), sequence, timestamp_ns, WIDTH, HEIGHT,
                       PIXEL_FORMAT_RGB24, len(rgb), bytes(20)) + rgb


def decode_frame(packet: bytes, *, now_ns: int, previous_sequence: int = 0,
                 max_age_ns: int = MAX_FRAME_AGE_NS) -> PerceptionFrame:
    if not isinstance(packet, bytes) or len(packet) != MAX_PACKET_SIZE:
        raise ValueError("perception packet size is invalid")
    if type(now_ns) is not int or now_ns < 1:
        raise ValueError("now_ns must be a positive integer")
    if type(previous_sequence) is not int or previous_sequence < 0:
        raise ValueError("previous_sequence must be a nonnegative integer")
    if type(max_age_ns) is not int or max_age_ns < 1:
        raise ValueError("max_age_ns must be positive")
    (magic, major, minor, header_size, raw_flags, sequence, timestamp_ns,
     width, height, pixel_format, payload_size, reserved) = HEADER.unpack_from(packet)
    if magic != MAGIC or major != VERSION_MAJOR or minor != VERSION_MINOR:
        raise ValueError("perception protocol version mismatch")
    if (header_size != HEADER_SIZE or width != WIDTH or height != HEIGHT or
            pixel_format != PIXEL_FORMAT_RGB24 or payload_size != PIXEL_BYTES or
            reserved != bytes(20)):
        raise ValueError("perception frame layout is invalid")
    if raw_flags & ~_KNOWN_FLAGS:
        raise ValueError("perception frame contains unsupported flags")
    if sequence <= previous_sequence or sequence >= (1 << 64) - 1:
        raise ValueError("perception sequence did not strictly increase")
    if timestamp_ns < 1 or timestamp_ns > now_ns or now_ns - timestamp_ns > max_age_ns:
        raise ValueError("perception frame timestamp is invalid or stale")
    return PerceptionFrame(sequence, timestamp_ns, FrameFlags(raw_flags),
                           packet[HEADER_SIZE:])
