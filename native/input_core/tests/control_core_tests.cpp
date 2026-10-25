// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/input/control_core.h"
#include "ets2ap/test/test_framework.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

using namespace ets2ap::input;

namespace {
constexpr std::uint32_t kCommandMagic = 0x50413243U;
constexpr std::uint32_t kStatusMagic = 0x50413253U;
constexpr std::uint64_t kStartNs = 1'000'000'000ULL;

void PutLittleEndian(std::vector<std::byte>& bytes, std::size_t offset,
                     std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i)
        bytes[offset + i] = static_cast<std::byte>((value >> (i * 8)) & 0xFFU);
}

std::uint64_t GetLittleEndian(std::span<const std::byte> bytes, std::size_t offset,
                              std::size_t width) {
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < width; ++i)
        result |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + i])) << (i * 8);
    return result;
}

void PutDouble(std::vector<std::byte>& bytes, std::size_t offset, double value) {
    PutLittleEndian(bytes, offset, std::bit_cast<std::uint64_t>(value), 8);
}

std::vector<std::byte> Packet(std::uint64_t sequence, std::uint64_t timestamp_ns,
                              double steering = 0.0, double throttle = 0.0,
                              double brake = 0.0, bool engage = false,
                              std::uint16_t minor = 0, std::size_t size = 64) {
    std::vector<std::byte> bytes(size);
    PutLittleEndian(bytes, 0, kCommandMagic, 4);
    PutLittleEndian(bytes, 4, 1, 2);
    PutLittleEndian(bytes, 6, minor, 2);
    PutLittleEndian(bytes, 8, size, 4);
    PutLittleEndian(bytes, 16, sequence, 8);
    PutLittleEndian(bytes, 24, timestamp_ns, 8);
    PutDouble(bytes, 32, steering);
    PutDouble(bytes, 40, throttle);
    PutDouble(bytes, 48, brake);
    PutLittleEndian(bytes, 56, engage ? 1 : 0, 1);
    return bytes;
}

SafetyGates SafeGates(double steering_limit = 1.0) {
    return {true, true, true, steering_limit};
}

CommandResult Accept(ControlCore& core, const std::vector<std::byte>& packet,
                     std::uint64_t now_ns, SafetyGates gates = {}) {
    return core.AcceptCommand(packet, now_ns, gates);
}

} // namespace

ETS2AP_TEST(NeutralHandshakeAcceptsV10AndCompatibleMinorTail) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    CHECK_EQ(core.Snapshot().state, ControlState::Disengaged);
    CHECK_EQ(core.Snapshot().last_accepted_sequence, 1ULL);

    auto extended = Packet(2, kStartNs + 50'000'000ULL, 0, 0, 0, false, 1, 80);
    CHECK_EQ(Accept(core, extended, kStartNs + 50'000'000ULL).outcome, CommandOutcome::Accepted);
    CHECK_EQ(core.Snapshot().last_accepted_sequence, 2ULL);
}

ETS2AP_TEST(FirstCommandMustBeSequenceOneNeutral) {
    ControlCore core;
    core.Connect(kStartNs);
    const auto result = Accept(core, Packet(2, kStartNs), kStartNs);
    CHECK_EQ(result.outcome, CommandOutcome::Rejected);
    CHECK(!result.close_connection);
    const auto status = core.Snapshot();
    CHECK_EQ(status.state, ControlState::Rejected);
    CHECK_EQ(status.reason, ControlReason::MalformedPacket);
    CHECK_EQ(status.steering, 0.0);
    CHECK_EQ(status.throttle, 0.0);
    CHECK_EQ(status.brake, 0.0);
}

