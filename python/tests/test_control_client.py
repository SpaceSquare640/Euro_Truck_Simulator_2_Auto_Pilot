"""Control.v1 client tests. Edited by: OpenAI-Codex_Responsible: Source_Code"""

from __future__ import annotations

import struct
import threading
import time
import unittest

from ets2ap.control_client import ControlClient, ControlClientError
from ets2ap.control_protocol import COMMAND_MAGIC, STATUS_MAGIC, ControlReason, ControlState


class _FakePipe:
    def __init__(self, *, verified: bool = True, read_delay_s: float = 0.0,
                 read_chunk_size: int | None = None,
                 block_first_read: bool = False) -> None:
        self.verified = verified
        self.read_delay_s = read_delay_s
        self.read_chunk_size = read_chunk_size
        self.block_first_read = block_first_read
        self.read_started = threading.Event()
        self.release_read = threading.Event()
        self.connected = False
        self.closed = False
        self.writes: list[bytes] = []
        self.read_buffer = bytearray()

    def connect(self, timeout_ms: int) -> None:
        self.connected = True

    def verify_server(self) -> bool:
        return self.verified

    def write(self, packet: bytes) -> None:
        self.writes.append(packet)

    def read(self, size: int, timeout_ms: int = 100) -> bytes:
        if self.block_first_read:
            self.block_first_read = False
            self.read_started.set()
            if not self.release_read.wait(timeout_ms / 1000):
                raise TimeoutError("fake pipe read timed out")
        if self.read_delay_s * 1000 > timeout_ms:
            time.sleep(timeout_ms / 1000)
            raise TimeoutError("fake pipe read timed out")
        time.sleep(self.read_delay_s)
        count = min(size, self.read_chunk_size or size)
        chunk = bytes(self.read_buffer[:count])
        del self.read_buffer[:count]
        return chunk

    def close(self) -> None:
        self.closed = True


class _AckRacePipe(_FakePipe):
    def __init__(self) -> None:
        super().__init__(block_first_read=True)
        self.status_read_finished = threading.Event()

    def write(self, packet: bytes) -> None:
        self.writes.append(packet)
        sequence = struct.unpack("<IHHIIQQdddB3sI", packet)[5]
        if sequence == 2:
            self.read_buffer.extend(_status_packet(1_002_000_000, last_accepted_sequence=2))
            self.release_read.set()
            if not self.status_read_finished.wait(1.0):
                raise TimeoutError("status reader did not consume the acknowledgement")


def _status_packet(timestamp_ns: int = 1_000_000_000,
                   last_accepted_sequence: int = 1) -> bytes:
    return struct.pack("<IHHIIQQII3d", STATUS_MAGIC, 1, 0, 64, 0,
                       last_accepted_sequence,
                       timestamp_ns, ControlState.DISENGAGED, ControlReason.NONE,
                       0.0, 0.0, 0.0)


