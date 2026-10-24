// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/telemetry/convert.h"
#include "ets2ap/telemetry/layout.h"
#include "ets2ap/telemetry/state.h"
#include "ets2ap/test/test_framework.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

using namespace ets2ap::telemetry;

namespace {
constexpr std::uint32_t Bit(ValidityBits bit) {
    return static_cast<std::uint32_t>(bit);
}

void SetAllChannels(TelemetryState& state) {
    const float speed = -1.0F;
    const float rpm = 3000.0F;
    const std::int32_t displayed = -2;
    const std::int32_t engine = -1;
    const float steering = 0.5F;
    const float throttle = 0.75F;
    const float brake = 0.25F;
    const WorldPlacement placement{100.0, 200.0, -300.0, 0.25F, -0.125F, 0.5F};
    const Float3 linear{1.0F, -2.0F, 3.0F};
    const Float3 angular{0.25F, -0.5F, 1.0F};
    const float navigation_distance = 123.0F;
    const float navigation_time = 45.0F;
    const float speed_limit = 20.0F;
    const float cruise = 18.0F;
    state.SetPaused(true);
    state.SetSpeed(&speed);
    state.SetEngineRpm(&rpm);
    state.SetDisplayedGear(&displayed);
    state.SetEngineGear(&engine);
    state.SetInputSteering(&steering);
    state.SetEffectiveSteering(&steering);
    state.SetInputThrottle(&throttle);
    state.SetEffectiveThrottle(&throttle);
    state.SetInputBrake(&brake);
    state.SetEffectiveBrake(&brake);
    state.SetWorldPlacement(&placement);
    state.SetLocalLinearVelocity(&linear);
    state.SetLocalAngularVelocity(&angular);
    state.SetNavigationDistance(&navigation_distance);
    state.SetNavigationTime(&navigation_time);
    state.SetNavigationSpeedLimit(&speed_limit);
    state.SetCruiseControl(&cruise);
}
} // namespace

ETS2AP_TEST(LayoutMatchesWireContract) {
    CHECK_EQ(sizeof(TelemetryV1), 240U);
    CHECK_EQ(alignof(TelemetryV1), 8U);
    CHECK_EQ(kMagic, 0x50413245U);
    CHECK_EQ(kVersionMajor, 1U);
    CHECK_EQ(kVersionMinor, 0U);
    CHECK_EQ(kStructSize, 240U);
    CHECK_EQ(kMappingSize, 4096U);
#define CHECK_FIELD(field, offset, size) \
    CHECK_EQ(offsetof(TelemetryV1, field), offset); \
    CHECK_EQ(sizeof(TelemetryV1::field), size)
    CHECK_FIELD(magic, 0U, 4U);
    CHECK_FIELD(version_major, 4U, 2U);
    CHECK_FIELD(version_minor, 6U, 2U);
    CHECK_FIELD(struct_size, 8U, 4U);
    CHECK_FIELD(validity_flags, 12U, 4U);
    CHECK_FIELD(sequence, 16U, 8U);
    CHECK_FIELD(timestamp_ns, 24U, 8U);
    CHECK_FIELD(paused, 32U, 1U);
    CHECK_FIELD(moving_backward, 33U, 1U);
    CHECK_FIELD(reserved_0, 34U, 2U);
    CHECK_FIELD(gear_displayed, 36U, 4U);
    CHECK_FIELD(gear_engine, 40U, 4U);
    CHECK_FIELD(reserved_1, 44U, 4U);
    CHECK_FIELD(speed_mps, 48U, 8U);
    CHECK_FIELD(engine_speed_rad_s, 56U, 8U);
    CHECK_FIELD(steering_input, 64U, 8U);
    CHECK_FIELD(steering_effective, 72U, 8U);
    CHECK_FIELD(throttle_input, 80U, 8U);
    CHECK_FIELD(throttle_effective, 88U, 8U);
    CHECK_FIELD(brake_input, 96U, 8U);
    CHECK_FIELD(brake_effective, 104U, 8U);
    CHECK_FIELD(world_x_m, 112U, 8U);
    CHECK_FIELD(world_y_m, 120U, 8U);
    CHECK_FIELD(world_z_m, 128U, 8U);
    CHECK_FIELD(heading_rad, 136U, 8U);
    CHECK_FIELD(pitch_rad, 144U, 8U);
    CHECK_FIELD(roll_rad, 152U, 8U);
    CHECK_FIELD(linear_velocity_local_x_mps, 160U, 8U);
    CHECK_FIELD(linear_velocity_local_y_mps, 168U, 8U);
    CHECK_FIELD(linear_velocity_local_z_mps, 176U, 8U);
    CHECK_FIELD(angular_velocity_local_x_rad_s, 184U, 8U);
    CHECK_FIELD(angular_velocity_local_y_rad_s, 192U, 8U);
    CHECK_FIELD(angular_velocity_local_z_rad_s, 200U, 8U);
    CHECK_FIELD(navigation_distance_m, 208U, 8U);
    CHECK_FIELD(navigation_time_s, 216U, 8U);
    CHECK_FIELD(speed_limit_mps, 224U, 8U);
    CHECK_FIELD(cruise_speed_mps, 232U, 8U);
#undef CHECK_FIELD
}

