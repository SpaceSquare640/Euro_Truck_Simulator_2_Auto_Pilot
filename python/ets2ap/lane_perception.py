"""Conservative RGB lane-marking estimator used by the supervised assist prototype.

Edited by: OpenAI-Codex_Responsible: Source_Code
"""

from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass(frozen=True)
class LaneEstimate:
    """Image-relative lane estimate; offset is positive when lane center is right."""

    offset: float | None
    heading_rad: float | None
    confidence: float


_SCAN_FRACTIONS = (0.56, 0.61, 0.66, 0.71, 0.76, 0.81, 0.86)
_MIN_MARKING_LUMA = 150


def _is_marking(red: int, green: int, blue: int) -> bool:
    luma = (red * 299 + green * 587 + blue * 114) // 1000
    white = luma >= _MIN_MARKING_LUMA and max(red, green, blue) - min(red, green, blue) <= 90
    yellow = red >= 145 and green >= 90 and blue <= 125 and red > green
    return white or yellow


def _runs(rgb: bytes, width: int, y: int, start: int, end: int) -> list[tuple[float, float]]:
    row = y * width * 3
    runs: list[tuple[float, float]] = []
    active: list[int] = []
    for x in range(start, end):
        pixel = row + x * 3
        if _is_marking(rgb[pixel], rgb[pixel + 1], rgb[pixel + 2]):
            active.append(x)
        elif active:
            if len(active) <= 8:
                luma = sum((rgb[row + index * 3] * 299 + rgb[row + index * 3 + 1] * 587 +
                            rgb[row + index * 3 + 2] * 114) // 1000 for index in active)
                runs.append(((active[0] + active[-1]) / 2, luma / (255 * len(active))))
            active.clear()
    if active and len(active) <= 8:
        luma = sum((rgb[row + index * 3] * 299 + rgb[row + index * 3 + 1] * 587 +
                    rgb[row + index * 3 + 2] * 114) // 1000 for index in active)
        runs.append(((active[0] + active[-1]) / 2, luma / (255 * len(active))))
    return runs


def _best_boundary(candidates: list[tuple[float, float]], expected: float,
                   max_distance: float, width: int) -> tuple[float, float] | None:
    eligible = [(x, score) for x, score in candidates if abs(x - expected) <= max_distance]
    if not eligible:
        return None
    x, brightness = max(eligible, key=lambda candidate: candidate[1] -
                        abs(candidate[0] - expected) / width)
    return x, brightness


def detect_lane(rgb: bytes, *, width: int, height: int) -> LaneEstimate:
    """Estimate lane alignment from a tightly packed RGB24 frame.

    The detector intentionally returns low confidence for dark, obscured, or
    ambiguous markings; callers must keep outputs neutral below 0.70.
    """
    if (type(width) is not int or type(height) is not int or
            not 80 <= width <= 640 or not 60 <= height <= 360 or
            not isinstance(rgb, bytes) or len(rgb) != width * height * 3):
        raise ValueError("invalid RGB frame dimensions or payload")

    center = width / 2
    left_points: list[tuple[int, float, float]] = []
    right_points: list[tuple[int, float, float]] = []
    for fraction in _SCAN_FRACTIONS:
        y = round(height * fraction)
        progress = (fraction - _SCAN_FRACTIONS[0]) / (_SCAN_FRACTIONS[-1] - _SCAN_FRACTIONS[0])
        expected_half_width = width * (0.07 + 0.22 * progress)
        max_distance = width * 0.13
        row = _runs(rgb, width, y, 0, width)
        left = _best_boundary([(x, score) for x, score in row if x < center - 3],
                              center - expected_half_width, max_distance, width)
        right = _best_boundary([(x, score) for x, score in row if x > center + 3],
                               center + expected_half_width, max_distance, width)
        if left is not None:
            left_points.append((y, left[0], left[1]))
        if right is not None:
            right_points.append((y, right[0], right[1]))

    right_by_y = {y: (right_x, score) for y, right_x, score in right_points}
    valid: list[tuple[int, float, float, float]] = []
    for y, left_x, left_score in left_points:
        right = right_by_y.get(y)
        if right is None:
            continue
        right_x, right_score = right
        if left_x < right_x:
            valid.append((y, left_x, right_x, (left_score + right_score) / 2))
    if len(valid) < 5:
        return LaneEstimate(None, None, len(valid) / len(_SCAN_FRACTIONS) * 0.5)

    widths = [right_x - left_x for _, left_x, right_x, _ in valid]
    if any(not width * 0.12 <= lane_width <= width * 0.72 for lane_width in widths):
        return LaneEstimate(None, None, 0.0)

    centers = [(y, (left_x + right_x) / 2) for y, left_x, right_x, _ in valid]
    weighted_center = sum(center_x for _, center_x in centers) / len(centers)
    mean_lane_width = sum(widths) / len(widths)
    offset = (weighted_center - center) / (mean_lane_width / 2)
    if not math.isfinite(offset) or abs(offset) > 1.5:
        return LaneEstimate(None, None, 0.0)

    first_y, first_center = centers[0]
    last_y, last_center = centers[-1]
    heading = math.atan((last_center - first_center) / max(1, last_y - first_y))
    monotonic = sum(later >= earlier for earlier, later in zip(widths, widths[1:]))
    width_progress = monotonic / max(1, len(widths) - 1)
    brightness = sum(score for _, _, _, score in valid) / len(valid)
    coverage = len(valid) / len(_SCAN_FRACTIONS)
    confidence = min(1.0, 0.55 * coverage + 0.25 * width_progress + 0.20 * brightness)
    return LaneEstimate(offset, heading, confidence)


def detect_front_clear(rgb: bytes, *, width: int, height: int,
                       lane_confidence: float) -> bool | None:
    """Conservatively flag texture/edge activity in the near, centered lane ROI.

    This is a prototype occupancy heuristic, not a trained vehicle detector.
    Callers must treat None as unsafe and verify this behavior in-game before
    enabling any release build.
    """
    if (type(width) is not int or type(height) is not int or
            not 80 <= width <= 640 or not 60 <= height <= 360 or
            not isinstance(rgb, bytes) or len(rgb) != width * height * 3):
        raise ValueError("invalid RGB frame dimensions or payload")
    if not math.isfinite(lane_confidence) or lane_confidence < 0.70:
        return None

    x_start, x_end = round(width * 0.42), round(width * 0.58)
    y_start, y_end = round(height * 0.63), round(height * 0.82)

    def luma(x: int, y: int) -> int:
        index = (y * width + x) * 3
        return (rgb[index] * 299 + rgb[index + 1] * 587 + rgb[index + 2] * 114) // 1000

    samples = [luma(x, y) for y in range(y_start, y_end) for x in range(x_start, x_end)]
    average_luma = sum(samples) / len(samples)
    if not 35.0 <= average_luma <= 205.0:
        return None

    edge_count = 0
    edge_samples = 0
    for y in range(y_start + 1, y_end - 1):
        for x in range(x_start + 1, x_end - 1):
            horizontal = abs(luma(x + 1, y) - luma(x - 1, y))
            vertical = abs(luma(x, y + 1) - luma(x, y - 1))
            if horizontal + vertical > 40:
                edge_count += 1
            edge_samples += 1
    return edge_count / max(1, edge_samples) <= 0.025