ETS2AP_TEST(RejectsMalformedHeaderReservedFieldsFlagsAndRange) {
    const auto bad_packets = [] {
        std::array<std::vector<std::byte>, 6> packets{
            Packet(1, kStartNs), Packet(1, kStartNs), Packet(1, kStartNs),
            Packet(1, kStartNs), Packet(1, kStartNs), Packet(1, kStartNs, 1.01)};
        PutLittleEndian(packets[0], 0, 0, 4);
        PutLittleEndian(packets[1], 12, 1, 4);
        packets[2][57] = std::byte{1};
        PutLittleEndian(packets[3], 60, 1, 4);
        packets[4][56] = std::byte{2};
        return packets;
    }();

    for (const auto& packet : bad_packets) {
        ControlCore core;
        core.Connect(kStartNs);
        CHECK_EQ(Accept(core, packet, kStartNs).outcome, CommandOutcome::Rejected);
        CHECK_EQ(core.Snapshot().state, ControlState::Rejected);
        CHECK_EQ(core.Snapshot().steering, 0.0);
    }

    ControlCore nonfinite;
    nonfinite.Connect(kStartNs);
    const auto nan_packet = Packet(1, kStartNs, std::numeric_limits<double>::quiet_NaN());
    CHECK_EQ(Accept(nonfinite, nan_packet, kStartNs).outcome, CommandOutcome::Rejected);
}

ETS2AP_TEST(VersionMismatchAndFramingErrorsCloseConnection) {
    ControlCore major;
    major.Connect(kStartNs);
    auto wrong_major = Packet(1, kStartNs);
    PutLittleEndian(wrong_major, 4, 2, 2);
    auto result = Accept(major, wrong_major, kStartNs);
    CHECK_EQ(result.outcome, CommandOutcome::ConnectionFatal);
    CHECK(result.close_connection);
    CHECK_EQ(major.Snapshot().reason, ControlReason::VersionMismatch);

    ControlCore frame;
    frame.Connect(kStartNs);
    auto truncated = Packet(1, kStartNs);
    truncated.pop_back();
    result = Accept(frame, truncated, kStartNs);
    CHECK_EQ(result.outcome, CommandOutcome::ConnectionFatal);
    CHECK(result.close_connection);
}

ETS2AP_TEST(EngagementNeedsFreshNeutralAndAllSafetyGates) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);

    auto engage = Packet(2, kStartNs + 1, 0.5, 0.4, 0, true);
    CHECK_EQ(Accept(core, engage, kStartNs + 1).outcome, CommandOutcome::SafetyBlocked);
    CHECK_EQ(core.Snapshot().state, ControlState::Disengaged);
    CHECK_EQ(core.Snapshot().throttle, 0.0);

    engage = Packet(3, kStartNs + 2, 0.5, 0.4, 0, true);
    CHECK_EQ(Accept(core, engage, kStartNs + 2, SafeGates()).outcome,
             CommandOutcome::SafetyBlocked);
    CHECK_EQ(Accept(core, Packet(4, kStartNs + 3), kStartNs + 3).outcome,
             CommandOutcome::Accepted);
    engage = Packet(5, kStartNs + 4, 0.5, 0.4, 0, true);
    CHECK_EQ(Accept(core, engage, kStartNs + 4, SafeGates()).outcome,
             CommandOutcome::Accepted);
    const auto status = core.Snapshot();
    CHECK_EQ(status.state, ControlState::Engaged);
    CHECK_EQ(status.steering, 0.0);
    CHECK_EQ(status.throttle, 0.0);
}

ETS2AP_TEST(BrakePriorityAndIndependentSlewLimitsApply) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(2, kStartNs + 10'000'000ULL, 1, 0.8, 0.5, true),
                    kStartNs + 10'000'000ULL, SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK_EQ(core.Snapshot().brake, 0.0);

    CHECK_EQ(Accept(core, Packet(3, kStartNs + 110'000'000ULL, 1, 0.8, 0.5, true),
                    kStartNs + 110'000'000ULL, SafeGates()).outcome, CommandOutcome::Accepted);
    const auto status = core.Snapshot();
    CHECK_NEAR(status.steering, 0.2, 1e-9);
    CHECK_EQ(status.throttle, 0.0);
    CHECK_NEAR(status.brake, 0.2, 1e-9);
}

ETS2AP_TEST(SpeedSteeringCapBoundsSlewTarget) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(2, kStartNs + 1, 1, 0, 0, true), kStartNs + 1,
                    SafeGates(0.25)).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(3, kStartNs + 50'000'001ULL, 1, 0, 0, true),
                    kStartNs + 50'000'001ULL, SafeGates(0.25)).outcome,
             CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(4, kStartNs + 100'000'001ULL, 1, 0, 0, true),
                    kStartNs + 100'000'001ULL, SafeGates(0.25)).outcome,
             CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(5, kStartNs + 150'000'001ULL, 1, 0, 0, true),
                    kStartNs + 150'000'001ULL, SafeGates(0.25)).outcome,
             CommandOutcome::Accepted);
    CHECK_NEAR(core.Snapshot().steering, 0.25, 1e-9);
}

