// Edited by: OpenAI-Codex_Responsible: Source_Code
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace ets2ap::input {

enum class ControlState : std::uint32_t {
    Disengaged = 0,
    Engaged = 1,
    ManualOverrideLatched = 2,
    Timeout = 3,
    Rejected = 4,
};

enum class ControlReason : std::uint32_t {
    None = 0,
    ManualInput = 1,
    CommandTimeout = 2,
    MalformedPacket = 3,
    VersionMismatch = 4,
    ClientDisconnected = 5,
    TelemetryUnavailable = 6,
};

enum class CommandOutcome {
    Accepted,
    SafetyBlocked,
    ManualOverride,
    Rejected,
    ConnectionFatal,
};

struct SafetyGates {
    bool telemetry_fresh{};
    bool speed_limit_available{};
    bool explicit_ui_authorized{};
    double maximum_steering_ratio{};
    bool perception_ready{};
    bool input_monitor_healthy{};
    bool game_focused{};
    bool front_clear{};
    bool manual_input_active{};
};

enum CommandSafetyFlag : std::uint32_t {
    UiAuthorized = 1U << 0,
    PerceptionReady = 1U << 1,
    InputMonitorHealthy = 1U << 2,
    GameFocused = 1U << 3,
    FrontClear = 1U << 4,
    ManualInputActive = 1U << 5,
};
inline constexpr std::uint32_t kKnownCommandSafetyFlags = 0x3FU;

struct ControlSnapshot {
    std::uint64_t last_accepted_sequence{};
    ControlState state{ControlState::Disengaged};
    ControlReason reason{ControlReason::None};
    double steering{};
    double throttle{};
    double brake{};
};

enum ControlCapability : std::uint32_t {
    SafetyFlagsV11 = 1U << 0,
};
inline constexpr std::uint32_t kKnownControlCapabilities = 0x1U;

struct CommandResult {
    CommandOutcome outcome{CommandOutcome::Rejected};
    bool close_connection{};
};

class ControlCore {
public:
    void Connect(std::uint64_t now_ns) noexcept;
    CommandResult AcceptCommand(std::span<const std::byte> packet,
                                std::uint64_t now_ns,
                                const SafetyGates& gates = {}) noexcept;
    void Tick(std::uint64_t now_ns) noexcept;
    void SetManualInput(bool active) noexcept;
    void Disconnect() noexcept;
    ControlSnapshot Snapshot() const noexcept;
    std::array<std::byte, 68> EncodeStatus(std::uint64_t timestamp_ns) noexcept;

private:
    void RejectLocked(ControlReason reason, bool close_connection) noexcept;
    bool ReadCommandLocked(std::span<const std::byte> packet,
                           std::uint64_t now_ns,
                           double& steering, double& throttle, double& brake,
                           bool& engage, std::uint64_t& sequence,
                           std::uint64_t& timestamp, std::uint32_t& safety_flags,
                           bool& version_mismatch,
                           bool& fatal_frame) noexcept;
    bool SafetyReadyLocked(const SafetyGates& gates) const noexcept;
    void SetManualInputLocked(bool active) noexcept;
    void ExpireIfNeededLocked(std::uint64_t now_ns) noexcept;
    void ZeroOutputsLocked() noexcept;
    void ApplySlewLocked(double steering, double throttle, double brake,
                         double steering_cap, std::uint64_t now_ns) noexcept;

    mutable std::mutex mutex_{};
    bool connected_{};
    bool ever_connected_{};
    bool has_accepted_command_{};
    bool neutral_ready_{};
    bool manual_input_active_{};
    bool manual_override_latched_{};
    bool client_disconnected_reason_pending_{};
    std::uint64_t last_receive_ns_{};
    std::uint64_t last_command_timestamp_ns_{};
    std::uint64_t last_slew_ns_{};
    ControlSnapshot snapshot_{};
};

} // namespace ets2ap::input
