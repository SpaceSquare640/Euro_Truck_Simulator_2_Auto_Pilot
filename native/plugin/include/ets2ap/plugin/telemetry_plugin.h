// Edited by: OpenAI-Codex_Responsible: Source_Code
#pragma once

#include "ets2ap/telemetry/shared_mapping.h"
#include "ets2ap/telemetry/state.h"

#include <scssdk_telemetry.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace ets2ap::plugin {

struct PluginOptions {
    telemetry::MappingNames mapping_names{};
    std::function<std::optional<std::uint64_t>()> now_ns{};
};

struct TelemetryControlSnapshot {
    bool fresh{};
    bool paused{};
    bool speed_limit_available{};
};

class TelemetryPlugin {
public:
    scs_result_t Init(const scs_telemetry_init_params_v100_t* params, PluginOptions options = {});
    void Shutdown();
    TelemetryControlSnapshot ControlSnapshot(std::uint64_t now_ns) const noexcept;

private:
    struct ChannelContext {
        TelemetryPlugin* plugin{};
        std::size_t index{};
    };
    static SCSAPI_VOID OnEvent(scs_event_t event, const void* info, scs_context_t context);
    static SCSAPI_VOID OnChannel(scs_string_t name, scs_u32_t index,
                                 const scs_value_t* value, scs_context_t context);
    void HandleEvent(scs_event_t event);
    void HandleChannel(std::size_t index, const scs_value_t* value) noexcept;
    void Publish();
    void Log(scs_log_type_t type, const char* message) const;

    telemetry::TelemetryState state_{};
    std::unique_ptr<telemetry::TelemetryMapping> mapping_{};
    PluginOptions options_{};
    scs_log_t log_{};
    std::array<ChannelContext, 17> channel_contexts_{};
    std::atomic<std::uint64_t> published_timestamp_ns_{0};
    std::atomic<std::uint32_t> published_validity_flags_{0};
    std::atomic<std::uint8_t> published_paused_{0};
};

TelemetryPlugin& GlobalTelemetryPlugin() noexcept;

} // namespace ets2ap::plugin
