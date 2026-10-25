// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/input/control_core.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace ets2ap::input {
namespace {

constexpr std::uint32_t kCommandMagic = 0x50413243U;
constexpr std::uint32_t kStatusMagic = 0x50413253U;
constexpr std::uint16_t kSupportedMajor = 1;
constexpr std::size_t kV1PrefixSize = 64;
constexpr std::size_t kMaximumPacketSize = 256;
constexpr std::uint64_t kWatchdogNs = 100'000'000ULL;
// [TUNE] Match control.v1; in-game confirmation is required before output integration.
constexpr double kSteeringRatePerSecond = 2.0;
constexpr double kThrottleRatePerSecond = 1.0;
constexpr double kBrakeRatePerSecond = 2.0;
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);

std::uint64_t ReadLittleEndian(std::span<const std::byte> bytes,
                               std::size_t offset, std::size_t width) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << (index * 8);
    }
    return value;
}

void WriteLittleEndian(std::array<std::byte, kV1PrefixSize>& bytes,
                       std::size_t offset, std::uint64_t value,
                       std::size_t width) noexcept {
    for (std::size_t index = 0; index < width; ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8)) & 0xFFU);
    }
}

bool IsNeutral(bool engage, double steering, double throttle, double brake) noexcept {
    return !engage && steering == 0.0 && throttle == 0.0 && brake == 0.0;
}

} // namespace

void ControlCore::Connect(std::uint64_t now_ns) noexcept {
    const std::lock_guard lock(mutex_);
    const bool reconnect = ever_connected_;
    connected_ = true;
    ever_connected_ = true;
    has_accepted_command_ = false;
    neutral_ready_ = false;
    last_receive_ns_ = now_ns;
    last_command_timestamp_ns_ = 0;
    last_slew_ns_ = now_ns;
    snapshot_.last_accepted_sequence = 0;
    ZeroOutputsLocked();

    if (manual_override_latched_) {
        snapshot_.state = ControlState::ManualOverrideLatched;
        snapshot_.reason = reconnect ? ControlReason::ClientDisconnected
                                     : ControlReason::ManualInput;
    } else {
        snapshot_.state = ControlState::Disengaged;
        snapshot_.reason = reconnect ? ControlReason::ClientDisconnected
                                     : (manual_input_active_ ? ControlReason::ManualInput
                                                             : ControlReason::None);
    }
    client_disconnected_reason_pending_ = reconnect;
}