class ControlClientTests(unittest.TestCase):
    def test_connect_verifies_server_and_sends_neutral_handshake(self) -> None:
        pipe = _FakePipe()
        client = ControlClient("trusted-server.exe", pipe=pipe,
                               now_ns=lambda: 1_000_000_000)

        client.connect()

        self.assertTrue(pipe.connected)
        self.assertEqual(len(pipe.writes), 1)
        fields = struct.unpack("<IHHIIQQdddB3sI", pipe.writes[0])
        self.assertEqual(fields[0], COMMAND_MAGIC)
        self.assertEqual(fields[5:7], (1, 1_000_000_000))
        self.assertEqual(fields[7:], (0.0, 0.0, 0.0, 0, b"\0\0\0", 0))

    def test_rejects_unverified_server_and_closes_pipe(self) -> None:
        pipe = _FakePipe(verified=False)
        client = ControlClient("trusted-server.exe", pipe=pipe,
                               now_ns=lambda: 1_000_000_000)

        with self.assertRaises(ControlClientError):
            client.connect()

        self.assertTrue(pipe.closed)
        self.assertEqual(pipe.writes, [])

    def test_commands_require_connection_and_sequence_increments(self) -> None:
        pipe = _FakePipe()
        timestamps = iter((1_000_000_000, 1_050_000_000))
        client = ControlClient("trusted-server.exe", pipe=pipe,
                               authorize_engage=lambda: True,
                               now_ns=lambda: next(timestamps))
        with self.assertRaises(ControlClientError):
            client.send_command(0.0, 0.0, 0.0, False)

        client.connect()
        client.send_command(0.25, 0.0, 0.0, True)
        self.assertEqual(len(pipe.writes), 2)
        fields = struct.unpack("<IHHIIQQdddB3sI", pipe.writes[1])
        self.assertEqual(fields[5], 2)
        self.assertEqual(fields[7:11], (0.25, 0.0, 0.0, 1))

    def test_reconnect_starts_a_fresh_sequence_domain(self) -> None:
        pipe = _FakePipe()
        timestamps = iter((1_000_000_000, 1_050_000_000, 1_100_000_000))
        client = ControlClient("trusted-server.exe", pipe=pipe,
                               now_ns=lambda: next(timestamps))

        client.connect()
        client.close()
        client.connect()

        self.assertEqual(len(pipe.writes), 2)
        self.assertEqual(struct.unpack("<IHHIIQQdddB3sI", pipe.writes[-1])[5], 1)

    def test_command_send_can_run_while_status_read_is_pending(self) -> None:
        pipe = _FakePipe(block_first_read=True)
        pipe.read_buffer.extend(_status_packet())
        clock_value = 1_000_000_000

        def now_ns() -> int:
            nonlocal clock_value
            clock_value += 1_000_000
            return clock_value

        client = ControlClient("trusted-server.exe", pipe=pipe,
                               authorize_engage=lambda: True, now_ns=now_ns)
        client.connect()
        status_results = []
        reader = threading.Thread(target=lambda: status_results.append(client.read_status()))
        reader.start()
        self.assertTrue(pipe.read_started.wait(0.5))

        self.assertEqual(client.send_command(0.25, 0.0, 0.0, True), 2)
        pipe.release_read.set()
        reader.join(0.5)

        self.assertFalse(reader.is_alive())
        self.assertEqual(status_results[0].state, ControlState.DISENGAGED)

    def test_ack_for_command_being_written_is_not_rejected_as_unsent(self) -> None:
        pipe = _AckRacePipe()
        timestamps = iter((1_000_000_000, 1_001_000_000, 1_002_000_000))
        client = ControlClient("trusted-server.exe", pipe=pipe,
                               authorize_engage=lambda: True,
                               now_ns=lambda: next(timestamps))
        client.connect()
        status_results = []
        status_errors = []

        def read_status() -> None:
            try:
                status_results.append(client.read_status())
            except ControlClientError as error:
                status_errors.append(error)
            finally:
                pipe.status_read_finished.set()

        reader = threading.Thread(target=read_status)
        reader.start()
        self.assertTrue(pipe.read_started.wait(0.5))
        writer = threading.Thread(target=lambda: client.send_command(0.25, 0.0, 0.0, True))
        writer.start()
        writer.join(1.0)
        reader.join(1.0)

        self.assertFalse(writer.is_alive())
        self.assertFalse(reader.is_alive())
        self.assertEqual(status_errors, [])
        self.assertEqual(len(status_results), 1)
        self.assertEqual(status_results[0].last_accepted_sequence, 2)

    def test_engagement_is_denied_without_an_explicit_authorizer(self) -> None:
        pipe = _FakePipe()
        client = ControlClient("trusted-server.exe", pipe=pipe, now_ns=lambda: 1_000_000_000)
        client.connect()

        with self.assertRaises(ControlClientError):
            client.send_command(0.0, 0.0, 0.0, True)

        self.assertEqual(len(pipe.writes), 1)

    def test_reads_and_validates_status_frame(self) -> None:
        pipe = _FakePipe()
        pipe.read_buffer.extend(_status_packet())
        client = ControlClient("trusted-server.exe", pipe=pipe, now_ns=lambda: 1_000_000_000)
        client.connect()

        status = client.read_status()

        self.assertEqual(status.state, ControlState.DISENGAGED)
        self.assertEqual(status.last_accepted_sequence, 1)

    def test_rejects_status_frame_not_completed_within_100ms(self) -> None:
        pipe = _FakePipe(read_delay_s=0.03, read_chunk_size=8)
        pipe.read_buffer.extend(_status_packet())
        client = ControlClient("trusted-server.exe", pipe=pipe, now_ns=lambda: 1_000_000_000)
        client.connect()

        started = time.monotonic()
        with self.assertRaises(ControlClientError):
            client.read_status()

        self.assertLess(time.monotonic() - started, 0.2)
        self.assertTrue(pipe.closed)


if __name__ == "__main__":
    unittest.main()
