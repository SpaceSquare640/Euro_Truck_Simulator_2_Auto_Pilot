// Edited by: OpenAI-Codex (OpenAI-Codex_Responsible: Source_Code)
#pragma once

#include "ets2ap/input/control_core.h"
#include "ets2ap/plugin/physical_input_monitor.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>

namespace ets2ap::plugin {

struct ControlHostOptions {
    std::function<std::optional<std::uint64_t>()> now_ns{};
    std::function<bool(const input::ControlSnapshot&, std::uint64_t)> apply_output{};
    std::function<void()> force_neutral{};
    std::function<PhysicalInputStatus()> sample_physical_input{};
    std::chrono::milliseconds tick_interval{10};
};

class ControlHost {
public:
    explicit ControlHost(ControlHostOptions options);
    ~ControlHost();
    ControlHost(const ControlHost&) = delete;
    ControlHost& operator=(const ControlHost&) = delete;

    bool Start() noexcept;
    void Stop() noexcept;
    bool IsRunning() const noexcept;
    PhysicalInputStatus GetPhysicalInputStatus() noexcept;
    bool Connect() noexcept;
    input::CommandResult AcceptCommand(std::span<const std::byte> packet,
                                       const input::SafetyGates& gates = {}) noexcept;
    input::ControlSnapshot Snapshot() noexcept;
    std::optional<std::array<std::byte, 68>> EncodeStatus() noexcept;
    void SetManualInput(bool active) noexcept;
    void Disconnect() noexcept;

private:
    void Run(std::stop_token stop_token) noexcept;
    void TickAndPublish() noexcept;
    void RefreshPhysicalInputLocked() noexcept;
    std::optional<std::uint64_t> ReadNowNsLocked() noexcept;
    bool PublishLocked(std::uint64_t now_ns) noexcept;
    void ForceNeutralLocked() noexcept;

    ControlHostOptions options_{};
    mutable std::mutex lifecycle_mutex_{};
    std::mutex operation_mutex_{};
    input::ControlCore core_{};
    PhysicalInputStatus physical_input_status_{};
    std::jthread worker_{};
    std::atomic<bool> running_{false};
};

ControlHost& GlobalControlHost() noexcept;

} // namespace ets2ap::plugin