CommandResult ControlCore::AcceptCommand(std::span<const std::byte> packet,
                                         std::uint64_t now_ns,
                                         const SafetyGates& gates) noexcept {
    const std::lock_guard lock(mutex_);
    if (!connected_) {
        return {CommandOutcome::ConnectionFatal, true};
    }

    ExpireIfNeededLocked(now_ns);

    double steering = 0.0;
    double throttle = 0.0;
    double brake = 0.0;
    bool engage = false;
    std::uint64_t sequence = 0;
    std::uint64_t timestamp = 0;
    bool version_mismatch = false;
    bool fatal_frame = false;
    if (!ReadCommandLocked(packet, now_ns, steering, throttle, brake, engage,
                           sequence, timestamp, version_mismatch, fatal_frame)) {
        const auto reason = version_mismatch ? ControlReason::VersionMismatch
                                             : ControlReason::MalformedPacket;
        RejectLocked(reason, fatal_frame || version_mismatch);
        return {fatal_frame || version_mismatch ? CommandOutcome::ConnectionFatal
                                                 : CommandOutcome::Rejected,
                fatal_frame || version_mismatch};
    }

    const bool neutral = IsNeutral(engage, steering, throttle, brake);
    const auto accept_metadata = [&] {
        has_accepted_command_ = true;
        snapshot_.last_accepted_sequence = sequence;
        last_command_timestamp_ns_ = timestamp;
        last_receive_ns_ = now_ns;
    };

    if (manual_override_latched_) {
        accept_metadata();
        snapshot_.state = ControlState::ManualOverrideLatched;
        snapshot_.reason = ControlReason::ManualInput;
        ZeroOutputsLocked();
        if (!manual_input_active_ && neutral) {
            manual_override_latched_ = false;
            neutral_ready_ = true;
            snapshot_.state = ControlState::Disengaged;
            snapshot_.reason = ControlReason::None;
            return {CommandOutcome::Accepted, false};
        }
        return {CommandOutcome::ManualOverride, false};
    }

    if (manual_input_active_ && engage) {
        accept_metadata();
        manual_override_latched_ = true;
        neutral_ready_ = false;
        snapshot_.state = ControlState::ManualOverrideLatched;
        snapshot_.reason = ControlReason::ManualInput;
        ZeroOutputsLocked();
        return {CommandOutcome::ManualOverride, false};
    }

    if (snapshot_.state == ControlState::Timeout) {
        accept_metadata();
        ZeroOutputsLocked();
        if (neutral) {
            neutral_ready_ = true;
            snapshot_.state = ControlState::Disengaged;
            snapshot_.reason = manual_input_active_ ? ControlReason::ManualInput
                                                    : ControlReason::None;
            return {CommandOutcome::Accepted, false};
        }
        return {CommandOutcome::SafetyBlocked, false};
    }

    if (snapshot_.state == ControlState::Rejected) {
        if (neutral) {
            accept_metadata();
            neutral_ready_ = true;
            snapshot_.state = ControlState::Disengaged;
            snapshot_.reason = manual_input_active_ ? ControlReason::ManualInput
                                                    : ControlReason::None;
            ZeroOutputsLocked();
            return {CommandOutcome::Accepted, false};
        }
        return {CommandOutcome::SafetyBlocked, false};
    }

    if (neutral) {
        accept_metadata();
        neutral_ready_ = true;
        snapshot_.state = ControlState::Disengaged;
        snapshot_.reason = manual_input_active_ ? ControlReason::ManualInput
                                                : ControlReason::None;
        ZeroOutputsLocked();
        return {CommandOutcome::Accepted, false};
    }

    if (snapshot_.state != ControlState::Engaged && !neutral_ready_) {
        accept_metadata();
        neutral_ready_ = false;
        ZeroOutputsLocked();
        return {CommandOutcome::SafetyBlocked, false};
    }

    if (!SafetyReadyLocked(gates)) {
        accept_metadata();
        neutral_ready_ = false;
        snapshot_.state = ControlState::Disengaged;
        snapshot_.reason = ControlReason::None;
        ZeroOutputsLocked();
        return {CommandOutcome::SafetyBlocked, false};
    }

    accept_metadata();
    const bool was_engaged = snapshot_.state == ControlState::Engaged;
    snapshot_.state = ControlState::Engaged;
    snapshot_.reason = ControlReason::None;
    if (brake > 0.0) throttle = 0.0;
    if (was_engaged) {
        ApplySlewLocked(steering, throttle, brake,
                        gates.maximum_steering_ratio, now_ns);
        if (snapshot_.brake > 0.0) snapshot_.throttle = 0.0;
    } else {
        neutral_ready_ = false;
        last_slew_ns_ = now_ns;
        ZeroOutputsLocked();
    }
    return {CommandOutcome::Accepted, false};
}

void ControlCore::Tick(std::uint64_t now_ns) noexcept {
    const std::lock_guard lock(mutex_);
    ExpireIfNeededLocked(now_ns);
}

void ControlCore::SetManualInput(bool active) noexcept {
    const std::lock_guard lock(mutex_);
    if (manual_input_active_ == active) return;
    manual_input_active_ = active;

    if (active) {
        if (snapshot_.state == ControlState::Engaged) {
            manual_override_latched_ = true;
        }
        if (manual_override_latched_) {
            snapshot_.state = ControlState::ManualOverrideLatched;
            snapshot_.reason = ControlReason::ManualInput;
            neutral_ready_ = false;
            ZeroOutputsLocked();
        } else if (snapshot_.state == ControlState::Disengaged) {
            snapshot_.reason = ControlReason::ManualInput;
        }
        return;
    }

    if (manual_override_latched_) {
        snapshot_.state = ControlState::ManualOverrideLatched;
        snapshot_.reason = ControlReason::ManualInput;
    } else if (snapshot_.state == ControlState::Disengaged &&
               snapshot_.reason == ControlReason::ManualInput) {
        snapshot_.reason = ControlReason::None;
    }
}

