// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/telemetry/convert.h"

#include <cmath>
#include <intrin.h>
#include <limits>
#include <numbers>

namespace ets2ap::telemetry {
namespace {
constexpr double kTau = 2.0 * std::numbers::pi_v<double>;
constexpr std::uint64_t kNsPerSecond = 1'000'000'000ULL;
}

double RpmToRadPerSec(float rpm) noexcept {
    return static_cast<double>(rpm) * kTau / 60.0;
}

double RevPerSecToRadPerSec(float revolutions_per_second) noexcept {
    return static_cast<double>(revolutions_per_second) * kTau;
}

double HeadingToRad(float heading) noexcept {
    double revolutions = std::fmod(static_cast<double>(heading), 1.0);
    if (revolutions < 0.0) {
        revolutions += 1.0;
    }
    const double radians = revolutions * kTau;
    return radians >= kTau || radians == 0.0 ? 0.0 : radians;
}

double PitchToRad(float pitch) noexcept {
    return static_cast<double>(pitch) * kTau;
}

double RollToRad(float roll) noexcept {
    return static_cast<double>(roll) * kTau;
}

std::optional<std::uint64_t> QpcTicksToNs(std::int64_t ticks, std::int64_t frequency) noexcept {
    if (ticks < 0 || frequency <= 0) {
        return std::nullopt;
    }
    const auto divisor = static_cast<std::uint64_t>(frequency);
    const auto whole_seconds = static_cast<std::uint64_t>(ticks) / divisor;
    const auto remainder_ticks = static_cast<std::uint64_t>(ticks) % divisor;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (whole_seconds > maximum / kNsPerSecond) {
        return std::nullopt;
    }

    // The remainder product can overflow uint64. MSVC's 128-bit intrinsics
    // preserve the exact integer division required by the wire contract.
    std::uint64_t high = 0;
    const std::uint64_t low = _umul128(remainder_ticks, kNsPerSecond, &high);
    std::uint64_t ignored_remainder = 0;
    const std::uint64_t fractional_ns = _udiv128(high, low, divisor, &ignored_remainder);
    const std::uint64_t whole_ns = whole_seconds * kNsPerSecond;
    if (fractional_ns > maximum - whole_ns) {
        return std::nullopt;
    }
    return whole_ns + fractional_ns;
}

} // namespace ets2ap::telemetry
