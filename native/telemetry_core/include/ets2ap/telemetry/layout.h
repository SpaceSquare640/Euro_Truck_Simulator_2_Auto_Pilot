// Edited by: OpenAI-Codex_Responsible: Source_Code
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ets2ap::telemetry {

inline constexpr std::uint32_t kMagic = 0x50413245U;
inline constexpr std::uint16_t kVersionMajor = 1U;
inline constexpr std::uint16_t kVersionMinor = 0U;
inline constexpr std::uint32_t kStructSize = 240U;
inline constexpr std::size_t kMappingSize = 4096U;
inline constexpr wchar_t kMappingName[] = L"Local\\ETS2AP_Telemetry_v1";
inline constexpr wchar_t kMutexName[] = L"Local\\ETS2AP_Telemetry_v1.lock";

enum class ValidityBits : std::uint32_t {
    Paused = 1U << 0,
    Speed = 1U << 1,
    EngineSpeed = 1U << 2,
    Gears = 1U << 3,
    Steering = 1U << 4,
    Throttle = 1U << 5,
    Brake = 1U << 6,
    WorldPlacement = 1U << 7,
    LocalLinearVelocity = 1U << 8,
    LocalAngularVelocity = 1U << 9,
    NavigationDistance = 1U << 10,
    NavigationTime = 1U << 11,
    SpeedLimit = 1U << 12,
    CruiseSpeed = 1U << 13,
};
inline constexpr std::uint32_t kKnownValidityMask = 0x3FFFU;

#pragma pack(push, 8)
struct TelemetryV1 {
    std::uint32_t magic;
    std::uint16_t version_major;
    std::uint16_t version_minor;
    std::uint32_t struct_size;
    std::uint32_t validity_flags;
    std::uint64_t sequence;
    std::uint64_t timestamp_ns;
    std::uint8_t paused;
    std::uint8_t moving_backward;
    std::uint16_t reserved_0;
    std::int32_t gear_displayed;
    std::int32_t gear_engine;
    std::uint32_t reserved_1;
    double speed_mps;
    double engine_speed_rad_s;
    double steering_input;
    double steering_effective;
    double throttle_input;
    double throttle_effective;
    double brake_input;
    double brake_effective;
    double world_x_m;
    double world_y_m;
    double world_z_m;
    double heading_rad;
    double pitch_rad;
    double roll_rad;
    double linear_velocity_local_x_mps;
    double linear_velocity_local_y_mps;
    double linear_velocity_local_z_mps;
    double angular_velocity_local_x_rad_s;
    double angular_velocity_local_y_rad_s;
    double angular_velocity_local_z_rad_s;
    double navigation_distance_m;
    double navigation_time_s;
    double speed_limit_mps;
    double cruise_speed_mps;
};
#pragma pack(pop)

static_assert(std::is_standard_layout_v<TelemetryV1>);
static_assert(sizeof(TelemetryV1) == kStructSize);
static_assert(alignof(TelemetryV1) == 8);
#define ETS2AP_ASSERT_FIELD(field, offset, bytes) \
    static_assert(offsetof(TelemetryV1, field) == offset); \
    static_assert(sizeof(TelemetryV1::field) == bytes)
ETS2AP_ASSERT_FIELD(magic, 0, 4);
ETS2AP_ASSERT_FIELD(version_major, 4, 2);
ETS2AP_ASSERT_FIELD(version_minor, 6, 2);
ETS2AP_ASSERT_FIELD(struct_size, 8, 4);
ETS2AP_ASSERT_FIELD(validity_flags, 12, 4);
ETS2AP_ASSERT_FIELD(sequence, 16, 8);
ETS2AP_ASSERT_FIELD(timestamp_ns, 24, 8);
ETS2AP_ASSERT_FIELD(paused, 32, 1);
ETS2AP_ASSERT_FIELD(moving_backward, 33, 1);
ETS2AP_ASSERT_FIELD(reserved_0, 34, 2);
ETS2AP_ASSERT_FIELD(gear_displayed, 36, 4);
ETS2AP_ASSERT_FIELD(gear_engine, 40, 4);
ETS2AP_ASSERT_FIELD(reserved_1, 44, 4);
ETS2AP_ASSERT_FIELD(speed_mps, 48, 8);
ETS2AP_ASSERT_FIELD(engine_speed_rad_s, 56, 8);
ETS2AP_ASSERT_FIELD(steering_input, 64, 8);
ETS2AP_ASSERT_FIELD(steering_effective, 72, 8);
ETS2AP_ASSERT_FIELD(throttle_input, 80, 8);
ETS2AP_ASSERT_FIELD(throttle_effective, 88, 8);
ETS2AP_ASSERT_FIELD(brake_input, 96, 8);
ETS2AP_ASSERT_FIELD(brake_effective, 104, 8);
ETS2AP_ASSERT_FIELD(world_x_m, 112, 8);
ETS2AP_ASSERT_FIELD(world_y_m, 120, 8);
ETS2AP_ASSERT_FIELD(world_z_m, 128, 8);
ETS2AP_ASSERT_FIELD(heading_rad, 136, 8);
ETS2AP_ASSERT_FIELD(pitch_rad, 144, 8);
ETS2AP_ASSERT_FIELD(roll_rad, 152, 8);
ETS2AP_ASSERT_FIELD(linear_velocity_local_x_mps, 160, 8);
ETS2AP_ASSERT_FIELD(linear_velocity_local_y_mps, 168, 8);
ETS2AP_ASSERT_FIELD(linear_velocity_local_z_mps, 176, 8);
ETS2AP_ASSERT_FIELD(angular_velocity_local_x_rad_s, 184, 8);
ETS2AP_ASSERT_FIELD(angular_velocity_local_y_rad_s, 192, 8);
ETS2AP_ASSERT_FIELD(angular_velocity_local_z_rad_s, 200, 8);
ETS2AP_ASSERT_FIELD(navigation_distance_m, 208, 8);
ETS2AP_ASSERT_FIELD(navigation_time_s, 216, 8);
ETS2AP_ASSERT_FIELD(speed_limit_mps, 224, 8);
ETS2AP_ASSERT_FIELD(cruise_speed_mps, 232, 8);
#undef ETS2AP_ASSERT_FIELD

} // namespace ets2ap::telemetry