ETS2AP_TEST(ReducedSteeringCapImmediatelyBoundsAppliedOutput) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(2, kStartNs + 1, 1, 0, 0, true), kStartNs + 1,
                    SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(3, kStartNs + 50'000'001ULL, 1, 0, 0, true),
                    kStartNs + 50'000'001ULL, SafeGates()).outcome,
             CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(4, kStartNs + 100'000'001ULL, 1, 0, 0, true),
                    kStartNs + 100'000'001ULL, SafeGates()).outcome,
             CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(5, kStartNs + 150'000'001ULL, 1, 0, 0, true),
                    kStartNs + 150'000'001ULL, SafeGates()).outcome,
             CommandOutcome::Accepted);
    CHECK(core.Snapshot().steering > 0.1);

    CHECK_EQ(Accept(core, Packet(6, kStartNs + 200'000'001ULL, 1, 0, 0, true),
                    kStartNs + 200'000'001ULL, SafeGates(0.1)).outcome,
             CommandOutcome::Accepted);
    CHECK_NEAR(core.Snapshot().steering, 0.1, 1e-9);
}

ETS2AP_TEST(BrakeImmediatelyCutsPreviouslyAppliedThrottle) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(2, kStartNs + 1, 0, 0.5, 0, true), kStartNs + 1,
                    SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(3, kStartNs + 50'000'001ULL, 0, 0.5, 0, true),
                    kStartNs + 50'000'001ULL, SafeGates()).outcome,
             CommandOutcome::Accepted);
    CHECK(core.Snapshot().throttle > 0.0);

    CHECK_EQ(Accept(core, Packet(4, kStartNs + 60'000'001ULL, 0, 0.5, 0.5, true),
                    kStartNs + 60'000'001ULL, SafeGates()).outcome,
             CommandOutcome::Accepted);
    CHECK_EQ(core.Snapshot().throttle, 0.0);
}

ETS2AP_TEST(ThrottleRemainsZeroUntilAppliedBrakeReturnsToZero) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(2, kStartNs + 1, 0, 0, 0.5, true), kStartNs + 1,
                    SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(3, kStartNs + 50'000'001ULL, 0, 0, 0.5, true),
                    kStartNs + 50'000'001ULL, SafeGates()).outcome,
             CommandOutcome::Accepted);
    CHECK(core.Snapshot().brake > 0.0);

    CHECK_EQ(Accept(core, Packet(4, kStartNs + 60'000'001ULL, 0, 0.5, 0, true),
                    kStartNs + 60'000'001ULL, SafeGates()).outcome,
             CommandOutcome::Accepted);
    CHECK_EQ(core.Snapshot().throttle, 0.0);
    CHECK(core.Snapshot().brake > 0.0);

    CHECK_EQ(Accept(core, Packet(5, kStartNs + 90'000'001ULL, 0, 0.5, 0, true),
                    kStartNs + 90'000'001ULL, SafeGates()).outcome,
             CommandOutcome::Accepted);
    CHECK_NEAR(core.Snapshot().brake, 0.02, 1e-9);
    CHECK_EQ(core.Snapshot().throttle, 0.0);
    CHECK_EQ(Accept(core, Packet(6, kStartNs + 100'000'001ULL, 0, 0.5, 0, true),
                    kStartNs + 100'000'001ULL, SafeGates()).outcome,
             CommandOutcome::Accepted);
    CHECK_EQ(core.Snapshot().brake, 0.0);
    CHECK(core.Snapshot().throttle > 0.0);
}

ETS2AP_TEST(NegativeZeroIsNormalizedBeforeOutput) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(2, kStartNs + 1, -0.0, 0, 0, true), kStartNs + 1,
                    SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(3, kStartNs + 2, -0.0, 0, 0, true), kStartNs + 2,
                    SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK(!std::signbit(core.Snapshot().steering));
}

