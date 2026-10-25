"""Tests for conservative highway lane guidance and speed control.

Edited by: OpenAI-Codex_Responsible: Source_Code
"""

from __future__ import annotations

import unittest

from ets2ap.control_protocol import SafetyFlags
from ets2ap.driver_control import DriverInputs, compute_driver_command
from ets2ap.driver_runtime import DriverAssistRuntime
from ets2ap.lane_perception import detect_front_clear, detect_lane
from ets2ap.perception_protocol import FrameFlags, PerceptionFrame, decode_frame, encode_frame
from ets2ap.telemetry import TelemetrySummary


def _frame(*, lane_shift: int = 0, obstacle: bool = False) -> bytes:
    width, height = 160, 90
    pixels = bytearray([48, 48, 48] * width * height)
    for y in range(46, 88):
        progress = (y - 46) / 42
        half_width = round(8 + progress * 35)
        for x in (width // 2 - half_width + lane_shift,
                  width // 2 + half_width + lane_shift):
            if 0 <= x < width:
                for dx in (0, 1):
                    offset = (y * width + x + dx) * 3
                    pixels[offset:offset + 3] = b"\xe0\xe0\xe0"
    if obstacle:
        for y in range(52, 65):
            for x in range(76 + lane_shift, 84 + lane_shift):
                offset = (y * width + x) * 3
                pixels[offset:offset + 3] = b"\xa0\x90\x80"
    return bytes(pixels)


def _safe_inputs(**changes: object) -> DriverInputs:
    values: dict[str, object] = {
        "speed_mps": 8.0,
        "speed_limit_mps": 18.0,
        "telemetry_age_s": 0.05,
        "paused": False,
        "capture_age_s": 0.03,
        "game_focused": True,
        "input_monitor_healthy": True,
        "manual_input_active": False,
        "ui_authorized": True,
        "lane_confidence": 0.95,
        "lane_offset": 0.0,
        "lane_heading_rad": 0.0,
        "front_clear": True,
    }
    values.update(changes)
    return DriverInputs(**values)  # type: ignore[arg-type]


class LanePerceptionTests(unittest.TestCase):
    def test_tracks_two_converging_lane_markings_and_rightward_offset(self) -> None:
        lane = detect_lane(_frame(lane_shift=8), width=160, height=90)
        self.assertGreaterEqual(lane.confidence, 0.70)
        self.assertGreater(lane.offset, 0.0)

    def test_rejects_frame_without_two_lane_boundaries(self) -> None:
        lane = detect_lane(bytes([48, 48, 48] * 160 * 90), width=160, height=90)
        self.assertLess(lane.confidence, 0.70)
        self.assertIsNone(lane.offset)

    def test_rejects_malformed_rgb_frame(self) -> None:
        with self.assertRaises(ValueError):
            detect_lane(b"short", width=160, height=90)

    def test_front_clear_gate_rejects_a_high_contrast_object_in_lane(self) -> None:
        estimate = detect_lane(_frame(obstacle=True), width=160, height=90)
        self.assertFalse(detect_front_clear(_frame(obstacle=True), width=160, height=90,
                                            lane_confidence=estimate.confidence))

    def test_front_clear_gate_accepts_uniform_visible_road_only(self) -> None:
        estimate = detect_lane(_frame(), width=160, height=90)
        self.assertTrue(detect_front_clear(_frame(), width=160, height=90,
                                           lane_confidence=estimate.confidence))

    def test_perception_frame_round_trip_preserves_monitor_flags(self) -> None:
        pixels = _frame()
        encoded = encode_frame(1, 1_000_000_000, pixels,
                               flags=FrameFlags.GAME_FOCUSED | FrameFlags.INPUT_MONITOR_HEALTHY |
                               FrameFlags.FRAME_VALID)
        frame = decode_frame(encoded, now_ns=1_050_000_000)
        self.assertEqual(frame.sequence, 1)
        self.assertEqual(frame.rgb, pixels)
        self.assertTrue(frame.game_focused)
        self.assertTrue(frame.input_monitor_healthy)
        self.assertFalse(frame.manual_input_active)
        self.assertTrue(frame.frame_valid)

    def test_perception_frame_rejects_stale_or_replayed_input(self) -> None:
        encoded = encode_frame(2, 1_000_000_000, _frame(), flags=FrameFlags(0))
        with self.assertRaises(ValueError):
            decode_frame(encoded, now_ns=1_200_000_001)
        with self.assertRaises(ValueError):
            decode_frame(encoded, now_ns=1_050_000_000, previous_sequence=2)

    def test_perception_header_uses_aligned_seqlock_fields_and_rejects_reserved_bytes(self) -> None:
        encoded = encode_frame(2, 1_000_000_000, _frame(), flags=FrameFlags(0))
        self.assertEqual(len(encoded), 64 + 160 * 90 * 3)
        self.assertEqual(int.from_bytes(encoded[16:24], "little"), 2)
        malformed = bytearray(encoded)
        malformed[44] = 1
        with self.assertRaises(ValueError):
            decode_frame(bytes(malformed), now_ns=1_050_000_000)


class DriverControlTests(unittest.TestCase):
    def test_commands_are_neutral_until_all_safety_inputs_are_valid(self) -> None:
        for changed in (
            {"telemetry_age_s": 0.30},
            {"paused": True},
            {"capture_age_s": 0.20},
            {"game_focused": False},
            {"input_monitor_healthy": False},
            {"manual_input_active": True},
            {"ui_authorized": False},
            {"lane_confidence": 0.69},
            {"front_clear": None},
            {"front_clear": False},
            {"speed_limit_mps": None},
        ):
            with self.subTest(changed=changed):
                command = compute_driver_command(_safe_inputs(**changed))
                self.assertFalse(command.engage)
                self.assertEqual((command.steering, command.throttle, command.brake), (0, 0, 0))

    def test_steering_points_back_toward_lane_center(self) -> None:
        command = compute_driver_command(_safe_inputs(lane_offset=0.25))
        self.assertTrue(command.engage)
        self.assertLess(command.steering, 0.0)

    def test_speed_control_respects_speed_limit_and_brake_priority(self) -> None:
        speeding = compute_driver_command(_safe_inputs(speed_mps=21.0, speed_limit_mps=20.0))
        self.assertEqual(speeding.throttle, 0.0)
        self.assertGreater(speeding.brake, 0.0)


class DriverRuntimeTests(unittest.TestCase):
    def test_unvalidated_front_detector_keeps_command_neutral_even_with_clean_frame(self) -> None:
        runtime = DriverAssistRuntime()
        runtime.authorize(True)
        telemetry = TelemetrySummary(speed_mps=8.0, speed_limit_mps=18.0,
                                     navigation_distance_m=1000.0,
                                     telemetry_age_s=0.05, paused=False)
        frame = PerceptionFrame(2, 1_000_000_000,
            FrameFlags.GAME_FOCUSED | FrameFlags.INPUT_MONITOR_HEALTHY | FrameFlags.FRAME_VALID,
            _frame())

        tick = runtime.evaluate(telemetry, frame, 1_050_000_000)

        self.assertIsNone(tick.front_clear)
        self.assertFalse(tick.command.engage)
        self.assertEqual((tick.command.steering, tick.command.throttle, tick.command.brake), (0, 0, 0))
        self.assertFalse(tick.safety_flags & SafetyFlags.FRONT_CLEAR)

    def test_manual_input_is_forwarded_as_an_independent_native_gate(self) -> None:
        runtime = DriverAssistRuntime()
        runtime.authorize(True)
        frame = PerceptionFrame(2, 1_000_000_000,
            FrameFlags.GAME_FOCUSED | FrameFlags.INPUT_MONITOR_HEALTHY |
            FrameFlags.MANUAL_INPUT_ACTIVE | FrameFlags.FRAME_VALID, _frame())

        tick = runtime.evaluate(TelemetrySummary(speed_mps=8.0, speed_limit_mps=18.0,
                                                  navigation_distance_m=1000.0,
                                                  telemetry_age_s=0.05, paused=False),
                                frame, 1_050_000_000)

        self.assertFalse(tick.command.engage)
        self.assertEqual(tick.command.reason, "MANUAL_OVERRIDE")
        self.assertTrue(tick.safety_flags & SafetyFlags.MANUAL_INPUT_ACTIVE)

    def test_future_capture_timestamp_is_not_treated_as_fresh(self) -> None:
        runtime = DriverAssistRuntime()
        frame = PerceptionFrame(2, 1_100_000_000,
            FrameFlags.GAME_FOCUSED | FrameFlags.INPUT_MONITOR_HEALTHY | FrameFlags.FRAME_VALID,
            _frame())
        tick = runtime.evaluate(TelemetrySummary(speed_mps=8.0, speed_limit_mps=18.0,
                                                  navigation_distance_m=1000.0,
                                                  telemetry_age_s=0.05, paused=False),
                                frame, 1_050_000_000)
        self.assertFalse(tick.safety_flags & SafetyFlags.PERCEPTION_READY)
        self.assertFalse(tick.command.engage)


if __name__ == "__main__":
    unittest.main()
