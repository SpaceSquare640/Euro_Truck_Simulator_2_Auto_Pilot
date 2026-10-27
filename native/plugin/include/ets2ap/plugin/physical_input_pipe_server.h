// Edited by: OpenAI-Codex_Responsible: Source_Code
#pragma once

#include "ets2ap/plugin/physical_input_monitor.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace ets2ap::plugin {

struct PhysicalInputPipeServerOptions {
    std::wstring trusted_client_image{};
    std::wstring pipe_name{L"\\\\.\\pipe\\ETS2AP_PhysicalInput_v1"};
};

class PhysicalInputPipeServer {
public:
    PhysicalInputPipeServer(PhysicalInputPipeServerOptions options,
                            KeyboardInputReportState& state);
    ~PhysicalInputPipeServer();
    PhysicalInputPipeServer(const PhysicalInputPipeServer&) = delete;
    PhysicalInputPipeServer& operator=(const PhysicalInputPipeServer&) = delete;

    bool Start() noexcept;
    void Stop() noexcept;
    bool IsRunning() const noexcept;
    bool IsListening() const noexcept;
    std::uint32_t LastError() const noexcept;

private:
    void Run(std::stop_token stop_token) noexcept;
    bool ServeClient(void* pipe, std::stop_token stop_token) noexcept;

    PhysicalInputPipeServerOptions options_{};
    KeyboardInputReportState& state_;
    std::mutex lifecycle_mutex_{};
    std::wstring canonical_client_image_{};
    std::jthread worker_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> listening_{false};
    std::atomic<std::uint32_t> last_error_{0};
};

std::wstring TrustedPhysicalInputClientImageFromInstallRegistry();
PhysicalInputPipeServer& GlobalPhysicalInputPipeServer() noexcept;

} // namespace ets2ap::plugin
