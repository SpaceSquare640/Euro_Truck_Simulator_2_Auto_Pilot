// Edited by: OpenAI-Codex_Responsible: Source_Code
#pragma once

#include "ets2ap/telemetry/layout.h"

#include <array>
#include <cstdint>

namespace ets2ap::telemetry {

struct WorldPlacement {
    double x;
    double y;
    double z;
    float heading;
    float pitch;
    float roll;
};

struct Float3 {
    float x;
    float y;
    float z;
};

class TelemetryState {
public:
    void Reset() noexcept;
    void SetPaused(bool paused) noexcept;
    void SetSpeed(const float* value) noexcept;
    void SetEngineRpm(const float* value) noexcept;
    void SetDisplayedGear(const std::int32_t* value) noexcept;
    void SetEngineGear(const std::int32_t* value) noexcept;
    void SetInputSteering(const float* value) noexcept;
    void SetEffectiveSteering(const float* value) noexcept;
    void SetInputThrottle(const float* value) noexcept;
    void SetEffectiveThrottle(const float* value) noexcept;
    void SetInputBrake(const float* value) noexcept;
    void SetEffectiveBrake(const float* value) noexcept;
    void SetWorldPlacement(const WorldPlacement* value) noexcept;
    void SetLocalLinearVelocity(const Float3* value) noexcept;
    void SetLocalAngularVelocity(const Float3* value) noexcept;
    void SetNavigationDistance(const float* value) noexcept;
    void SetNavigationTime(const float* value) noexcept;
    void SetNavigationSpeedLimit(const float* value) noexcept;
    void SetCruiseControl(const float* value) noexcept;
    TelemetryV1 Build(std::uint64_t timestamp_ns) const noexcept;

private:
    enum Channel : std::size_t {
        Speed, EngineRpm, DisplayedGear, EngineGear,
        InputSteering, EffectiveSteering, InputThrottle, EffectiveThrottle,
        InputBrake, EffectiveBrake, World, LinearVelocity, AngularVelocity,
        NavigationDistance, NavigationTime, NavigationSpeedLimit, CruiseControl,
        Count
    };

    static bool SetFloat(const float* value, double& target, float lower, float upper) noexcept;
    TelemetryV1 data_{};
    std::array<bool, Count> valid_{};
    bool paused_valid_ = false;
    bool moving_backward_ = false;
};

} // namespace ets2ap::telemetry