ETS2AP_TEST(ManualOverrideLatchRequiresReleaseAndNeutralCommand) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(2, kStartNs + 1, 0.1, 0.1, 0, true), kStartNs + 1,
                    SafeGates()).outcome, CommandOutcome::Accepted);

    core.SetManualInput(true);
    CHECK_EQ(core.Snapshot().state, ControlState::ManualOverrideLatched);
    CHECK_EQ(core.Snapshot().throttle, 0.0);
    core.SetManualInput(false);
    CHECK_EQ(core.Snapshot().state, ControlState::ManualOverrideLatched);
    CHECK_EQ(Accept(core, Packet(3, kStartNs + 2), kStartNs + 2).outcome,
             CommandOutcome::Accepted);
    CHECK_EQ(core.Snapshot().state, ControlState::Disengaged);
    CHECK_EQ(Accept(core, Packet(4, kStartNs + 3, 0, 0.2, 0, true), kStartNs + 3,
                    SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK_EQ(core.Snapshot().state, ControlState::Engaged);
    CHECK_EQ(core.Snapshot().throttle, 0.0);
}

ETS2AP_TEST(StatusPacketUsesDefinedLittleEndianLayout) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    const auto packet = core.EncodeStatus(kStartNs + 25);
    CHECK_EQ(packet.size(), std::size_t{64});
    CHECK_EQ(GetLittleEndian(packet, 0, 4), static_cast<std::uint64_t>(kStatusMagic));
    CHECK_EQ(GetLittleEndian(packet, 4, 2), 1ULL);
    CHECK_EQ(GetLittleEndian(packet, 6, 2), 0ULL);
    CHECK_EQ(GetLittleEndian(packet, 8, 4), 64ULL);
    CHECK_EQ(GetLittleEndian(packet, 16, 8), 1ULL);
    CHECK_EQ(GetLittleEndian(packet, 24, 8), kStartNs + 25);
    CHECK_EQ(GetLittleEndian(packet, 32, 4), static_cast<std::uint64_t>(ControlState::Disengaged));
    CHECK_EQ(GetLittleEndian(packet, 36, 4), static_cast<std::uint64_t>(ControlReason::None));
    CHECK_EQ(std::bit_cast<double>(GetLittleEndian(packet, 40, 8)), 0.0);
}

ETS2AP_TEST(DisconnectZerosAndPreservesManualLatch) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(2, kStartNs + 1, 0, 0.1, 0, true), kStartNs + 1,
                    SafeGates()).outcome, CommandOutcome::Accepted);
    core.SetManualInput(true);
    core.Disconnect();
    CHECK_EQ(core.Snapshot().state, ControlState::ManualOverrideLatched);
    CHECK_EQ(core.Snapshot().reason, ControlReason::ClientDisconnected);
    CHECK_EQ(core.Snapshot().throttle, 0.0);

    core.Connect(kStartNs + 2);
    CHECK_EQ(core.Snapshot().state, ControlState::ManualOverrideLatched);
    const auto disconnected_status = core.EncodeStatus(kStartNs + 2);
    CHECK_EQ(GetLittleEndian(disconnected_status, 36, 4),
             static_cast<std::uint64_t>(ControlReason::ClientDisconnected));
    const auto resumed_status = core.EncodeStatus(kStartNs + 3);
    CHECK_EQ(GetLittleEndian(resumed_status, 36, 4),
             static_cast<std::uint64_t>(ControlReason::ManualInput));
    CHECK_EQ(Accept(core, Packet(1, kStartNs + 2), kStartNs + 2).outcome,
             CommandOutcome::ManualOverride);
    core.SetManualInput(false);
    CHECK_EQ(Accept(core, Packet(2, kStartNs + 4), kStartNs + 4).outcome,
             CommandOutcome::Accepted);
    CHECK_EQ(core.Snapshot().state, ControlState::Disengaged);
    CHECK_EQ(Accept(core, Packet(3, kStartNs + 5, 0, 0.1, 0, true), kStartNs + 5,
                    SafeGates()).outcome, CommandOutcome::Accepted);
}

