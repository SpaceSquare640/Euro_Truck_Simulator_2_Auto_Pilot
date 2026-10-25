// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/plugin/telemetry_plugin.h"

#include "ets2ap/telemetry/convert.h"

#include <eurotrucks2/scssdk_eut2.h>
#include <eurotrucks2/scssdk_telemetry_eut2.h>
#include <Windows.h>

#include <cstring>
#include <utility>

namespace ets2ap::plugin {
namespace {
using namespace telemetry;

struct ChannelSpec { const char* name; scs_value_type_t type; };
constexpr std::array<ChannelSpec, 17> kChannels{{
    {SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm, SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_displayed_gear, SCS_VALUE_TYPE_s32},
    {SCS_TELEMETRY_TRUCK_CHANNEL_engine_gear, SCS_VALUE_TYPE_s32},
    {SCS_TELEMETRY_TRUCK_CHANNEL_input_steering, SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_effective_steering, SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_input_throttle, SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle, SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_input_brake, SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_effective_brake, SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_VALUE_TYPE_dplacement},
    {SCS_TELEMETRY_TRUCK_CHANNEL_local_linear_velocity, SCS_VALUE_TYPE_fvector},
    {SCS_TELEMETRY_TRUCK_CHANNEL_local_angular_velocity, SCS_VALUE_TYPE_fvector},
    {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_distance, SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_time, SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_speed_limit, SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_cruise_control, SCS_VALUE_TYPE_float},
}};

std::optional<std::uint64_t> QpcNowNs() noexcept {
    LARGE_INTEGER ticks{};
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceCounter(&ticks) || !QueryPerformanceFrequency(&frequency)) return std::nullopt;
    return QpcTicksToNs(ticks.QuadPart, frequency.QuadPart);
}

const char* MappingFailureReason(MappingError error) noexcept {
    switch (error) {
    case MappingError::TokenUnavailable: return "Telemetry mapping: token unavailable";
    case MappingError::SecuritySetupFailed: return "Telemetry mapping: security setup failed";
    case MappingError::SecurityMismatch: return "Telemetry mapping: security mismatch";
    case MappingError::MutexUnavailable: return "Telemetry mapping: writer mutex unavailable";
    case MappingError::MappingUnavailable: return "Telemetry mapping: mapping unavailable";
    case MappingError::ViewUnavailable: return "Telemetry mapping: view unavailable";
    case MappingError::ViewTooSmall: return "Telemetry mapping: view too small";
    case MappingError::ClockUnavailable: return "Telemetry mapping: clock unavailable";
    case MappingError::NotOpen: return "Telemetry mapping: not open";
    case MappingError::None: return "Telemetry mapping: unspecified failure";
    }
    return "Telemetry mapping: unspecified failure";
}
} // namespace

void TelemetryPlugin::Log(scs_log_type_t type, const char* message) const {
    if (log_) log_(type, message);
}

scs_result_t TelemetryPlugin::Init(const scs_telemetry_init_params_v100_t* params,
                                   PluginOptions options) {
    if (!params || !params->common.game_id) return SCS_RESULT_invalid_parameter;
    if (mapping_) return SCS_RESULT_generic_error;
    log_ = params->common.log;
    if (std::strcmp(params->common.game_id, SCS_GAME_ID_EUT2) != 0) {
        Log(SCS_LOG_TYPE_error, "Unsupported game for ETS2 telemetry mapping");
        log_ = nullptr;
        return SCS_RESULT_unsupported;
    }
    options_ = std::move(options);
    if (!options_.now_ns) options_.now_ns = QpcNowNs;
    state_.Reset();
    mapping_ = std::make_unique<TelemetryMapping>(options_.mapping_names);
    const auto created = mapping_->Create();
    if (!created) {
        Log(SCS_LOG_TYPE_error, MappingFailureReason(created.error));
        mapping_.reset();
        log_ = nullptr;
        return SCS_RESULT_generic_error;
    }

    constexpr std::array<scs_event_t, 4> events{{
        SCS_TELEMETRY_EVENT_paused, SCS_TELEMETRY_EVENT_started,
        SCS_TELEMETRY_EVENT_frame_end, SCS_TELEMETRY_EVENT_configuration}};
    if (!params->register_for_event || !params->register_for_channel) {
        Log(SCS_LOG_TYPE_error, "Telemetry SDK registration unavailable");
        Shutdown();
        return SCS_RESULT_generic_error;
    }
    for (const auto event : events) {
        if (params->register_for_event(event, &OnEvent, this) != SCS_RESULT_ok) {
            Log(SCS_LOG_TYPE_error, "Telemetry event registration failed");
            Shutdown();
            return SCS_RESULT_generic_error;
        }
    }
    for (std::size_t i = 0; i < kChannels.size(); ++i) {
        channel_contexts_[i] = {this, i};
        const auto& channel = kChannels[i];
        if (params->register_for_channel(channel.name, SCS_U32_NIL, channel.type,
                SCS_TELEMETRY_CHANNEL_FLAG_no_value, &OnChannel, &channel_contexts_[i]) != SCS_RESULT_ok) {
            Log(SCS_LOG_TYPE_warning, "Telemetry channel registration failed");
        }
    }
    return SCS_RESULT_ok;
}

