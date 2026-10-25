"""Offline checks for telemetry summary and UI framing.

Edited by: OpenAI-Codex_Responsible: Source_Code
"""

import struct
import sys
import time
import unittest

from ets2ap.telemetry import parse_snapshot
from ets2ap.ui_protocol import frame, message, parse
from ets2ap.perception_protocol import FrameFlags, PerceptionFrame, PIXEL_BYTES
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
        self.assertEqual(parse(encoded[4:])["type"], "get_status")

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

    def test_perception_status_keeps_unvalidated_gates_explicit(self):
        frame_value = PerceptionFrame(2, 1_000_000_000,
            FrameFlags.GAME_FOCUSED | FrameFlags.FRAME_VALID, bytes(PIXEL_BYTES))
        payload = perception_payload(frame_value, 1_040_000_000)
        self.assertTrue(payload["frame_valid"])
        self.assertTrue(payload["game_focused"])
        self.assertFalse(payload["input_monitor_healthy"])
        self.assertIsNone(payload["front_clear"])
        self.assertLess(payload["lane_confidence"], 0.70)

    def test_stale_perception_status_does_not_publish_lane_confidence(self):
        frame_value = PerceptionFrame(2, 1_000_000_000,
            FrameFlags.GAME_FOCUSED | FrameFlags.FRAME_VALID, bytes(PIXEL_BYTES))
        payload = perception_payload(frame_value, 1_200_000_001)
        self.assertFalse(payload["frame_valid"])
        self.assertIsNone(payload["lane_confidence"])
        self.assertIsNone(payload["front_clear"])


if __name__ == "__main__":
    unittest.main()