void ControlCore::Disconnect() noexcept {
    const std::lock_guard lock(mutex_);
    connected_ = false;
    neutral_ready_ = false;
    ZeroOutputsLocked();
    snapshot_.state = manual_override_latched_ ? ControlState::ManualOverrideLatched
                                               : ControlState::Disengaged;
    snapshot_.reason = ControlReason::ClientDisconnected;
    client_disconnected_reason_pending_ = true;
}

ControlSnapshot ControlCore::Snapshot() const noexcept {
    const std::lock_guard lock(mutex_);
    return snapshot_;
}

std::array<std::byte, 64> ControlCore::EncodeStatus(std::uint64_t timestamp_ns) noexcept {
    const std::lock_guard lock(mutex_);
    std::array<std::byte, kV1PrefixSize> packet{};
    WriteLittleEndian(packet, 0, kStatusMagic, 4);
    WriteLittleEndian(packet, 4, kSupportedMajor, 2);
    WriteLittleEndian(packet, 6, 0, 2);
    WriteLittleEndian(packet, 8, kV1PrefixSize, 4);
    WriteLittleEndian(packet, 16, snapshot_.last_accepted_sequence, 8);
    WriteLittleEndian(packet, 24, timestamp_ns, 8);
    WriteLittleEndian(packet, 32, static_cast<std::uint32_t>(snapshot_.state), 4);

    const auto status_reason = client_disconnected_reason_pending_
                                   ? ControlReason::ClientDisconnected
                                   : snapshot_.reason;
    WriteLittleEndian(packet, 36, static_cast<std::uint32_t>(status_reason), 4);
    const bool engaged = snapshot_.state == ControlState::Engaged;
    WriteLittleEndian(packet, 40, std::bit_cast<std::uint64_t>(engaged ? snapshot_.steering : 0.0), 8);
    WriteLittleEndian(packet, 48, std::bit_cast<std::uint64_t>(engaged ? snapshot_.throttle : 0.0), 8);
    WriteLittleEndian(packet, 56, std::bit_cast<std::uint64_t>(engaged ? snapshot_.brake : 0.0), 8);

    if (client_disconnected_reason_pending_) {
        client_disconnected_reason_pending_ = false;
        snapshot_.reason = (manual_override_latched_ ||
                            (snapshot_.state == ControlState::Disengaged && manual_input_active_))
                               ? ControlReason::ManualInput
                               : ControlReason::None;
    }
    return packet;
}

void ControlCore::RejectLocked(ControlReason reason, bool) noexcept {
    neutral_ready_ = false;
    ZeroOutputsLocked();
    if (manual_override_latched_) {
        snapshot_.state = ControlState::ManualOverrideLatched;
        snapshot_.reason = ControlReason::ManualInput;
        return;
    }
    snapshot_.state = ControlState::Rejected;
    snapshot_.reason = reason;
}

bool ControlCore::ReadCommandLocked(std::span<const std::byte> packet,
                                    std::uint64_t now_ns,
                                    double& steering, double& throttle, double& brake,
                                    bool& engage, std::uint64_t& sequence,
                                    std::uint64_t& timestamp, bool& version_mismatch,
                                    bool& fatal_frame) noexcept {
    steering = 0.0;
    throttle = 0.0;
    brake = 0.0;
    engage = false;
    sequence = 0;
    timestamp = 0;
    version_mismatch = false;
    fatal_frame = false;

    if (packet.size() < 16) {
        fatal_frame = true;
        return false;
    }
    const auto magic = ReadLittleEndian(packet, 0, 4);
    const auto major = ReadLittleEndian(packet, 4, 2);
    const auto minor = ReadLittleEndian(packet, 6, 2);
    const auto declared_size = ReadLittleEndian(packet, 8, 4);
    if (declared_size < kV1PrefixSize || declared_size > kMaximumPacketSize ||
        declared_size != packet.size()) {
        fatal_frame = true;
        return false;
    }
    if (magic != kCommandMagic) return false;
    if (major != kSupportedMajor) {
        version_mismatch = true;
        return false;
    }
    if (minor == 0 && declared_size != kV1PrefixSize) {
        fatal_frame = true;
        return false;
    }
    if (packet.size() < kV1PrefixSize) {
        fatal_frame = true;
        return false;
    }
    if (ReadLittleEndian(packet, 12, 4) != 0 ||
        ReadLittleEndian(packet, 57, 3) != 0 ||
        ReadLittleEndian(packet, 60, 4) != 0) {
        return false;
    }

    sequence = ReadLittleEndian(packet, 16, 8);
    timestamp = ReadLittleEndian(packet, 24, 8);
    steering = std::bit_cast<double>(ReadLittleEndian(packet, 32, 8));
    throttle = std::bit_cast<double>(ReadLittleEndian(packet, 40, 8));
    brake = std::bit_cast<double>(ReadLittleEndian(packet, 48, 8));
    const auto engage_byte = ReadLittleEndian(packet, 56, 1);
    if (engage_byte > 1) return false;
    engage = engage_byte == 1;

    if (sequence == 0 || sequence == std::numeric_limits<std::uint64_t>::max() ||
        (!has_accepted_command_ && sequence != 1) ||
        (has_accepted_command_ && sequence <= snapshot_.last_accepted_sequence)) {
        return false;
    }
    if (timestamp == 0 || timestamp > now_ns || now_ns - timestamp > kWatchdogNs ||
        (has_accepted_command_ && timestamp <= last_command_timestamp_ns_)) {
        return false;
    }
    if (!std::isfinite(steering) || !std::isfinite(throttle) || !std::isfinite(brake) ||
        steering < -1.0 || steering > 1.0 || throttle < 0.0 || throttle > 1.0 ||
        brake < 0.0 || brake > 1.0) {
        return false;
    }
    if (steering == 0.0) steering = 0.0;
    if (throttle == 0.0) throttle = 0.0;
    if (brake == 0.0) brake = 0.0;
    if (!engage && !IsNeutral(engage, steering, throttle, brake)) return false;
    if (!has_accepted_command_ && !IsNeutral(engage, steering, throttle, brake)) return false;
    return true;
}

