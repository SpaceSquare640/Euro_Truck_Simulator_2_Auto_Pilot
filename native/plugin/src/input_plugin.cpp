// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/plugin/input_plugin.h"

#include <eurotrucks2/scssdk_eut2.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <Windows.h>

namespace ets2ap::plugin {
namespace {
constexpr std::uint64_t kCommandTimeoutNs = 100'000'000ULL;
constexpr std::uint64_t kSteeringMask = 0xFFFFULL;
constexpr std::uint64_t kAxisMask = 0xFFFFULL;
constexpr unsigned kThrottleShift = 16;
constexpr unsigned kBrakeShift = 32;
constexpr unsigned kEngagedShift = 48;

constexpr std::array<scs_input_device_input_t, 3> kInputs{{
    {"steering", "Steering", SCS_VALUE_TYPE_float, 0},
    {"aforward", "Forward", SCS_VALUE_TYPE_float, 0},
    {"abackward", "Backward", SCS_VALUE_TYPE_float, 0},
}};

std::optional<std::uint64_t> QpcNowNs() noexcept {
    LARGE_INTEGER ticks{};
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceCounter(&ticks) || !QueryPerformanceFrequency(&frequency) ||
        ticks.QuadPart < 0 || frequency.QuadPart <= 0) return std::nullopt;
    const auto ticks_u = static_cast<std::uint64_t>(ticks.QuadPart);
    const auto frequency_u = static_cast<std::uint64_t>(frequency.QuadPart);
    const auto seconds = ticks_u / frequency_u;
    const auto remainder = ticks_u % frequency_u;
    if (seconds > std::numeric_limits<std::uint64_t>::max() / 1'000'000'000ULL)
        return std::nullopt;
    const auto whole = seconds * 1'000'000'000ULL;
    const auto fraction = (remainder * 1'000'000'000ULL) / frequency_u;
    if (whole > std::numeric_limits<std::uint64_t>::max() - fraction) return std::nullopt;
    return whole + fraction;
}

InputPlugin g_input_plugin;

bool ValidCommand(const InputCommand& command) noexcept {
    if (!std::isfinite(command.steering) || !std::isfinite(command.throttle) ||
        !std::isfinite(command.brake) || command.steering < -1.0f || command.steering > 1.0f ||
        command.throttle < 0.0f || command.throttle > 1.0f ||
        command.brake < 0.0f || command.brake > 1.0f || command.timestamp_ns == 0) return false;
    return command.engaged ||
        (command.steering == 0.0f && command.throttle == 0.0f && command.brake == 0.0f);
}
} // namespace

scs_result_t InputPlugin::Init(scs_u32_t version, const scs_input_init_params_t* params,
                               InputPluginOptions options) {
    if (version != SCS_INPUT_VERSION_1_00) return SCS_RESULT_unsupported;
    if (!params || initialized_.load(std::memory_order_acquire)) return SCS_RESULT_invalid_parameter;

    const auto* init = static_cast<const scs_input_init_params_v100_t*>(params);
    if (!init->common.game_id || !init->register_device) return SCS_RESULT_invalid_parameter;
    if (std::strcmp(init->common.game_id, SCS_GAME_ID_EUT2) != 0) return SCS_RESULT_unsupported;

    options_ = std::move(options);
    if (!options_.now_ns) options_.now_ns = QpcNowNs;
    log_ = init->common.log;
    Neutralize();
    next_input_index_ = 0;
    frame_command_ = 0;
    frame_neutral_latched_ = true;

    device_.name = "ets2ap_autopilot";
    device_.display_name = "ETS2AP Autopilot";
    device_.type = SCS_INPUT_DEVICE_TYPE_semantical;
    device_.input_count = static_cast<scs_u32_t>(kInputs.size());
    device_.inputs = kInputs.data();
    device_.callback_context = this;
    device_.input_active_callback = nullptr;
    device_.input_event_callback = &OnInputEvent;

    const auto result = init->register_device(&device_);
    if (result != SCS_RESULT_ok) {
        if (log_) log_(SCS_LOG_TYPE_error, "SCS semantic input device registration failed");
        log_ = nullptr;
        options_ = {};
        return result;
    }
    initialized_.store(true, std::memory_order_release);
    if (log_) log_(SCS_LOG_TYPE_message,
        "SCS semantic input device registered; game-profile mix support requires verification");
    return SCS_RESULT_ok;
}

void InputPlugin::Shutdown() noexcept {
    forced_neutral_.store(true, std::memory_order_release);
    initialized_.store(false, std::memory_order_release);
    Neutralize();
    log_ = nullptr;
}

bool InputPlugin::SetCommand(const InputCommand& command) noexcept {
    try {
        std::lock_guard lock(writer_mutex_);
        if (!initialized_.load(std::memory_order_acquire) || !ValidCommand(command)) {
            forced_neutral_.store(true, std::memory_order_release);
            NeutralizeLocked();
            return false;
        }

        const auto now = NowNs();
        if (!now || command.timestamp_ns > *now || *now - command.timestamp_ns > kCommandTimeoutNs ||
            command.timestamp_ns <= last_command_timestamp_ns_) {
            forced_neutral_.store(true, std::memory_order_release);
            NeutralizeLocked();
            return false;
        }

        forced_neutral_.store(true, std::memory_order_release);
        auto effective = command;
        if (effective.brake > 0.0f) effective.throttle = 0.0f;
        revision_.fetch_add(1, std::memory_order_acq_rel);
        packed_command_.store(Pack(effective), std::memory_order_relaxed);
        command_timestamp_ns_.store(command.timestamp_ns, std::memory_order_relaxed);
        last_command_timestamp_ns_ = command.timestamp_ns;
        revision_.fetch_add(1, std::memory_order_release);
        forced_neutral_.store(!command.engaged, std::memory_order_release);
        return true;
    } catch (...) {
        forced_neutral_.store(true, std::memory_order_release);
        return false;
    }
}

std::optional<std::uint64_t> InputPlugin::NowNs() const noexcept {
    try {
        return options_.now_ns ? options_.now_ns() : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

void InputPlugin::Neutralize() noexcept {
    forced_neutral_.store(true, std::memory_order_release);
    try {
        std::lock_guard lock(writer_mutex_);
        NeutralizeLocked();
    } catch (...) {
        // The atomic fail-safe keeps the callback output neutral if locking fails.
    }
}

void InputPlugin::NeutralizeLocked() noexcept {
    revision_.fetch_add(1, std::memory_order_acq_rel);
    packed_command_.store(0, std::memory_order_relaxed);
    command_timestamp_ns_.store(0, std::memory_order_relaxed);
    revision_.fetch_add(1, std::memory_order_release);
}

std::uint64_t InputPlugin::ReadPackedCommand() const noexcept {
    if (!initialized_.load(std::memory_order_acquire)) return 0;
    if (forced_neutral_.load(std::memory_order_acquire)) return 0;
    const auto before = revision_.load(std::memory_order_acquire);
    if ((before & 1ULL) != 0) return 0;
    const auto packed = packed_command_.load(std::memory_order_relaxed);
    const auto timestamp = command_timestamp_ns_.load(std::memory_order_relaxed);
    const auto after = revision_.load(std::memory_order_acquire);
    if (before != after || (after & 1ULL) != 0 ||
        forced_neutral_.load(std::memory_order_acquire)) return 0;
    if ((packed & (1ULL << kEngagedShift)) == 0) return 0;
    try {
        const auto now = options_.now_ns ? options_.now_ns() : std::nullopt;
        if (!now || timestamp == 0 || timestamp > *now || *now - timestamp > kCommandTimeoutNs)
            return 0;
    } catch (...) {
        return 0;
    }
    return packed;
}

std::uint64_t InputPlugin::Pack(const InputCommand& command) noexcept {
    const auto steering = static_cast<std::int16_t>(std::lround(command.steering * 32767.0f));
    const auto steering_bits = static_cast<std::uint16_t>(steering);
    const auto throttle = static_cast<std::uint16_t>(std::lround(command.throttle * 65535.0f));
    const auto brake = static_cast<std::uint16_t>(std::lround(command.brake * 65535.0f));
    return static_cast<std::uint64_t>(steering_bits) |
        (static_cast<std::uint64_t>(throttle) << kThrottleShift) |
        (static_cast<std::uint64_t>(brake) << kBrakeShift) |
        (1ULL << kEngagedShift);
}

InputCommand InputPlugin::Unpack(std::uint64_t packed) noexcept {
    const auto steering = static_cast<std::int16_t>(packed & kSteeringMask);
    const auto throttle = static_cast<std::uint16_t>((packed >> kThrottleShift) & kAxisMask);
    const auto brake = static_cast<std::uint16_t>((packed >> kBrakeShift) & kAxisMask);
    return {static_cast<float>(steering) / 32767.0f,
            static_cast<float>(throttle) / 65535.0f,
            static_cast<float>(brake) / 65535.0f,
            (packed & (1ULL << kEngagedShift)) != 0,
            0};
}

SCSAPI_RESULT InputPlugin::OnInputEvent(scs_input_event_t* event_info, scs_u32_t flags,
                                        scs_context_t context) noexcept {
    auto* plugin = static_cast<InputPlugin*>(context);
    if (!plugin || !event_info) return SCS_RESULT_not_found;
    if (flags & SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame) {
        plugin->next_input_index_ = 0;
        plugin->frame_command_ = plugin->ReadPackedCommand();
        plugin->frame_neutral_latched_ = plugin->frame_command_ == 0;
    }
    if (plugin->next_input_index_ >= kInputs.size()) return SCS_RESULT_not_found;

    const auto index = plugin->next_input_index_++;
    if (plugin->forced_neutral_.load(std::memory_order_acquire))
        plugin->frame_neutral_latched_ = true;
    const auto packed = plugin->frame_neutral_latched_ ? 0 : plugin->frame_command_;
    const auto now = packed != 0 ? plugin->Unpack(packed) : InputCommand{};
    event_info->input_index = index;
    switch (index) {
    case 0: event_info->value_float.value = now.engaged ? now.steering : 0.0f; break;
    case 1: event_info->value_float.value = now.engaged ? now.throttle : 0.0f; break;
    case 2: event_info->value_float.value = now.engaged ? now.brake : 0.0f; break;
    default: return SCS_RESULT_not_found;
    }
    return SCS_RESULT_ok;
}

InputPlugin& GlobalInputPlugin() noexcept {
    return g_input_plugin;
}

} // namespace ets2ap::plugin
