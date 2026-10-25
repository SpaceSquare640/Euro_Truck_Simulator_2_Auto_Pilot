// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/plugin/control_host.h"

#include "ets2ap/plugin/input_plugin.h"

#include <utility>

namespace ets2ap::plugin {
namespace {

constexpr auto kMinimumTickInterval = std::chrono::milliseconds{1};
constexpr auto kMaximumTickInterval = std::chrono::milliseconds{10};

ControlHostOptions GlobalOptions() {
    ControlHostOptions options{};
    options.now_ns = [] { return GlobalInputPlugin().NowNs(); };
    options.apply_output = [](const input::ControlSnapshot& snapshot, std::uint64_t timestamp_ns) {
        const bool engaged = snapshot.state == input::ControlState::Engaged;
        const InputCommand command{
            engaged ? static_cast<float>(snapshot.steering) : 0.0f,
            engaged ? static_cast<float>(snapshot.throttle) : 0.0f,
            engaged ? static_cast<float>(snapshot.brake) : 0.0f,
            engaged,
            timestamp_ns};
        return GlobalInputPlugin().SetCommand(command);
    };
    options.force_neutral = [] { GlobalInputPlugin().Neutralize(); };
    return options;
}

} // namespace

ControlHost::ControlHost(ControlHostOptions options) : options_(std::move(options)) {}

ControlHost::~ControlHost() {
    Stop();
}

bool ControlHost::Start() noexcept {
    try {
        const std::lock_guard lifecycle_lock(lifecycle_mutex_);
        if (running_.load(std::memory_order_acquire)) return true;

        const std::lock_guard operation_lock(operation_mutex_);
        if (!options_.now_ns || !options_.apply_output || !options_.force_neutral ||
            options_.tick_interval < kMinimumTickInterval ||
            options_.tick_interval > kMaximumTickInterval) {
            ForceNeutralLocked();
            return false;
        }

        const auto now_ns = ReadNowNsLocked();
        if (!now_ns || *now_ns == 0) {
            ForceNeutralLocked();
            return false;
        }

        core_.Disconnect();
        if (!PublishLocked(*now_ns)) return false;

        worker_ = std::jthread([this](std::stop_token stop_token) { Run(stop_token); });
        running_.store(true, std::memory_order_release);
        return true;
    } catch (...) {
        try {
            const std::lock_guard operation_lock(operation_mutex_);
            core_.Disconnect();
            ForceNeutralLocked();
        } catch (...) {
        }
        return false;
    }
}

void ControlHost::Stop() noexcept {
    try {
        const std::lock_guard lifecycle_lock(lifecycle_mutex_);
        if (worker_.joinable()) {
            worker_.request_stop();
            running_.store(false, std::memory_order_release);
            worker_.join();
        }
        running_.store(false, std::memory_order_release);

        const std::lock_guard operation_lock(operation_mutex_);
        core_.Disconnect();
        const auto now_ns = ReadNowNsLocked();
        if (now_ns && *now_ns != 0) {
            (void)PublishLocked(*now_ns);
        }
        ForceNeutralLocked();
    } catch (...) {
        running_.store(false, std::memory_order_release);
        try {
            if (options_.force_neutral) options_.force_neutral();
        } catch (...) {
        }
    }
}

bool ControlHost::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

bool ControlHost::Connect() noexcept {
    if (!IsRunning()) return false;
    try {
        const std::lock_guard lock(operation_mutex_);
        if (!IsRunning()) return false;
        const auto now_ns = ReadNowNsLocked();
        if (!now_ns || *now_ns == 0) {
            core_.Disconnect();
            ForceNeutralLocked();
            return false;
        }
        core_.Connect(*now_ns);
        return PublishLocked(*now_ns);
    } catch (...) {
        ForceNeutralLocked();
        return false;
    }
}

input::CommandResult ControlHost::AcceptCommand(std::span<const std::byte> packet,
                                                const input::SafetyGates& gates) noexcept {
    if (!IsRunning()) return {input::CommandOutcome::ConnectionFatal, true};
    try {
        const std::lock_guard lock(operation_mutex_);
        if (!IsRunning()) return {input::CommandOutcome::ConnectionFatal, true};
        const auto now_ns = ReadNowNsLocked();
        if (!now_ns || *now_ns == 0) {
            core_.Disconnect();
            ForceNeutralLocked();
            return {input::CommandOutcome::ConnectionFatal, true};
        }

        auto result = core_.AcceptCommand(packet, *now_ns, gates);
        if (!PublishLocked(*now_ns)) {
            return {input::CommandOutcome::ConnectionFatal, true};
        }
        return result;
    } catch (...) {
        ForceNeutralLocked();
        return {input::CommandOutcome::ConnectionFatal, true};
    }
}

void ControlHost::SetManualInput(bool active) noexcept {
    try {
        const std::lock_guard lock(operation_mutex_);
        core_.SetManualInput(active);
        if (!IsRunning()) {
            ForceNeutralLocked();
            return;
        }

        const auto now_ns = ReadNowNsLocked();
        if (!now_ns || *now_ns == 0 || !PublishLocked(*now_ns)) {
            core_.Disconnect();
            ForceNeutralLocked();
        }
    } catch (...) {
        ForceNeutralLocked();
    }
}

void ControlHost::Disconnect() noexcept {
    try {
        const std::lock_guard lock(operation_mutex_);
        core_.Disconnect();
        const auto now_ns = ReadNowNsLocked();
        if (!now_ns || *now_ns == 0 || !PublishLocked(*now_ns)) {
            ForceNeutralLocked();
        }
    } catch (...) {
        ForceNeutralLocked();
    }
}

void ControlHost::Run(std::stop_token stop_token) noexcept {
    try {
        auto next_tick = std::chrono::steady_clock::now() + options_.tick_interval;
        while (!stop_token.stop_requested()) {
            std::this_thread::sleep_until(next_tick);
            if (stop_token.stop_requested()) break;
            TickAndPublish();

            const auto now = std::chrono::steady_clock::now();
            next_tick += options_.tick_interval;
            if (next_tick <= now) next_tick = now + options_.tick_interval;
        }
    } catch (...) {
        running_.store(false, std::memory_order_release);
        try {
            const std::lock_guard lock(operation_mutex_);
            core_.Disconnect();
            ForceNeutralLocked();
        } catch (...) {
        }
    }
}

void ControlHost::TickAndPublish() noexcept {
    try {
        const std::lock_guard lock(operation_mutex_);
        const auto now_ns = ReadNowNsLocked();
        if (!now_ns || *now_ns == 0) {
            core_.Disconnect();
            ForceNeutralLocked();
            return;
        }
        core_.Tick(*now_ns);
        if (!PublishLocked(*now_ns)) core_.Disconnect();
    } catch (...) {
        ForceNeutralLocked();
    }
}

std::optional<std::uint64_t> ControlHost::ReadNowNsLocked() noexcept {
    try {
        return options_.now_ns ? options_.now_ns() : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

bool ControlHost::PublishLocked(std::uint64_t now_ns) noexcept {
    auto snapshot = core_.Snapshot();
    if (snapshot.state != input::ControlState::Engaged) {
        snapshot.steering = 0.0;
        snapshot.throttle = 0.0;
        snapshot.brake = 0.0;
    }

    try {
        if (options_.apply_output && options_.apply_output(snapshot, now_ns)) return true;
    } catch (...) {
    }

    core_.Disconnect();
    ForceNeutralLocked();
    return false;
}

void ControlHost::ForceNeutralLocked() noexcept {
    try {
        if (options_.force_neutral) options_.force_neutral();
    } catch (...) {
    }
}

ControlHost& GlobalControlHost() noexcept {
    static ControlHost host{GlobalOptions()};
    return host;
}

} // namespace ets2ap::plugin
