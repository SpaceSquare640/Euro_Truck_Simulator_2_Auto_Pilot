// Edited by: OpenAI-Codex_Responsible: Source_Code
#pragma once

#include <cstdint>
#include <optional>

namespace ets2ap::telemetry {

double RpmToRadPerSec(float rpm) noexcept;
double RevPerSecToRadPerSec(float revolutions_per_second) noexcept;
double HeadingToRad(float heading) noexcept;
double PitchToRad(float pitch) noexcept;
double RollToRad(float roll) noexcept;
std::optional<std::uint64_t> QpcTicksToNs(std::int64_t ticks, std::int64_t frequency) noexcept;

} // namespace ets2ap::telemetry
