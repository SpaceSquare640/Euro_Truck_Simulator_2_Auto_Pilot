// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/plugin/telemetry_plugin.h"

namespace {
ets2ap::plugin::TelemetryPlugin g_plugin;
}

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version,
                                const scs_telemetry_init_params_t* const params) {
    try {
        if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01)
            return SCS_RESULT_unsupported;
        if (!params) return SCS_RESULT_invalid_parameter;
        return g_plugin.Init(static_cast<const scs_telemetry_init_params_v100_t*>(params));
    } catch (...) {
        try { g_plugin.Shutdown(); } catch (...) {}
        return SCS_RESULT_generic_error;
    }
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    try { g_plugin.Shutdown(); } catch (...) {}
}