bool ControlCore::SafetyReadyLocked(const SafetyGates& gates) const noexcept {
    return gates.telemetry_fresh && gates.speed_limit_available &&
           gates.explicit_ui_authorized && std::isfinite(gates.maximum_steering_ratio) &&
           gates.maximum_steering_ratio >= 0.0 && gates.maximum_steering_ratio <= 1.0;
}

void ControlCore::ExpireIfNeededLocked(std::uint64_t now_ns) noexcept {
    if (!connected_ || manual_override_latched_ || snapshot_.state == ControlState::Timeout) {
        return;
    }
    if (now_ns < last_receive_ns_) {
        ZeroOutputsLocked();
        neutral_ready_ = false;
        snapshot_.state = ControlState::Timeout;
        snapshot_.reason = ControlReason::CommandTimeout;
        return;
    }
    if (now_ns - last_receive_ns_ <= kWatchdogNs) return;
    ZeroOutputsLocked();
    neutral_ready_ = false;
    snapshot_.state = ControlState::Timeout;
    snapshot_.reason = ControlReason::CommandTimeout;
}

void ControlCore::ZeroOutputsLocked() noexcept {
    snapshot_.steering = 0.0;
    snapshot_.throttle = 0.0;
    snapshot_.brake = 0.0;
}

void ControlCore::ApplySlewLocked(double steering, double throttle, double brake,
                                  double steering_cap, std::uint64_t now_ns) noexcept {
    const double elapsed_seconds = now_ns >= last_slew_ns_
                                       ? static_cast<double>(now_ns - last_slew_ns_) / 1'000'000'000.0
                                       : 0.0;
    const double steering_target = std::clamp(steering, -steering_cap, steering_cap);
    const auto limit_delta = [elapsed_seconds](double current, double target,
                                                double rate) noexcept {
        const double maximum_delta = rate * elapsed_seconds;
        const double one_ulp_tolerance = std::nextafter(
            maximum_delta, std::numeric_limits<double>::infinity());
        if (std::abs(target - current) <= one_ulp_tolerance) return target;
        return std::clamp(target, current - maximum_delta, current + maximum_delta);
    };
    snapshot_.steering = limit_delta(snapshot_.steering, steering_target,
                                     kSteeringRatePerSecond);
    snapshot_.steering = std::clamp(snapshot_.steering, -steering_cap, steering_cap);
    snapshot_.throttle = limit_delta(snapshot_.throttle, throttle,
                                     kThrottleRatePerSecond);
    snapshot_.brake = limit_delta(snapshot_.brake, brake, kBrakeRatePerSecond);
    last_slew_ns_ = now_ns;
}

} // namespace ets2ap::input
