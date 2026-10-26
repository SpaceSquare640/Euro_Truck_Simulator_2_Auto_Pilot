"""Offline checks for telemetry summary and UI framing.

Edited by: OpenAI-Codex_Responsible: Source_Code
"""

import struct
import sys
import threading
import time
import unittest
from unittest import mock

from ets2ap.telemetry import parse_snapshot
from ets2ap.control_protocol import ControlReason, ControlState, ControlStatus
from ets2ap.ui_protocol import frame, message, parse
from ets2ap.perception_protocol import FrameFlags, PerceptionFrame, PIXEL_BYTES
from ets2ap import ui_server
from ets2ap.ui_server import perception_payload, status_payload


class UiStatusTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform == "win32", "Windows QPC only")
    def test_python_clock_shares_qpc_origin(self):
        import ctypes

        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        frequency = ctypes.c_int64()
        before = ctypes.c_int64()
        after = ctypes.c_int64()
        self.assertTrue(kernel.QueryPerformanceFrequency(ctypes.byref(frequency)))
        self.assertTrue(kernel.QueryPerformanceCounter(ctypes.byref(before)))
        observed = time.perf_counter_ns()
        self.assertTrue(kernel.QueryPerformanceCounter(ctypes.byref(after)))
        lower = before.value * 1_000_000_000 // frequency.value
        upper = after.value * 1_000_000_000 // frequency.value
        self.assertLessEqual(lower - 1_000_000, observed)
        self.assertLessEqual(observed, upper + 1_000_000)

    def test_frame_round_trip(self):
        encoded = frame(message("get_status", "u-1", {}))
        self.assertEqual(struct.unpack_from("<I", encoded)[0], len(encoded) - 4)
        decoded = parse(encoded[4:])
        self.assertEqual(decoded["type"], "get_status")
        self.assertEqual(decoded["version"], "1.2")
        self.assertEqual(message("get_status", "u-2", {}, version="1.1")["version"], "1.1")

    def test_reject_duplicate_and_nonfinite(self):
        base = b'{"type":"hello","type":"status","version":"1.0","id":"u-1","timestamp":"1","payload":{}}'
        with self.assertRaises(ValueError):
            parse(base)
        with self.assertRaises(ValueError):
            parse(b'{"type":"hello","version":"1.0","id":"u-1","timestamp":"1","payload":{"x":1e999}}')

    def test_telemetry_validity_and_age(self):
        data = bytearray(240)
        data[:4] = b"E2AP"
        struct.pack_into("<HHII", data, 4, 1, 0, 240, 1 | (1 << 1) | (1 << 10) | (1 << 12))
        struct.pack_into("<Q", data, 24, 1_000_000_000)
        struct.pack_into("<d", data, 48, 10.0)
        struct.pack_into("<d", data, 208, 1250.0)
        struct.pack_into("<d", data, 224, 20.0)
        summary = parse_snapshot(data, 1_100_000_000)
        self.assertEqual((summary.speed_mps, summary.navigation_distance_m, summary.speed_limit_mps),
                         (10.0, 1250.0, 20.0))
        self.assertAlmostEqual(summary.telemetry_age_s, 0.1)
        self.assertEqual(status_payload(summary, 0)["error_code"], "CONTROL_UNAVAILABLE")
        self.assertEqual(status_payload(None, 0)["error_code"], "TELEMETRY_UNAVAILABLE")
        control = {"connection": "not_configured", "state": None, "reason": None}
        self.assertEqual(status_payload(summary, 0, control=control)["control"], control)
        self.assertNotIn("control", status_payload(summary, 0))

    def test_perception_status_keeps_unvalidated_gates_explicit(self):
        frame_value = PerceptionFrame(2, 1_000_000_000,
            FrameFlags.GAME_FOCUSED | FrameFlags.FRAME_VALID, bytes(PIXEL_BYTES))
        payload = perception_payload(frame_value, 1_040_000_000,
                                     lane_confidence=0.5, front_clear=None)
        self.assertTrue(payload["frame_valid"])
        self.assertTrue(payload["game_focused"])
        self.assertFalse(payload["input_monitor_healthy"])
        self.assertIsNone(payload["front_clear"])
        self.assertEqual(payload["lane_confidence"], 0.5)

    def test_stale_perception_status_does_not_publish_lane_confidence(self):
        frame_value = PerceptionFrame(2, 1_000_000_000,
            FrameFlags.GAME_FOCUSED | FrameFlags.FRAME_VALID, bytes(PIXEL_BYTES))
        payload = perception_payload(frame_value, 1_200_000_001)
        self.assertFalse(payload["frame_valid"])
        self.assertIsNone(payload["lane_confidence"])
        self.assertIsNone(payload["front_clear"])

    def test_control_payload_distinguishes_unconfigured_and_disconnected(self):
        build = getattr(ui_server, "control_payload", lambda *args, **kwargs: None)
        self.assertEqual(build(None, configured=False, now_ns=100), {
            "connection": "not_configured", "state": None, "reason": None,
        })
        self.assertEqual(build(None, configured=True, now_ns=100), {
            "connection": "disconnected", "state": None, "reason": None,
        })

    def test_control_payload_includes_only_fresh_native_status(self):
        build = getattr(ui_server, "control_payload", lambda *args, **kwargs: None)
        status = ControlStatus(1, 90, ControlState.ENGAGED, ControlReason.NONE,
                               0.0, 0.0, 0.0)
        self.assertEqual(build(status, configured=True, now_ns=100), {
            "connection": "connected", "state": "engaged", "reason": "none",
        })
        self.assertEqual(build(status, configured=True, now_ns=300_000_091), {
            "connection": "disconnected", "state": None, "reason": None,
        })
        self.assertEqual(build(status, configured=True, now_ns=89), {
            "connection": "disconnected", "state": None, "reason": None,
        })

    def test_control_session_neutral_heartbeat_is_independent_of_status_rate(self):
        class FastStatusClient:
            def __init__(self):
                self.status_reads = 0
                self.commands = []
                self.lock = threading.Lock()

            def connect(self, timeout_ms):
                self.send_command(0.0, 0.0, 0.0, False)

            def read_status(self):
                with self.lock:
                    self.status_reads += 1
                time.sleep(0.001)
                return None

            def send_command(self, steering, throttle, brake, engage, **kwargs):
                with self.lock:
                    self.commands.append(time.monotonic())

            def close(self):
                pass

        client = FastStatusClient()
        with mock.patch.object(ui_server, "ControlClient", return_value=client):
            session = ui_server.ControlSession("unused-for-fake-client")
            session.start()
            time.sleep(0.15)
            session.close()

        self.assertGreaterEqual(client.status_reads, 20)
        self.assertLessEqual(len(client.commands), 6)


if __name__ == "__main__":
    unittest.main()