ETS2AP_TEST(ValidityBitsMatchFourteenGroups) {
    const std::array bits{
        ValidityBits::Paused, ValidityBits::Speed, ValidityBits::EngineSpeed,
        ValidityBits::Gears, ValidityBits::Steering, ValidityBits::Throttle,
        ValidityBits::Brake, ValidityBits::WorldPlacement,
        ValidityBits::LocalLinearVelocity, ValidityBits::LocalAngularVelocity,
        ValidityBits::NavigationDistance, ValidityBits::NavigationTime,
        ValidityBits::SpeedLimit, ValidityBits::CruiseSpeed};
    for (std::size_t i = 0; i < bits.size(); ++i) {
        CHECK_EQ(Bit(bits[i]), 1U << i);
    }
    CHECK_EQ(kKnownValidityMask, 0x3FFFU);
}

ETS2AP_TEST(UnitConversionsPreserveSignsAndWrapHeading) {
    constexpr double pi = std::numbers::pi_v<double>;
    CHECK_NEAR(RpmToRadPerSec(3000.0F), 100.0 * pi, 1e-12);
    CHECK_NEAR(RevPerSecToRadPerSec(-0.5F), -pi, 1e-12);
    const std::array<float, 6> source{0.0F, 0.25F, 0.5F, 0.75F, 1.0F, -0.0F};
    const std::array<double, 6> expected{0.0, pi / 2, pi, 3 * pi / 2, 0.0, 0.0};
    for (std::size_t i = 0; i < source.size(); ++i) {
        CHECK_NEAR(HeadingToRad(source[i]), expected[i], 1e-12);
    }
    CHECK_NEAR(HeadingToRad(-0.0001F), 2 * pi - 2 * pi * static_cast<double>(0.0001F), 1e-12);
    CHECK_NEAR(PitchToRad(-0.25F), -pi / 2, 1e-12);
    CHECK_NEAR(RollToRad(0.5F), pi, 1e-12);
}