ETS2AP_TEST(TimestampAndSequenceMustIncreaseStrictly) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(2, kStartNs), kStartNs + 1).outcome,
             CommandOutcome::Rejected);
    CHECK_EQ(core.Snapshot().steering, 0.0);

    ControlCore replay;
    replay.Connect(kStartNs);
    CHECK_EQ(Accept(replay, Packet(1, kStartNs), kStartNs).outcome,
             CommandOutcome::Accepted);
    CHECK_EQ(Accept(replay, Packet(1, kStartNs + 1), kStartNs + 1).outcome,
             CommandOutcome::Rejected);
}

ETS2AP_TEST(RejectsZeroFutureAndStaleTimestamps) {
    const auto check_rejected = [](std::uint64_t timestamp, std::uint64_t now) {
        ControlCore core;
        core.Connect(kStartNs);
        const auto result = Accept(core, Packet(1, timestamp), now);
        CHECK_EQ(result.outcome, CommandOutcome::Rejected);
        CHECK_EQ(core.Snapshot().state, ControlState::Rejected);
        CHECK_EQ(core.Snapshot().reason, ControlReason::MalformedPacket);
    };

    check_rejected(0, kStartNs);
    check_rejected(kStartNs + 1, kStartNs);
    check_rejected(kStartNs, kStartNs + 100'000'001ULL);
}

ETS2AP_TEST(TimeoutNeedsFreshNeutralBeforeExplicitReengagement) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(2, kStartNs + 1, 0, 0.5, 0, true), kStartNs + 1,
                    SafeGates()).outcome, CommandOutcome::Accepted);

    core.Tick(kStartNs + 100'000'002ULL);
    auto status = core.Snapshot();
    CHECK_EQ(status.state, ControlState::Timeout);
    CHECK_EQ(status.reason, ControlReason::CommandTimeout);
    CHECK_EQ(status.throttle, 0.0);

    CHECK_EQ(Accept(core, Packet(3, kStartNs + 100'000'003ULL, 0, 0.5, 0, true),
                    kStartNs + 100'000'003ULL, SafeGates()).outcome,
             CommandOutcome::SafetyBlocked);
    CHECK_EQ(core.Snapshot().state, ControlState::Timeout);
    CHECK_EQ(Accept(core, Packet(4, kStartNs + 100'000'004ULL),
                    kStartNs + 100'000'004ULL).outcome, CommandOutcome::Accepted);
    CHECK_EQ(core.Snapshot().state, ControlState::Disengaged);
    CHECK_EQ(Accept(core, Packet(5, kStartNs + 100'000'005ULL, 0, 0.5, 0, true),
                    kStartNs + 100'000'005ULL, SafeGates()).outcome,
             CommandOutcome::Accepted);
    status = core.Snapshot();
    CHECK_EQ(status.state, ControlState::Engaged);
    CHECK_EQ(status.throttle, 0.0);
}

ETS2AP_TEST(MalformedCommandCannotRefreshWatchdog) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    auto malformed = Packet(2, kStartNs + 50'000'000ULL);
    malformed[57] = std::byte{1};
    CHECK_EQ(Accept(core, malformed, kStartNs + 50'000'000ULL).outcome,
             CommandOutcome::Rejected);

    core.Tick(kStartNs + 100'000'001ULL);
    CHECK_EQ(core.Snapshot().state, ControlState::Timeout);
    CHECK_EQ(core.Snapshot().reason, ControlReason::CommandTimeout);
}

ETS2AP_TEST(MonotonicClockRegressionFailsClosed) {
    ControlCore core;
    core.Connect(kStartNs);
    CHECK_EQ(Accept(core, Packet(1, kStartNs), kStartNs).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(2, kStartNs + 1, 0, 0.5, 0, true), kStartNs + 1,
                    SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK_EQ(Accept(core, Packet(3, kStartNs + 50'000'001ULL, 0, 0.5, 0, true),
                    kStartNs + 50'000'001ULL, SafeGates()).outcome,
             CommandOutcome::Accepted);
    CHECK(core.Snapshot().throttle > 0.0);

    core.Tick(kStartNs + 50'000'000ULL);
    CHECK_EQ(core.Snapshot().state, ControlState::Timeout);
    CHECK_EQ(core.Snapshot().reason, ControlReason::CommandTimeout);
    CHECK_EQ(core.Snapshot().throttle, 0.0);
}

ETS2AP_TEST_MAIN()
