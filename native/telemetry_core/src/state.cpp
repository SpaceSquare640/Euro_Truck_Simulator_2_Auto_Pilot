// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/telemetry/state.h"

#include "ets2ap/telemetry/convert.h"

#include <cmath>
#include <limits>

namespace ets2ap::telemetry {
namespace {
constexpr auto kAnyFiniteLow = std::numeric_limits<float>::lowest();
constexpr auto kAnyFiniteHigh = std::numeric_limits<float>::max();
constexpr std::uint32_t Mask(ValidityBits bit) noexcept {
    return static_cast<std::uint32_t>(bit);
}
} // namespace

void TelemetryState::Reset() noexcept {
    data_ = {};
    valid_.fill(false);
    paused_valid_ = false;
    moving_backward_ = false;
}

void TelemetryState::SetPaused(bool paused) noexcept {
    data_.paused = paused ? 1U : 0U;
    paused_valid_ = true;
}

bool TelemetryState::SetFloat(const float* value, double& target,
                              float lower, float upper) noexcept {
    if (value == nullptr || !std::isfinite(*value) || *value < lower || *value > upper) {
        target = 0.0;
        return false;
    }
    target = static_cast<double>(*value);
    return true;
}

void TelemetryState::SetSpeed(const float* value) noexcept {
    double widened = 0.0;
    valid_[Speed] = SetFloat(value, widened, kAnyFiniteLow, kAnyFiniteHigh);
    if (!valid_[Speed]) {
        data_.speed_mps = 0.0;
        data_.moving_backward = 0;
        // Contract gap: an invalid sample discards the previous hysteresis state.
        moving_backward_ = false;
        return;
    }
    data_.speed_mps = std::abs(widened);
    if (*value < -0.05F) {
        moving_backward_ = true;
    } else if (*value > -0.02F) {
        moving_backward_ = false;
    }
    data_.moving_backward = moving_backward_ ? 1U : 0U;
}

void TelemetryState::SetEngineRpm(const float* value) noexcept {
    double widened = 0.0;
    valid_[EngineRpm] = SetFloat(value, widened, 0.0F, kAnyFiniteHigh);
    data_.engine_speed_rad_s = valid_[EngineRpm] ? RpmToRadPerSec(*value) : 0.0;
}

void TelemetryState::SetDisplayedGear(const std::int32_t* value) noexcept {
    valid_[DisplayedGear] = value != nullptr;
    data_.gear_displayed = value != nullptr ? *value : 0;
}

void TelemetryState::SetEngineGear(const std::int32_t* value) noexcept {
    valid_[EngineGear] = value != nullptr;
    data_.gear_engine = value != nullptr ? *value : 0;
}

void TelemetryState::SetInputSteering(const float* value) noexcept {
    valid_[InputSteering] = SetFloat(value, data_.steering_input, -1.0F, 1.0F);
}

void TelemetryState::SetEffectiveSteering(const float* value) noexcept {
    valid_[EffectiveSteering] = SetFloat(value, data_.steering_effective, -1.0F, 1.0F);
}

void TelemetryState::SetInputThrottle(const float* value) noexcept {
    valid_[InputThrottle] = SetFloat(value, data_.throttle_input, 0.0F, 1.0F);
}

void TelemetryState::SetEffectiveThrottle(const float* value) noexcept {
    valid_[EffectiveThrottle] = SetFloat(value, data_.throttle_effective, 0.0F, 1.0F);
}

void TelemetryState::SetInputBrake(const float* value) noexcept {
    valid_[InputBrake] = SetFloat(value, data_.brake_input, 0.0F, 1.0F);
}

void TelemetryState::SetEffectiveBrake(const float* value) noexcept {
    valid_[EffectiveBrake] = SetFloat(value, data_.brake_effective, 0.0F, 1.0F);
}

void TelemetryState::SetWorldPlacement(const WorldPlacement* value) noexcept {
    valid_[World] = value != nullptr &&
                    std::isfinite(value->x) && std::isfinite(value->y) && std::isfinite(value->z) &&
                    std::isfinite(value->heading) && std::isfinite(value->pitch) && std::isfinite(value->roll) &&
                    value->pitch >= -0.25F && value->pitch <= 0.25F &&
                    value->roll >= -0.5F && value->roll <= 0.5F;
    if (!valid_[World]) {
        data_.world_x_m = data_.world_y_m = data_.world_z_m = 0.0;
        data_.heading_rad = data_.pitch_rad = data_.roll_rad = 0.0;
        return;
    }
    data_.world_x_m = value->x;
    data_.world_y_m = value->y;
    data_.world_z_m = value->z;
    data_.heading_rad = HeadingToRad(value->heading);
    data_.pitch_rad = PitchToRad(value->pitch);
    data_.roll_rad = RollToRad(value->roll);
}

void TelemetryState::SetLocalLinearVelocity(const Float3* value) noexcept {
    valid_[LinearVelocity] = value != nullptr &&
                             std::isfinite(value->x) && std::isfinite(value->y) && std::isfinite(value->z);
    data_.linear_velocity_local_x_mps = valid_[LinearVelocity] ? static_cast<double>(value->x) : 0.0;
    data_.linear_velocity_local_y_mps = valid_[LinearVelocity] ? static_cast<double>(value->y) : 0.0;
    data_.linear_velocity_local_z_mps = valid_[LinearVelocity] ? static_cast<double>(value->z) : 0.0;
}

void TelemetryState::SetLocalAngularVelocity(const Float3* value) noexcept {
    valid_[AngularVelocity] = value != nullptr &&
                              std::isfinite(value->x) && std::isfinite(value->y) && std::isfinite(value->z);
    data_.angular_velocity_local_x_rad_s = valid_[AngularVelocity] ? RevPerSecToRadPerSec(value->x) : 0.0;
    data_.angular_velocity_local_y_rad_s = valid_[AngularVelocity] ? RevPerSecToRadPerSec(value->y) : 0.0;
    data_.angular_velocity_local_z_rad_s = valid_[AngularVelocity] ? RevPerSecToRadPerSec(value->z) : 0.0;
}

void TelemetryState::SetNavigationDistance(const float* value) noexcept {
    valid_[NavigationDistance] = SetFloat(value, data_.navigation_distance_m, 0.0F, kAnyFiniteHigh);
}

void TelemetryState::SetNavigationTime(const float* value) noexcept {
    valid_[NavigationTime] = SetFloat(value, data_.navigation_time_s, 0.0F, kAnyFiniteHigh);
}

void TelemetryState::SetNavigationSpeedLimit(const float* value) noexcept {
    valid_[NavigationSpeedLimit] = SetFloat(value, data_.speed_limit_mps, 0.0F, kAnyFiniteHigh);
}

void TelemetryState::SetCruiseControl(const float* value) noexcept {
    valid_[CruiseControl] = SetFloat(value, data_.cruise_speed_mps, 0.0F, kAnyFiniteHigh);
}

TelemetryV1 TelemetryState::Build(std::uint64_t timestamp_ns) const noexcept {
    TelemetryV1 result{};
    result.magic = kMagic;
    result.version_major = kVersionMajor;
    result.version_minor = kVersionMinor;
    result.struct_size = kStructSize;
    result.timestamp_ns = timestamp_ns;
    if (paused_valid_) {
        result.validity_flags |= Mask(ValidityBits::Paused);
        result.paused = data_.paused;
    }
    if (valid_[Speed]) {
        result.validity_flags |= Mask(ValidityBits::Speed);
        result.speed_mps = data_.speed_mps;
        result.moving_backward = data_.moving_backward;
    }
    if (valid_[EngineRpm]) {
        result.validity_flags |= Mask(ValidityBits::EngineSpeed);
        result.engine_speed_rad_s = data_.engine_speed_rad_s;
    }
    if (valid_[DisplayedGear] && valid_[EngineGear]) {
        result.validity_flags |= Mask(ValidityBits::Gears);
        result.gear_displayed = data_.gear_displayed;
        result.gear_engine = data_.gear_engine;
    }
    if (valid_[InputSteering] && valid_[EffectiveSteering]) {
        result.validity_flags |= Mask(ValidityBits::Steering);
        result.steering_input = data_.steering_input;
        result.steering_effective = data_.steering_effective;
    }
    if (valid_[InputThrottle] && valid_[EffectiveThrottle]) {
        result.validity_flags |= Mask(ValidityBits::Throttle);
        result.throttle_input = data_.throttle_input;
        result.throttle_effective = data_.throttle_effective;
    }
    if (valid_[InputBrake] && valid_[EffectiveBrake]) {
        result.validity_flags |= Mask(ValidityBits::Brake);
        result.brake_input = data_.brake_input;
        result.brake_effective = data_.brake_effective;
    }
    if (valid_[World]) {
        result.validity_flags |= Mask(ValidityBits::WorldPlacement);
        result.world_x_m = data_.world_x_m;
        result.world_y_m = data_.world_y_m;
        result.world_z_m = data_.world_z_m;
        result.heading_rad = data_.heading_rad;
        result.pitch_rad = data_.pitch_rad;
        result.roll_rad = data_.roll_rad;
    }
    if (valid_[LinearVelocity]) {
        result.validity_flags |= Mask(ValidityBits::LocalLinearVelocity);
        result.linear_velocity_local_x_mps = data_.linear_velocity_local_x_mps;
        result.linear_velocity_local_y_mps = data_.linear_velocity_local_y_mps;
        result.linear_velocity_local_z_mps = data_.linear_velocity_local_z_mps;
    }
    if (valid_[AngularVelocity]) {
        result.validity_flags |= Mask(ValidityBits::LocalAngularVelocity);
        result.angular_velocity_local_x_rad_s = data_.angular_velocity_local_x_rad_s;
        result.angular_velocity_local_y_rad_s = data_.angular_velocity_local_y_rad_s;
        result.angular_velocity_local_z_rad_s = data_.angular_velocity_local_z_rad_s;
    }
    if (valid_[NavigationDistance]) {
        result.validity_flags |= Mask(ValidityBits::NavigationDistance);
        result.navigation_distance_m = data_.navigation_distance_m;
    }
    if (valid_[NavigationTime]) {
        result.validity_flags |= Mask(ValidityBits::NavigationTime);
        result.navigation_time_s = data_.navigation_time_s;
    }
    if (valid_[NavigationSpeedLimit]) {
        result.validity_flags |= Mask(ValidityBits::SpeedLimit);
        result.speed_limit_mps = data_.speed_limit_mps;
    }
    if (valid_[CruiseControl]) {
        result.validity_flags |= Mask(ValidityBits::CruiseSpeed);
        result.cruise_speed_mps = data_.cruise_speed_mps;
    }
    return result;
}

} // namespace ets2ap::telemetry
