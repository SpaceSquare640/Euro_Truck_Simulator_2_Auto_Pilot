// Edited by: OpenAI-Codex_Responsible: Source_Code
#pragma once

#include "ets2ap/plugin/control_host.h"

#include <atomic>
#include <cstdint>
#include <stop_token>
#include <string>
#include <thread>

namespace ets2ap::plugin {

struct ControlPipeServerOptions {
    std::wstring trusted_client_image{};
    std::wstring pipe_name{L"\\\\.\\pipe\\ETS2AP_Control_v1"};
};

class ControlPipeServer {
public:
    ControlPipeServer(ControlHost& host, ControlPipeServerOptions options);
    ~ControlPipeServer();
    ControlPipeServer(const ControlPipeServer&) = delete;
    ControlPipeServer& operator=(const ControlPipeServer&) = delete;

    bool Start() noexcept;
    void Stop() noexcept;
    bool IsRunning() const noexcept;
    bool IsListening() const noexcept;
    std::uint32_t LastError() const noexcept;

private:
    void Run(std::stop_token stop_token) noexcept;
    bool ServeClient(void* pipe, std::stop_token stop_token) noexcept;

    ControlHost& host_;
    ControlPipeServerOptions options_{};
    std::wstring canonical_client_image_{};
    std::jthread worker_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> listening_{false};
    std::atomic<std::uint32_t> last_error_{0};
};

std::wstring TrustedControlClientImageFromInstallRegistry();
ControlPipeServer& GlobalControlPipeServer() noexcept;

} // namespace ets2ap::plugin
