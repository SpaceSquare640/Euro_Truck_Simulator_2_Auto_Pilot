// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/plugin/telemetry_plugin.h"
#include "ets2ap/plugin/input_plugin.h"
#include "ets2ap/plugin/control_host.h"
#include "ets2ap/plugin/control_pipe_server.h"
#include "ets2ap/plugin/gameframe_capture.h"

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version,
                                const scs_telemetry_init_params_t* const params) {
    try {
        if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01)
            return SCS_RESULT_unsupported;
        if (!params) return SCS_RESULT_invalid_parameter;
        const auto result = ets2ap::plugin::GlobalTelemetryPlugin().Init(
            static_cast<const scs_telemetry_init_params_v100_t*>(params));
        if (result == SCS_RESULT_ok) (void)ets2ap::plugin::StartGameFrameCapture();
        return result;
    } catch (...) {
        ets2ap::plugin::StopGameFrameCapture();
        try { ets2ap::plugin::GlobalTelemetryPlugin().Shutdown(); } catch (...) {}
        return SCS_RESULT_generic_error;
    }
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    ets2ap::plugin::StopGameFrameCapture();
    try { ets2ap::plugin::GlobalTelemetryPlugin().Shutdown(); } catch (...) {}
}

SCSAPI_RESULT scs_input_init(const scs_u32_t version, const scs_input_init_params_t* const params) {
    try {
        auto& input_plugin = ets2ap::plugin::GlobalInputPlugin();
        const auto result = input_plugin.Init(version, params);
        if (result != SCS_RESULT_ok) return result;
        if (!ets2ap::plugin::GlobalControlHost().Start()) {
            input_plugin.Shutdown();
            return SCS_RESULT_generic_error;
        }
        (void)ets2ap::plugin::GlobalControlPipeServer().Start();
        return SCS_RESULT_ok;
    } catch (...) {
        ets2ap::plugin::GlobalControlPipeServer().Stop();
        ets2ap::plugin::GlobalControlHost().Stop();
        ets2ap::plugin::GlobalInputPlugin().Shutdown();
        return SCS_RESULT_generic_error;
    }
}

SCSAPI_VOID scs_input_shutdown(void) {
    ets2ap::plugin::GlobalControlPipeServer().Stop();
    ets2ap::plugin::GlobalControlHost().Stop();
    ets2ap::plugin::GlobalInputPlugin().Shutdown();
}
