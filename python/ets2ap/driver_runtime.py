"""Join telemetry and perception inputs into one fail-closed driver tick.

Edited by: OpenAI-Codex_Responsible: Source_Code
"""

from __future__ import annotations

from dataclasses import dataclass

from .driver_control import DriverCommand, DriverInputs, MIN_LANE_CONFIDENCE, compute_driver_command
from .lane_perception import detect_front_clear, detect_lane
from .perception_protocol import FrameFlags, PerceptionFrame
from .telemetry import TelemetrySummary
from .control_protocol import SafetyFlags


@dataclass(frozen=True)
class DriverTick:
    command: DriverCommand
    safety_flags: SafetyFlags
    lane_confidence: float
    front_clear: bool | None


class DriverAssistRuntime:
    """Combine latest observations; front-object gating stays disabled by default."""

    def __init__(self, *, front_detector_validated: bool = False) -> None:
        self._ui_authorized = False
        self._front_detector_validated = front_detector_validated

    def authorize(self, authorized: bool) -> None:
        if type(authorized) is not bool:
            raise ValueError("authorization must be bool")
        self._ui_authorized = authorized

    def evaluate(self, telemetry: TelemetrySummary | None,
                 frame: PerceptionFrame | None, now_ns: int) -> DriverTick:
        if type(now_ns) is not int or now_ns < 1:
            raise ValueError("now_ns must be a positive integer")

        lane_offset = None
        lane_heading = None
        lane_confidence = 0.0
        capture_age = None
        front_clear: bool | None = None
        if frame is not None:
            capture_age = ((now_ns - frame.timestamp_ns) / 1_000_000_000
                           if frame.timestamp_ns <= now_ns else None)
            if frame.frame_valid and capture_age is not None and capture_age <= 0.15:
                lane = detect_lane(frame.rgb, width=160, height=90)
                lane_offset = lane.offset
                lane_heading = lane.heading_rad
                lane_confidence = lane.confidence
                if self._front_detector_validated:
                    front_clear = detect_front_clear(
                        frame.rgb, width=160, height=90,
                        lane_confidence=lane.confidence,
                    )

        flags = SafetyFlags(0)
        if self._ui_authorized:
            flags |= SafetyFlags.UI_AUTHORIZED
        if frame is not None and frame.input_monitor_healthy:
            flags |= SafetyFlags.INPUT_MONITOR_HEALTHY
        if frame is not None and frame.game_focused:
            flags |= SafetyFlags.GAME_FOCUSED
        if frame is not None and frame.manual_input_active:
            flags |= SafetyFlags.MANUAL_INPUT_ACTIVE
        if lane_confidence >= MIN_LANE_CONFIDENCE and lane_offset is not None and lane_heading is not None:
            flags |= SafetyFlags.PERCEPTION_READY
        if front_clear is True:
            flags |= SafetyFlags.FRONT_CLEAR

        inputs = DriverInputs(
            speed_mps=telemetry.speed_mps if telemetry else None,
            speed_limit_mps=telemetry.speed_limit_mps if telemetry else None,
            telemetry_age_s=telemetry.telemetry_age_s if telemetry else None,
            paused=telemetry.paused if telemetry else None,
            capture_age_s=capture_age,
            game_focused=bool(frame and frame.game_focused),
            input_monitor_healthy=bool(frame and frame.input_monitor_healthy),
            manual_input_active=bool(frame and frame.manual_input_active),
            ui_authorized=self._ui_authorized,
            lane_confidence=lane_confidence,
            lane_offset=lane_offset,
            lane_heading_rad=lane_heading,
            front_clear=front_clear,
        )
        command = compute_driver_command(inputs)
        return DriverTick(command, flags, lane_confidence, front_clear)
