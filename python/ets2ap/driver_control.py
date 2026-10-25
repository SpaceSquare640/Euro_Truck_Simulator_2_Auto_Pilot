"""Fail-closed lane-centering and speed-control policy for the supervised prototype.

Edited by: OpenAI-Codex_Responsible: Source_Code
"""

from __future__ import annotations

import math
from dataclasses import dataclass


MAX_TARGET_SPEED_MPS = 20.0  # [TUNE] Conservative prototype cap; confirm in a parked/closed test.
DEFAULT_TARGET_SPEED_MPS = 16.0  # [TUNE] 57.6 km/h initial setpoint.
MIN_LANE_CONFIDENCE = 0.70
MAX_TELEMETRY_AGE_S = 0.25
MAX_CAPTURE_AGE_S = 0.15
MAX_STEERING_RATIO = 0.30  # [TUNE] Independent of native slew limits.


@dataclass(frozen=True)
class DriverInputs:
    speed_mps: float | None
    speed_limit_mps: float | None
    telemetry_age_s: float | None
    paused: bool | None
    capture_age_s: float | None
    game_focused: bool
    input_monitor_healthy: bool
    manual_input_active: bool
    ui_authorized: bool
    lane_confidence: float
    lane_offset: float | None
    lane_heading_rad: float | None
    front_clear: bool | None


@dataclass(frozen=True)
class DriverCommand:
    steering: float = 0.0
    throttle: float = 0.0
    brake: float = 0.0
    engage: bool = False
    reason: str = "CONTROL_UNAVAILABLE"


def _neutral(reason: str) -> DriverCommand:
    return DriverCommand(reason=reason)


def _valid_nonnegative(value: float | None) -> bool:
    return value is not None and math.isfinite(value) and value >= 0.0


def compute_driver_command(inputs: DriverInputs,
                           target_speed_mps: float = DEFAULT_TARGET_SPEED_MPS) -> DriverCommand:
    """Produce a bounded command only while every independent safety gate is ready."""
    if inputs.manual_input_active:
        return _neutral("MANUAL_OVERRIDE")
    if not inputs.game_focused or not inputs.ui_authorized:
        return _neutral("CONTROL_UNAVAILABLE")
    if not inputs.input_monitor_healthy:
        return _neutral("CONTROL_UNAVAILABLE")
    if inputs.paused is not False:
        return _neutral("TELEMETRY_UNAVAILABLE")
    if (not _valid_nonnegative(inputs.speed_mps) or
            not _valid_nonnegative(inputs.speed_limit_mps) or
            inputs.speed_limit_mps == 0.0 or
            not _valid_nonnegative(inputs.telemetry_age_s) or
            inputs.telemetry_age_s > MAX_TELEMETRY_AGE_S):
        return _neutral("TELEMETRY_STALE")
    if (not _valid_nonnegative(inputs.capture_age_s) or
            inputs.capture_age_s > MAX_CAPTURE_AGE_S or
            not math.isfinite(inputs.lane_confidence) or
            inputs.lane_confidence < MIN_LANE_CONFIDENCE or
            inputs.lane_offset is None or not math.isfinite(inputs.lane_offset) or
            inputs.lane_heading_rad is None or not math.isfinite(inputs.lane_heading_rad)):
        return _neutral("PERCEPTION_UNAVAILABLE")
    if inputs.front_clear is not True:
        return _neutral("FRONT_OBSTACLE_OR_UNKNOWN")
    if not math.isfinite(target_speed_mps) or target_speed_mps <= 0.0:
        return _neutral("INVALID_CONFIG")

    steering = max(-MAX_STEERING_RATIO,
                   min(MAX_STEERING_RATIO,
                       -0.8 * inputs.lane_offset - 0.5 * inputs.lane_heading_rad))
    speed_target = min(target_speed_mps, inputs.speed_limit_mps, MAX_TARGET_SPEED_MPS)
    speed_error = speed_target - inputs.speed_mps
    throttle = min(0.35, max(0.0, speed_error * 0.12))
    brake = min(0.45, max(0.0, -speed_error * 0.20))
    if brake > 0.0:
        throttle = 0.0
    return DriverCommand(steering, throttle, brake, True, "NONE")
