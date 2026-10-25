"""Control.v1 packet tests. Edited by: OpenAI-Codex_Responsible: Source_Code"""

from __future__ import annotations

import math
import struct
import unittest

from ets2ap.control_protocol import (
    COMMAND_MAGIC,
    STATUS_MAGIC,
    ControlState,
    ControlReason,
    encode_command,
    parse_status,
)


def _status_packet(
    *,
    minor: int = 0,
    packet_size: int = 64,
    reserved_header: int = 0,
    last_sequence: int = 0,
    timestamp_ns: int = 1_000_000_000,
    state: int = ControlState.DISENGAGED,
    reason: int = ControlReason.NONE,
    steering: float = 0.0,
    throttle: float = 0.0,
    brake: float = 0.0,
) -> bytes:
    data = bytearray(packet_size)
    struct.pack_into(
        "<IHHIIQQII3d",
        data,
        0,
        STATUS_MAGIC,
        1,
        minor,
        packet_size,
        reserved_header,
        last_sequence,
        timestamp_ns,
        int(state),
        int(reason),
        steering,
        throttle,
        brake,
    )
    return bytes(data)


class ControlProtocolTests(unittest.TestCase):
    def test_encodes_the_exact_v1_neutral_command_layout(self) -> None:
        packet = encode_command(1, 5_000_000_000, 0.0, 0.0, 0.0, False)

        self.assertEqual(len(packet), 64)
        fields = struct.unpack("<IHHIIQQdddB3sI", packet)
        self.assertEqual(fields, (COMMAND_MAGIC, 1, 0, 64, 0, 1,
                                  5_000_000_000, 0.0, 0.0, 0.0, 0, b"\0\0\0", 0))

    def test_rejects_invalid_or_non_neutral_disengaged_commands(self) -> None:
        invalid_commands = (
            (0, 1, 0.0, 0.0, 0.0, False),
            (1, 0, 0.0, 0.0, 0.0, False),
            (1, 1, 1.01, 0.0, 0.0, True),
            (1, 1, 0.0, math.inf, 0.0, True),
            (1, 1, 0.0, 0.2, 0.0, False),
        )
        for command in invalid_commands:
            with self.subTest(command=command), self.assertRaises(ValueError):
                encode_command(*command)

    def test_parses_valid_status_and_compatible_minor_tail(self) -> None:
        packet = _status_packet(
            minor=3,
            packet_size=68,
            last_sequence=7,
            timestamp_ns=1_000_000_000,
            state=ControlState.ENGAGED,
            steering=0.25,
            throttle=0.5,
        )
        packet = packet[:64] + b"tail"

        status = parse_status(packet, now_ns=1_000_000_000)

        self.assertEqual(status.last_accepted_sequence, 7)
        self.assertEqual(status.state, ControlState.ENGAGED)
        self.assertEqual(status.steering, 0.25)
        self.assertEqual(status.throttle, 0.5)

    def test_rejects_bad_header_values_and_invalid_state_output_pairs(self) -> None:
        invalid_packets = (
            _status_packet(reserved_header=1),
            _status_packet(state=99),
            _status_packet(state=ControlState.DISENGAGED, reason=ControlReason.COMMAND_TIMEOUT),
            _status_packet(steering=0.1),
            _status_packet(steering=math.nan),
        )
        for packet in invalid_packets:
            with self.subTest(packet=packet), self.assertRaises(ValueError):
                parse_status(packet, now_ns=1_000_000_000)

    def test_rejects_stale_future_and_non_monotonic_status_timestamps(self) -> None:
        checks = (
            (_status_packet(timestamp_ns=700_000_000), 1_000_000_000, 0),
            (_status_packet(timestamp_ns=1_000_000_001), 1_000_000_000, 0),
            (_status_packet(timestamp_ns=1_000_000_000), 1_000_000_000, 1_000_000_000),
        )
        for packet, now_ns, previous_ns in checks:
            with self.subTest(timestamp=packet[24:32]), self.assertRaises(ValueError):
                parse_status(packet, now_ns=now_ns, previous_timestamp_ns=previous_ns)


if __name__ == "__main__":
    unittest.main()