ETS2AP_TEST(QpcConversionUsesCheckedIntegerArithmetic) {
    CHECK_EQ(QpcTicksToNs(3'000'000'000LL, 24'000'000LL).value(), 125'000'000'000ULL);
    CHECK_EQ(QpcTicksToNs(123'456'789LL, 10'000'000LL).value(), 12'345'678'900ULL);
    CHECK_EQ(QpcTicksToNs(std::numeric_limits<std::int64_t>::max(), 1'000'000'000LL).value(),
             static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    CHECK(!QpcTicksToNs(-1, 10'000'000).has_value());
    CHECK(!QpcTicksToNs(1, 0).has_value());
    CHECK(!QpcTicksToNs(1, -1).has_value());
    CHECK(!QpcTicksToNs(std::numeric_limits<std::int64_t>::max(), 1).has_value());

    // Independent direct multiplication is safe for these bounded 32-bit samples.
    std::uint64_t seed = 0x90A34C7812ULL;
    for (int i = 0; i < 1000; ++i) {
        seed = seed * 6364136223846793005ULL + 1ULL;
        const auto ticks = static_cast<std::int64_t>((seed >> 32) & 0xFFFFFFFFULL);
        const auto freq = static_cast<std::int64_t>(1 + (seed & 0xFFFFFFULL));
        const auto expected = static_cast<std::uint64_t>(ticks) * 1'000'000'000ULL /
                              static_cast<std::uint64_t>(freq);
        CHECK_EQ(QpcTicksToNs(ticks, freq).value(), expected);
    }
}

ETS2AP_TEST(AllChannelsSetTheirFourteenValidityGroups) {
    TelemetryState state;
    SetAllChannels(state);
    const auto snapshot = state.Build(123456U);
    CHECK_EQ(snapshot.validity_flags, 0x3FFFU);
    CHECK_EQ(snapshot.timestamp_ns, 123456ULL);
    CHECK_EQ(snapshot.paused, 1U);
    CHECK_EQ(snapshot.moving_backward, 1U);
    CHECK_EQ(snapshot.speed_mps, 1.0);
    CHECK_NEAR(snapshot.engine_speed_rad_s, 100.0 * std::numbers::pi_v<double>, 1e-10);
    CHECK_EQ(snapshot.gear_displayed, -2);
    CHECK_EQ(snapshot.gear_engine, -1);
    CHECK_EQ(snapshot.steering_input, 0.5);
    CHECK_EQ(snapshot.steering_effective, 0.5);
    CHECK_EQ(snapshot.throttle_input, 0.75);
    CHECK_EQ(snapshot.brake_effective, 0.25);
    CHECK_EQ(snapshot.world_x_m, 100.0);
    CHECK_EQ(snapshot.world_y_m, 200.0);
    CHECK_EQ(snapshot.world_z_m, -300.0);
    CHECK_NEAR(snapshot.heading_rad, std::numbers::pi_v<double> / 2, 1e-12);
    CHECK_NEAR(snapshot.pitch_rad, -std::numbers::pi_v<double> / 4, 1e-12);
    CHECK_NEAR(snapshot.roll_rad, std::numbers::pi_v<double>, 1e-12);
    CHECK_EQ(snapshot.linear_velocity_local_x_mps, 1.0);
    CHECK_EQ(snapshot.linear_velocity_local_y_mps, -2.0);
    CHECK_EQ(snapshot.linear_velocity_local_z_mps, 3.0);
    CHECK_NEAR(snapshot.angular_velocity_local_x_rad_s, std::numbers::pi_v<double> / 2, 1e-12);
    CHECK_NEAR(snapshot.angular_velocity_local_y_rad_s, -std::numbers::pi_v<double>, 1e-12);
    CHECK_NEAR(snapshot.angular_velocity_local_z_rad_s, 2 * std::numbers::pi_v<double>, 1e-12);
    CHECK_EQ(snapshot.navigation_distance_m, 123.0);
    CHECK_EQ(snapshot.navigation_time_s, 45.0);
    CHECK_EQ(snapshot.speed_limit_mps, 20.0);
    CHECK_EQ(snapshot.cruise_speed_mps, 18.0);
}

ETS2AP_TEST(InvalidCallbacksClearWholeGroupUntilRecovered) {
    TelemetryState state;
    SetAllChannels(state);
    const float valid = 0.5F;
    state.SetEffectiveSteering(nullptr);
    auto snapshot = state.Build(1U);
    CHECK_EQ(snapshot.validity_flags & Bit(ValidityBits::Steering), 0U);
    CHECK_EQ(snapshot.steering_input, 0.0);
    CHECK_EQ(snapshot.steering_effective, 0.0);
    CHECK_NE(snapshot.validity_flags & Bit(ValidityBits::Speed), 0U);
    state.SetEffectiveSteering(&valid);
    snapshot = state.Build(2U);
    CHECK_NE(snapshot.validity_flags & Bit(ValidityBits::Steering), 0U);
    CHECK_EQ(snapshot.steering_input, 0.5);
    CHECK_EQ(snapshot.steering_effective, 0.5);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    state.SetNavigationTime(&nan);
    snapshot = state.Build(3U);
    CHECK_EQ(snapshot.validity_flags & Bit(ValidityBits::NavigationTime), 0U);
    CHECK_EQ(snapshot.navigation_time_s, 0.0);
}

ETS2AP_TEST(InvalidNumbersAndOutOfRangeValuesAreNeverPublished) {
    TelemetryState state;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float neg_inf = -inf;
    const float over_one = 1.01F;
    const float under_zero = -0.01F;
    const float over_rpm = -1.0F;
    const WorldPlacement bad_position{inf, 0.0, 0.0, 0.0F, 0.0F, 0.0F};
    const WorldPlacement bad_pitch{0.0, 0.0, 0.0, 0.0F, 0.2501F, 0.0F};
    const WorldPlacement bad_roll{0.0, 0.0, 0.0, 0.0F, 0.0F, -0.5001F};
    const Float3 bad_linear{0.0F, nan, 0.0F};
    const Float3 bad_angular{0.0F, 0.0F, neg_inf};
    SetAllChannels(state);
    state.SetSpeed(&nan);
    state.SetEngineRpm(&over_rpm);
    state.SetInputSteering(&over_one);
    state.SetEffectiveThrottle(&under_zero);
    state.SetInputBrake(&inf);
    state.SetWorldPlacement(&bad_position);
    state.SetLocalLinearVelocity(&bad_linear);
    state.SetLocalAngularVelocity(&bad_angular);
    state.SetNavigationDistance(&neg_inf);
    state.SetNavigationTime(nullptr);
    state.SetNavigationSpeedLimit(&under_zero);
    state.SetCruiseControl(&nan);
    const auto snapshot = state.Build(0);
    CHECK_EQ(snapshot.validity_flags, Bit(ValidityBits::Paused) | Bit(ValidityBits::Gears));
    CHECK_EQ(snapshot.speed_mps, 0.0);
    CHECK_EQ(snapshot.moving_backward, 0U);
    CHECK_EQ(snapshot.engine_speed_rad_s, 0.0);
    CHECK_EQ(snapshot.steering_input, 0.0);
    CHECK_EQ(snapshot.throttle_input, 0.0);
    CHECK_EQ(snapshot.brake_effective, 0.0);
    CHECK_EQ(snapshot.world_x_m, 0.0);
    CHECK_EQ(snapshot.linear_velocity_local_y_mps, 0.0);
    CHECK_EQ(snapshot.angular_velocity_local_z_rad_s, 0.0);
    CHECK_EQ(snapshot.navigation_distance_m, 0.0);
    CHECK_EQ(snapshot.navigation_time_s, 0.0);
    CHECK_EQ(snapshot.speed_limit_mps, 0.0);
    CHECK_EQ(snapshot.cruise_speed_mps, 0.0);
    state.SetWorldPlacement(&bad_pitch);
    CHECK_EQ(state.Build(0).validity_flags & Bit(ValidityBits::WorldPlacement), 0U);
    state.SetWorldPlacement(&bad_roll);
    CHECK_EQ(state.Build(0).validity_flags & Bit(ValidityBits::WorldPlacement), 0U);
}

ETS2AP_TEST(BoundaryValuesAndNegativeZeroAreValid) {
    TelemetryState state;
    const float negative_zero = -0.0F;
    const float positive_one = 1.0F;
    const float negative_one = -1.0F;
    const WorldPlacement placement{0.0, -0.0, 0.0, 1.0F, -0.25F, 0.5F};
    state.SetSpeed(&negative_zero);
    state.SetEngineRpm(&negative_zero);
    state.SetInputSteering(&negative_one);
    state.SetEffectiveSteering(&positive_one);
    state.SetInputThrottle(&negative_zero);
    state.SetEffectiveThrottle(&positive_one);
    state.SetInputBrake(&negative_zero);
    state.SetEffectiveBrake(&positive_one);
    state.SetWorldPlacement(&placement);
    state.SetNavigationDistance(&negative_zero);
    state.SetNavigationTime(&negative_zero);
    state.SetNavigationSpeedLimit(&negative_zero);
    state.SetCruiseControl(&negative_zero);
    const auto snapshot = state.Build(0);
    CHECK_EQ(snapshot.moving_backward, 0U);
    CHECK_EQ(snapshot.speed_mps, 0.0);
    CHECK(!std::signbit(snapshot.speed_mps));
    CHECK_EQ(snapshot.engine_speed_rad_s, 0.0);
    CHECK_EQ(snapshot.steering_input, -1.0);
    CHECK_EQ(snapshot.steering_effective, 1.0);
    CHECK_NE(snapshot.validity_flags & Bit(ValidityBits::WorldPlacement), 0U);
    CHECK_EQ(snapshot.heading_rad, 0.0);
    CHECK_NE(snapshot.validity_flags & Bit(ValidityBits::SpeedLimit), 0U);
    CHECK_NE(snapshot.validity_flags & Bit(ValidityBits::CruiseSpeed), 0U);
}

ETS2AP_TEST(BackwardHysteresisUsesStrictThresholdsAndResetsOnInvalidSpeed) {
    TelemetryState state;
    struct Sample { float speed; std::uint8_t backward; };
    const std::array samples{
        Sample{-0.05F, 0U}, Sample{-0.051F, 1U}, Sample{-0.035F, 1U},
        Sample{-0.02F, 1U}, Sample{-0.019F, 0U}, Sample{-0.05F, 0U},
        Sample{-0.051F, 1U}, Sample{-0.0F, 0U}};
    for (const auto sample : samples) {
        state.SetSpeed(&sample.speed);
        CHECK_EQ(state.Build(0).moving_backward, sample.backward);
    }
    const float back = -1.0F;
    state.SetSpeed(&back);
    CHECK_EQ(state.Build(0).moving_backward, 1U);
    state.SetSpeed(nullptr);
    auto snapshot = state.Build(0);
    CHECK_EQ(snapshot.validity_flags & Bit(ValidityBits::Speed), 0U);
    CHECK_EQ(snapshot.moving_backward, 0U);
    const float middle = -0.03F;
    state.SetSpeed(&middle);
    snapshot = state.Build(0);
    CHECK_EQ(snapshot.moving_backward, 0U);
    CHECK_NE(snapshot.validity_flags & Bit(ValidityBits::Speed), 0U);
}

ETS2AP_TEST(PausedEventsDoNotClearChannelValidityButResetDoes) {
    TelemetryState state;
    const float speed = 5.0F;
    state.SetSpeed(&speed);
    CHECK_EQ(state.Build(0).validity_flags & Bit(ValidityBits::Paused), 0U);
    state.SetPaused(true);
    CHECK_EQ(state.Build(0).paused, 1U);
    CHECK_NE(state.Build(0).validity_flags & Bit(ValidityBits::Speed), 0U);
    state.SetPaused(false);
    CHECK_EQ(state.Build(0).paused, 0U);
    CHECK_NE(state.Build(0).validity_flags & Bit(ValidityBits::Speed), 0U);
    state.Reset();
    CHECK_EQ(state.Build(0).validity_flags, 0U);
    CHECK_EQ(state.Build(0).speed_mps, 0.0);
}

ETS2AP_TEST(BuildPopulatesHeaderAndZerosReservedFields) {
    TelemetryState state;
    const auto snapshot = state.Build(987654321ULL);
    CHECK_EQ(snapshot.magic, 0x50413245U);
    CHECK_EQ(snapshot.version_major, 1U);
    CHECK_EQ(snapshot.version_minor, 0U);
    CHECK_EQ(snapshot.struct_size, 240U);
    CHECK_EQ(snapshot.validity_flags, 0U);
    CHECK_EQ(snapshot.sequence, 0ULL);
    CHECK_EQ(snapshot.timestamp_ns, 987654321ULL);
    CHECK_EQ(snapshot.reserved_0, 0U);
    CHECK_EQ(snapshot.reserved_1, 0U);
}

ETS2AP_TEST(EveryScalarChannelRejectsMissingAndNonfiniteSamples) {
    using Setter = void (TelemetryState::*)(const float*) noexcept;
    struct ChannelCase { Setter setter; ValidityBits bit; float minimum; float maximum; };
    const std::array channels{
        ChannelCase{&TelemetryState::SetSpeed, ValidityBits::Speed, -10.0F, 10.0F},
        ChannelCase{&TelemetryState::SetEngineRpm, ValidityBits::EngineSpeed, 0.0F, 100.0F},
        ChannelCase{&TelemetryState::SetInputSteering, ValidityBits::Steering, -1.0F, 1.0F},
        ChannelCase{&TelemetryState::SetEffectiveSteering, ValidityBits::Steering, -1.0F, 1.0F},
        ChannelCase{&TelemetryState::SetInputThrottle, ValidityBits::Throttle, 0.0F, 1.0F},
        ChannelCase{&TelemetryState::SetEffectiveThrottle, ValidityBits::Throttle, 0.0F, 1.0F},
        ChannelCase{&TelemetryState::SetInputBrake, ValidityBits::Brake, 0.0F, 1.0F},
        ChannelCase{&TelemetryState::SetEffectiveBrake, ValidityBits::Brake, 0.0F, 1.0F},
        ChannelCase{&TelemetryState::SetNavigationDistance, ValidityBits::NavigationDistance, 0.0F, 100.0F},
        ChannelCase{&TelemetryState::SetNavigationTime, ValidityBits::NavigationTime, 0.0F, 100.0F},
        ChannelCase{&TelemetryState::SetNavigationSpeedLimit, ValidityBits::SpeedLimit, 0.0F, 100.0F},
        ChannelCase{&TelemetryState::SetCruiseControl, ValidityBits::CruiseSpeed, 0.0F, 100.0F}};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float positive_inf = std::numeric_limits<float>::infinity();
    const float negative_inf = -positive_inf;
    for (const auto& channel : channels) {
        TelemetryState state;
        SetAllChannels(state);
        (state.*channel.setter)(nullptr);
        CHECK_EQ(state.Build(1).validity_flags & Bit(channel.bit), 0U);
        for (const float invalid : {nan, positive_inf, negative_inf}) {
            (state.*channel.setter)(&invalid);
            CHECK_EQ(state.Build(1).validity_flags & Bit(channel.bit), 0U);
        }
        (state.*channel.setter)(&channel.minimum);
        CHECK_NE(state.Build(1).validity_flags & Bit(channel.bit), 0U);
        (state.*channel.setter)(&channel.maximum);
        CHECK_NE(state.Build(1).validity_flags & Bit(channel.bit), 0U);
    }
}

ETS2AP_TEST(ConstrainedScalarChannelsRejectOutOfRangeOnBothSides) {
    using Setter = void (TelemetryState::*)(const float*) noexcept;
    struct ChannelCase { Setter setter; ValidityBits bit; float below; float above; };
    const std::array channels{
        ChannelCase{&TelemetryState::SetInputSteering, ValidityBits::Steering, -1.01F, 1.01F},
        ChannelCase{&TelemetryState::SetEffectiveSteering, ValidityBits::Steering, -1.01F, 1.01F},
        ChannelCase{&TelemetryState::SetInputThrottle, ValidityBits::Throttle, -0.01F, 1.01F},
        ChannelCase{&TelemetryState::SetEffectiveThrottle, ValidityBits::Throttle, -0.01F, 1.01F},
        ChannelCase{&TelemetryState::SetInputBrake, ValidityBits::Brake, -0.01F, 1.01F},
        ChannelCase{&TelemetryState::SetEffectiveBrake, ValidityBits::Brake, -0.01F, 1.01F}};
    for (const auto& channel : channels) {
        TelemetryState state;
        SetAllChannels(state);
        (state.*channel.setter)(&channel.below);
        CHECK_EQ(state.Build(0).validity_flags & Bit(channel.bit), 0U);
        (state.*channel.setter)(&channel.above);
        CHECK_EQ(state.Build(0).validity_flags & Bit(channel.bit), 0U);
    }
    using NonnegativeSetter = void (TelemetryState::*)(const float*) noexcept;
    const std::array nonnegative{
        NonnegativeSetter{&TelemetryState::SetEngineRpm},
        NonnegativeSetter{&TelemetryState::SetNavigationDistance},
        NonnegativeSetter{&TelemetryState::SetNavigationTime},
        NonnegativeSetter{&TelemetryState::SetNavigationSpeedLimit},
        NonnegativeSetter{&TelemetryState::SetCruiseControl}};
    const float below_zero = -0.01F;
    for (const auto setter : nonnegative) {
        TelemetryState state;
        SetAllChannels(state);
        (state.*setter)(&below_zero);
        CHECK_NE(state.Build(0).validity_flags, kKnownValidityMask);
    }
}

ETS2AP_TEST(CompositeChannelsClearAndRestoreTheirCompleteGroups) {
    TelemetryState state;
    SetAllChannels(state);
    const auto initial = state.Build(0);
    CHECK_EQ(initial.validity_flags, kKnownValidityMask);
    state.SetDisplayedGear(nullptr);
    auto snapshot = state.Build(0);
    CHECK_EQ(snapshot.validity_flags & Bit(ValidityBits::Gears), 0U);
    CHECK_EQ(snapshot.gear_displayed, 0);
    CHECK_EQ(snapshot.gear_engine, 0);
    const std::int32_t gear = std::numeric_limits<std::int32_t>::min();
    state.SetDisplayedGear(&gear);
    CHECK_NE(state.Build(0).validity_flags & Bit(ValidityBits::Gears), 0U);
    state.SetEngineGear(nullptr);
    CHECK_EQ(state.Build(0).validity_flags & Bit(ValidityBits::Gears), 0U);
    state.SetEngineGear(&gear);
    CHECK_EQ(state.Build(0).gear_engine, gear);

    const WorldPlacement valid{0.0, 1.0, 2.0, 0.0F, 0.0F, 0.0F};
    const WorldPlacement invalid{0.0, 1.0, std::numeric_limits<double>::infinity(),
                                 0.0F, 0.0F, 0.0F};
    state.SetWorldPlacement(nullptr);
    CHECK_EQ(state.Build(0).validity_flags & Bit(ValidityBits::WorldPlacement), 0U);
    state.SetWorldPlacement(&invalid);
    CHECK_EQ(state.Build(0).validity_flags & Bit(ValidityBits::WorldPlacement), 0U);
    state.SetWorldPlacement(&valid);
    CHECK_NE(state.Build(0).validity_flags & Bit(ValidityBits::WorldPlacement), 0U);

    const Float3 invalid_vector{1.0F, 2.0F, std::numeric_limits<float>::infinity()};
    const Float3 valid_vector{1.0F, 2.0F, 3.0F};
    state.SetLocalLinearVelocity(nullptr);
    CHECK_EQ(state.Build(0).validity_flags & Bit(ValidityBits::LocalLinearVelocity), 0U);
    state.SetLocalLinearVelocity(&invalid_vector);
    CHECK_EQ(state.Build(0).linear_velocity_local_x_mps, 0.0);
    state.SetLocalLinearVelocity(&valid_vector);
    CHECK_NE(state.Build(0).validity_flags & Bit(ValidityBits::LocalLinearVelocity), 0U);
    state.SetLocalAngularVelocity(nullptr);
    CHECK_EQ(state.Build(0).validity_flags & Bit(ValidityBits::LocalAngularVelocity), 0U);
    state.SetLocalAngularVelocity(&invalid_vector);
    CHECK_EQ(state.Build(0).angular_velocity_local_x_rad_s, 0.0);
    state.SetLocalAngularVelocity(&valid_vector);
    snapshot = state.Build(0);
    CHECK_NE(snapshot.validity_flags & Bit(ValidityBits::LocalAngularVelocity), 0U);
    CHECK_EQ(snapshot.validity_flags, kKnownValidityMask);
}

ETS2AP_TEST_MAIN()