void TelemetryPlugin::Publish() {
    if (!mapping_) return;
    const auto timestamp = options_.now_ns();
    if (!timestamp) {
        Log(SCS_LOG_TYPE_error, "Telemetry clock unavailable");
        return;
    }
    const auto sample = state_.Build(*timestamp);
    if (!mapping_->Publish(sample)) {
        Log(SCS_LOG_TYPE_error, "Telemetry publication failed");
        return;
    }
    published_validity_flags_.store(sample.validity_flags, std::memory_order_relaxed);
    published_paused_.store(sample.paused, std::memory_order_relaxed);
    published_timestamp_ns_.store(sample.timestamp_ns, std::memory_order_release);
}

TelemetryControlSnapshot TelemetryPlugin::ControlSnapshot(std::uint64_t now_ns) const noexcept {
    const auto timestamp = published_timestamp_ns_.load(std::memory_order_acquire);
    const auto flags = published_validity_flags_.load(std::memory_order_relaxed);
    const bool paused_available = (flags & static_cast<std::uint32_t>(ValidityBits::Paused)) != 0;
    const bool speed_limit_available =
        (flags & static_cast<std::uint32_t>(ValidityBits::SpeedLimit)) != 0;
    const bool paused = paused_available &&
        published_paused_.load(std::memory_order_relaxed) != 0;
    const bool fresh = timestamp != 0 && timestamp <= now_ns &&
        now_ns - timestamp <= 250'000'000ULL && paused_available && !paused;
    return {fresh, paused, speed_limit_available};
}

void TelemetryPlugin::HandleEvent(scs_event_t event) {
    switch (event) {
    case SCS_TELEMETRY_EVENT_paused: state_.SetPaused(true); Publish(); break;
    case SCS_TELEMETRY_EVENT_started: state_.SetPaused(false); Publish(); break;
    case SCS_TELEMETRY_EVENT_frame_end: Publish(); break;
    case SCS_TELEMETRY_EVENT_configuration:
        // Conservative reset on every configuration callback; the contract does not define a narrower trigger.
        state_.Reset(); Publish(); break;
    default: break;
    }
}

SCSAPI_VOID TelemetryPlugin::OnEvent(scs_event_t event, const void*, scs_context_t context) {
    auto* plugin = static_cast<TelemetryPlugin*>(context);
    if (!plugin) return;
    try { plugin->HandleEvent(event); }
    catch (...) {
        try { plugin->Log(SCS_LOG_TYPE_error, "Telemetry event callback failed"); } catch (...) {}
        try { plugin->Shutdown(); } catch (...) {}
    }
}

void TelemetryPlugin::HandleChannel(std::size_t index, const scs_value_t* value) noexcept {
    if (index >= kChannels.size()) return;
    if (value && value->type != kChannels[index].type) value = nullptr;
    const float* f = value ? &value->value_float.value : nullptr;
    const std::int32_t* i = value ? &value->value_s32.value : nullptr;
    switch (index) {
    case 0: state_.SetSpeed(f); break;
    case 1: state_.SetEngineRpm(f); break;
    case 2: state_.SetDisplayedGear(i); break;
    case 3: state_.SetEngineGear(i); break;
    case 4: state_.SetInputSteering(f); break;
    case 5: state_.SetEffectiveSteering(f); break;
    case 6: state_.SetInputThrottle(f); break;
    case 7: state_.SetEffectiveThrottle(f); break;
    case 8: state_.SetInputBrake(f); break;
    case 9: state_.SetEffectiveBrake(f); break;
    case 10: {
        const auto& p = value ? value->value_dplacement : scs_value_dplacement_t{};
        const WorldPlacement converted{p.position.x, p.position.y, p.position.z,
            p.orientation.heading, p.orientation.pitch, p.orientation.roll};
        state_.SetWorldPlacement(value ? &converted : nullptr);
        break;
    }
    case 11: case 12: {
        const auto& v = value ? value->value_fvector : scs_value_fvector_t{};
        const Float3 converted{v.x, v.y, v.z};
        if (index == 11) state_.SetLocalLinearVelocity(value ? &converted : nullptr);
        else state_.SetLocalAngularVelocity(value ? &converted : nullptr);
        break;
    }
    case 13: state_.SetNavigationDistance(f); break;
    case 14: state_.SetNavigationTime(f); break;
    case 15: state_.SetNavigationSpeedLimit(f); break;
    case 16: state_.SetCruiseControl(f); break;
    default: break;
    }
}

SCSAPI_VOID TelemetryPlugin::OnChannel(scs_string_t, scs_u32_t, const scs_value_t* value,
                                        scs_context_t context) {
    const auto* channel = static_cast<const ChannelContext*>(context);
    if (channel && channel->plugin) channel->plugin->HandleChannel(channel->index, value);
}

void TelemetryPlugin::Shutdown() {
    if (!mapping_) { log_ = nullptr; return; }
    state_.Reset();
    try { Publish(); } catch (...) {
        // A failed clock/log cannot leave the writer mutex held.
    }
    mapping_->Close();
    mapping_.reset();
    published_validity_flags_.store(0, std::memory_order_relaxed);
    published_paused_.store(0, std::memory_order_relaxed);
    published_timestamp_ns_.store(0, std::memory_order_release);
    log_ = nullptr;
}

TelemetryPlugin& GlobalTelemetryPlugin() noexcept {
    static TelemetryPlugin plugin;
    return plugin;
}

} // namespace ets2ap::plugin
