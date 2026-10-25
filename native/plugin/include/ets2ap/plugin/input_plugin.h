// Edited by: OpenAI-Codex_Responsible: Source_Code
#pragma once

#include <scssdk_input.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

namespace ets2ap::plugin {

struct InputCommand {
    float steering{};
    float throttle{};
    float brake{};
    bool engaged{};
    std::uint64_t timestamp_ns{};
};

struct InputPluginOptions {
    std::function<std::optional<std::uint64_t>()> now_ns{};
};

class InputPlugin {
public:
    scs_result_t Init(scs_u32_t version, const scs_input_init_params_t* params,
                      InputPluginOptions options = {});
    void Shutdown() noexcept;
    bool SetCommand(const InputCommand& command) noexcept;
    std::optional<std::uint64_t> NowNs() const noexcept;
    void Neutralize() noexcept;

private:
    static SCSAPI_RESULT OnInputEvent(scs_input_event_t* event_info, scs_u32_t flags,
                                      scs_context_t context) noexcept;
    void NeutralizeLocked() noexcept;
    std::uint64_t ReadPackedCommand() const noexcept;
    static std::uint64_t Pack(const InputCommand& command) noexcept;
    static InputCommand Unpack(std::uint64_t packed) noexcept;

    scs_input_device_t device_{};
    scs_log_t log_{};
    InputPluginOptions options_{};
    mutable std::mutex writer_mutex_{};
    std::atomic<std::uint64_t> revision_{0};
    std::atomic<std::uint64_t> packed_command_{0};
    std::atomic<std::uint64_t> command_timestamp_ns_{0};
    std::atomic<bool> forced_neutral_{true};
    std::atomic<bool> initialized_{false};
    scs_u32_t next_input_index_{};
    std::uint64_t frame_command_{};
    bool frame_neutral_latched_{};
    std::uint64_t last_command_timestamp_ns_{};
};

InputPlugin& GlobalInputPlugin() noexcept;

} // namespace ets2ap::plugin
