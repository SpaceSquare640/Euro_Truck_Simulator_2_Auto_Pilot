// Edited by: OpenAI-Codex (OpenAI-Codex_Responsible: Source_Code)
#include "ets2ap/plugin/control_pipe_server.h"
#include "ets2ap/plugin/input_plugin.h"
#include "ets2ap/plugin/physical_input_pipe_server.h"
#include "ets2ap/plugin/telemetry_plugin.h"

#include <Windows.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace ets2ap::plugin {
namespace {

constexpr wchar_t kInstallRegistryKey[] = L"SOFTWARE\\Kingsley\\ETS2 Auto Pilot";
constexpr wchar_t kInstallRegistryValue[] = L"InstallLocation";
constexpr DWORD kPipeBufferSize = 256;
constexpr DWORD kFirstReadMs = 50;
constexpr DWORD kFrameDeadlineMs = 100;
constexpr DWORD kStatusIntervalMs = 50;
constexpr DWORD kStatusWriteDeadlineMs = 20;
constexpr DWORD kPhysicalInputFrameDeadlineMs = 300;
constexpr DWORD kCanonicalVolumeNameNt = 0x00000002U;
constexpr DWORD kProcessQueryLimitedInformation = 0x1000;
constexpr DWORD kTokenQuery = 0x0008;
constexpr TOKEN_INFORMATION_CLASS kTokenUserClass = TokenUser;
constexpr DWORD kErrorIoPending = ERROR_IO_PENDING;
constexpr DWORD kErrorPipeConnected = ERROR_PIPE_CONNECTED;
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);

enum class IoResult { Complete, Timeout, Failed };

std::uint32_t ReadU32(const std::byte* data) noexcept {
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[0])) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[1])) << 8) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[2])) << 16) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[3])) << 24);
}

std::wstring CanonicalImagePath(const std::wstring& path) {
    if (path.empty()) return {};
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetFinalPathNameByHandleW(file, buffer.data(),
        static_cast<DWORD>(buffer.size()), kCanonicalVolumeNameNt);
    CloseHandle(file);
    if (length == 0 || length >= buffer.size()) return {};
    std::wstring result(buffer.data(), length);
    std::transform(result.begin(), result.end(), result.begin(), ::towupper);
    return result;
}

bool CurrentUserToken(HANDLE process, HANDLE& token) noexcept {
    return OpenProcessToken(process, kTokenQuery, &token) != FALSE;
}

bool ReadTokenUser(HANDLE token, std::vector<std::byte>& storage,
                   TOKEN_USER*& token_user) noexcept {
    DWORD needed = 0;
    GetTokenInformation(token, kTokenUserClass, nullptr, 0, &needed);
    if (needed < sizeof(TOKEN_USER)) return false;
    try {
        storage.resize(needed);
    } catch (...) {
        return false;
    }
    if (!GetTokenInformation(token, kTokenUserClass, storage.data(), needed, &needed)) return false;
    token_user = reinterpret_cast<TOKEN_USER*>(storage.data());
    return true;
}

std::wstring CurrentUserSidString() {
    HANDLE token = nullptr;
    if (!CurrentUserToken(GetCurrentProcess(), token)) return {};
    std::vector<std::byte> storage;
    TOKEN_USER* user = nullptr;
    if (!ReadTokenUser(token, storage, user)) {
        CloseHandle(token);
        return {};
    }
    LPWSTR text = nullptr;
    const bool converted = ConvertSidToStringSidW(user->User.Sid, &text) != FALSE;
    CloseHandle(token);
    if (!converted || !text) return {};
    std::wstring result(text);
    LocalFree(text);
    return result;
}

bool VerifyClient(HANDLE pipe, const std::wstring& trusted_image,
                  DWORD* failure_error = nullptr) noexcept {
    if (failure_error) *failure_error = ERROR_ACCESS_DENIED;
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(pipe, &pid) || pid == 0) {
        if (failure_error) *failure_error = GetLastError();
        return false;
    }
    if (pid == GetCurrentProcessId()) {
        std::array<wchar_t, 32768> path{};
        const DWORD path_length = GetModuleFileNameW(nullptr, path.data(),
            static_cast<DWORD>(path.size()));
        if (path_length == 0 || path_length >= path.size()) {
            if (failure_error) *failure_error = GetLastError();
            return false;
        }
        const std::wstring client_image = CanonicalImagePath(
            std::wstring(path.data(), path_length));
        const bool matches = !client_image.empty() && client_image == trusted_image;
        if (failure_error) *failure_error = matches ? ERROR_SUCCESS :
            (client_image.empty() ? ERROR_ACCESS_DENIED : ERROR_BAD_EXE_FORMAT);
        return matches;
    }

    HANDLE process = OpenProcess(kProcessQueryLimitedInformation, FALSE, pid);
    if (!process) {
        if (failure_error) *failure_error = GetLastError();
        return false;
    }

    bool valid = false;
    HANDLE process_token = nullptr;
    HANDLE current_token = nullptr;
    try {
        std::array<wchar_t, 32768> path{};
        DWORD path_length = static_cast<DWORD>(path.size());
        if (!QueryFullProcessImageNameW(process, 0, path.data(), &path_length)) {
            if (failure_error) *failure_error = GetLastError();
            CloseHandle(process);
            return false;
        }
        const std::wstring client_image = CanonicalImagePath(
            std::wstring(path.data(), path_length));
        if (client_image.empty() || client_image != trusted_image) {
            if (failure_error) *failure_error = client_image.empty()
                ? ERROR_ACCESS_DENIED : ERROR_BAD_EXE_FORMAT;
            CloseHandle(process);
            return false;
        }
        if (!CurrentUserToken(process, process_token) ||
            !CurrentUserToken(GetCurrentProcess(), current_token)) {
            if (failure_error) *failure_error = GetLastError();
            CloseHandle(process);
            if (process_token) CloseHandle(process_token);
            if (current_token) CloseHandle(current_token);
            return false;
        }
        std::vector<std::byte> process_storage;
        std::vector<std::byte> current_storage;
        TOKEN_USER* process_user = nullptr;
        TOKEN_USER* current_user = nullptr;
        valid = ReadTokenUser(process_token, process_storage, process_user) &&
                ReadTokenUser(current_token, current_storage, current_user) &&
                EqualSid(process_user->User.Sid, current_user->User.Sid) != FALSE;
        if (valid && failure_error) *failure_error = ERROR_SUCCESS;
    } catch (...) {
        valid = false;
    }
    if (process_token) CloseHandle(process_token);
    if (current_token) CloseHandle(current_token);
    CloseHandle(process);
    return valid;
}

IoResult WaitOperation(HANDLE pipe, OVERLAPPED& operation, DWORD timeout_ms,
                       DWORD& transferred, std::stop_token stop_token) noexcept {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (!stop_token.stop_requested()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const DWORD wait_ms = static_cast<DWORD>(std::max<std::int64_t>(
            1, std::min<std::int64_t>(25, remaining.count())));
        const DWORD result = WaitForSingleObject(operation.hEvent, wait_ms);
        if (result == WAIT_OBJECT_0) {
            if (GetOverlappedResult(pipe, &operation, &transferred, FALSE)) return IoResult::Complete;
            return IoResult::Failed;
        }
        if (result != WAIT_TIMEOUT) return IoResult::Failed;
    }
    CancelIoEx(pipe, &operation);
    DWORD ignored = 0;
    GetOverlappedResult(pipe, &operation, &ignored, TRUE);
    return stop_token.stop_requested() ? IoResult::Failed : IoResult::Timeout;
}

IoResult ReadSome(HANDLE pipe, std::span<std::byte> buffer, DWORD timeout_ms,
                  DWORD& transferred, std::stop_token stop_token) noexcept {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) return IoResult::Failed;
    OVERLAPPED operation{};
    operation.hEvent = event;
    const BOOL immediate = ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                                    &transferred, &operation);
    IoResult result = IoResult::Failed;
    if (immediate) {
        result = IoResult::Complete;
    } else if (GetLastError() == kErrorIoPending) {
        result = WaitOperation(pipe, operation, timeout_ms, transferred, stop_token);
    }
    CloseHandle(event);
    return result;
}

IoResult WriteAll(HANDLE pipe, std::span<const std::byte> data,
                  DWORD timeout_ms, std::stop_token stop_token) noexcept {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    std::size_t offset = 0;
    while (offset < data.size() && !stop_token.stop_requested()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return IoResult::Timeout;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const DWORD wait_ms = static_cast<DWORD>(std::max<std::int64_t>(1, remaining.count()));
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event) return IoResult::Failed;
        OVERLAPPED operation{};
        operation.hEvent = event;
        DWORD transferred = 0;
        const BOOL immediate = WriteFile(pipe, data.data() + offset,
            static_cast<DWORD>(data.size() - offset), &transferred, &operation);
        IoResult result = IoResult::Failed;
        if (immediate) {
            result = IoResult::Complete;
        } else if (GetLastError() == kErrorIoPending) {
            result = WaitOperation(pipe, operation, wait_ms, transferred, stop_token);
        }
        CloseHandle(event);
        if (result != IoResult::Complete || transferred == 0) return IoResult::Failed;
        offset += transferred;
    }
    return offset == data.size() ? IoResult::Complete : IoResult::Failed;
}

IoResult ReadExact(HANDLE pipe, std::span<std::byte> destination,
                   std::chrono::steady_clock::time_point deadline,
                   std::stop_token stop_token) noexcept {
    std::size_t offset = 0;
    while (offset < destination.size() && !stop_token.stop_requested()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return IoResult::Timeout;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        DWORD count = 0;
        const auto result = ReadSome(pipe, destination.subspan(offset),
            static_cast<DWORD>(std::max<std::int64_t>(1, remaining.count())), count, stop_token);
        if (result != IoResult::Complete) return result;
        if (count == 0) return IoResult::Failed;
        offset += count;
    }
    return offset == destination.size() ? IoResult::Complete : IoResult::Failed;
}

bool ConnectClient(HANDLE pipe, std::stop_token stop_token,
                   std::atomic<std::uint32_t>& last_error) noexcept {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) {
        last_error.store(GetLastError(), std::memory_order_release);
        return false;
    }
    OVERLAPPED operation{};
    operation.hEvent = event;
    const BOOL connected = ConnectNamedPipe(pipe, &operation);
    if (connected) {
        last_error.store(ERROR_SUCCESS, std::memory_order_release);
        CloseHandle(event);
        return true;
    }
    const DWORD error = GetLastError();
    if (error == kErrorPipeConnected) {
        last_error.store(ERROR_SUCCESS, std::memory_order_release);
        CloseHandle(event);
        return true;
    }
    if (error != kErrorIoPending) {
        last_error.store(error, std::memory_order_release);
        CloseHandle(event);
        return false;
    }
    DWORD ignored = 0;
    const auto result = WaitOperation(pipe, operation, INFINITE, ignored, stop_token);
    if (result != IoResult::Complete)
        last_error.store(GetLastError(), std::memory_order_release);
    CloseHandle(event);
    return result == IoResult::Complete;
}

bool SameSnapshot(const input::ControlSnapshot& left,
                  const input::ControlSnapshot& right) noexcept {
    return left.last_accepted_sequence == right.last_accepted_sequence &&
           left.state == right.state && left.reason == right.reason &&
           left.steering == right.steering && left.throttle == right.throttle &&
           left.brake == right.brake;
}

} // namespace

ControlPipeServer::ControlPipeServer(ControlHost& host, ControlPipeServerOptions options)
    : host_(host), options_(std::move(options)) {}

ControlPipeServer::~ControlPipeServer() {
    Stop();
}

bool ControlPipeServer::Start() noexcept {
    try {
        if (running_.load(std::memory_order_acquire)) return true;
        last_error_.store(ERROR_SUCCESS, std::memory_order_release);
        canonical_client_image_ = CanonicalImagePath(options_.trusted_client_image);
        if (canonical_client_image_.empty() || !host_.IsRunning()) return false;
        running_.store(true, std::memory_order_release);
        worker_ = std::jthread([this](std::stop_token token) { Run(token); });
        return true;
    } catch (...) {
        running_.store(false, std::memory_order_release);
        return false;
    }
}

void ControlPipeServer::Stop() noexcept {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    running_.store(false, std::memory_order_release);
    listening_.store(false, std::memory_order_release);
    host_.Disconnect();
}

bool ControlPipeServer::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

bool ControlPipeServer::IsListening() const noexcept {
    return listening_.load(std::memory_order_acquire);
}

std::uint32_t ControlPipeServer::LastError() const noexcept {
    return last_error_.load(std::memory_order_acquire);
}

void ControlPipeServer::Run(std::stop_token stop_token) noexcept {
    const std::wstring sid = CurrentUserSidString();
    if (sid.empty()) {
        last_error_.store(ERROR_ACCESS_DENIED, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        host_.Disconnect();
        return;
    }
    const std::wstring sddl = L"D:P(A;;GA;;;" + sid + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                               &descriptor, nullptr)) {
        last_error_.store(GetLastError(), std::memory_order_release);
        running_.store(false, std::memory_order_release);
        host_.Disconnect();
        return;
    }
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};

    while (!stop_token.stop_requested()) {
        HANDLE pipe = CreateNamedPipeW(options_.pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, kPipeBufferSize, kPipeBufferSize, 0, &security);
        if (pipe == INVALID_HANDLE_VALUE) {
            last_error_.store(GetLastError(), std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        listening_.store(true, std::memory_order_release);
        if (ConnectClient(pipe, stop_token, last_error_) &&
            VerifyClient(pipe, canonical_client_image_)) {
            if (host_.Connect()) (void)ServeClient(pipe, stop_token);
        }
        host_.Disconnect();
        listening_.store(false, std::memory_order_release);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    LocalFree(descriptor);
    running_.store(false, std::memory_order_release);
    host_.Disconnect();
}

bool ControlPipeServer::ServeClient(void* raw_pipe, std::stop_token stop_token) noexcept {
    HANDLE pipe = static_cast<HANDLE>(raw_pipe);
    std::atomic<bool> status_writer_failed{false};
    std::jthread status_writer;
    try {
        status_writer = std::jthread([&](std::stop_token writer_token) {
            auto previous = host_.Snapshot();
            auto next_periodic_status = std::chrono::steady_clock::now();
            bool has_sent_status = false;
            while (!writer_token.stop_requested() && !stop_token.stop_requested()) {
                const auto now = std::chrono::steady_clock::now();
                const auto current = host_.Snapshot();
                const bool changed = !has_sent_status || !SameSnapshot(previous, current);
                if (changed || now >= next_periodic_status) {
                    const auto status = host_.EncodeStatus();
                    if (!status || WriteAll(pipe, *status, kStatusWriteDeadlineMs,
                                            writer_token) != IoResult::Complete) {
                        status_writer_failed.store(true, std::memory_order_release);
                        return;
                    }
                    previous = current;
                    has_sent_status = true;
                    next_periodic_status = std::chrono::steady_clock::now() +
                                           std::chrono::milliseconds(kStatusIntervalMs);
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    } catch (...) {
        return false;
    }

    while (!stop_token.stop_requested() &&
           !status_writer_failed.load(std::memory_order_acquire)) {

        std::array<std::byte, 16> header{};
        DWORD received = 0;
        const auto first_read = ReadSome(pipe, header, kFirstReadMs, received, stop_token);
        if (first_read == IoResult::Timeout && received == 0) continue;
        if (first_read != IoResult::Complete || received == 0) return false;
        const auto frame_deadline = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(kFrameDeadlineMs);
        if (received < header.size() &&
            ReadExact(pipe, std::span<std::byte>(header).subspan(received), frame_deadline,
                      stop_token) != IoResult::Complete) return false;

        const std::uint32_t packet_size = ReadU32(header.data() + 8);
        if (packet_size < 64 || packet_size > kPipeBufferSize) return false;
        std::vector<std::byte> packet(packet_size);
        std::copy(header.begin(), header.end(), packet.begin());
        if (packet_size > header.size() &&
            ReadExact(pipe, std::span<std::byte>(packet).subspan(header.size()),
                      frame_deadline, stop_token) != IoResult::Complete) return false;

        input::SafetyGates gates{};
        try {
            if (options_.safety_gates) gates = options_.safety_gates();
        } catch (...) {
            gates = {};
        }
        const auto result = host_.AcceptCommand(packet, gates);
        if (result.close_connection) break;
    }
    status_writer.request_stop();
    if (status_writer.joinable()) status_writer.join();
    return !status_writer_failed.load(std::memory_order_acquire) &&
           !stop_token.stop_requested();
}

std::wstring TrustedControlClientImageFromInstallRegistry() {
    DWORD byte_count = 0;
    const auto flags = RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kInstallRegistryKey, kInstallRegistryValue,
                     flags, nullptr, nullptr, &byte_count) != ERROR_SUCCESS ||
        byte_count < sizeof(wchar_t)) return {};
    std::vector<wchar_t> value(byte_count / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kInstallRegistryKey, kInstallRegistryValue,
                     flags, nullptr, value.data(), &byte_count) != ERROR_SUCCESS) return {};
    std::filesystem::path path(value.data());
    path /= L"runtime";
    path /= L"python.exe";
    return path.wstring();
}

std::wstring TrustedPhysicalInputClientImageFromInstallRegistry() {
    DWORD byte_count = 0;
    const auto flags = RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kInstallRegistryKey, kInstallRegistryValue,
                     flags, nullptr, nullptr, &byte_count) != ERROR_SUCCESS ||
        byte_count < sizeof(wchar_t)) return {};
    std::vector<wchar_t> value(byte_count / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kInstallRegistryKey, kInstallRegistryValue,
                     flags, nullptr, value.data(), &byte_count) != ERROR_SUCCESS) return {};
    std::filesystem::path path(value.data());
    path /= L"ETS2AP.Ui.exe";
    return path.wstring();
}

ControlPipeServer& GlobalControlPipeServer() noexcept {
    static ControlPipeServer server{GlobalControlHost(), [] {
        ControlPipeServerOptions options{};
        options.trusted_client_image = TrustedControlClientImageFromInstallRegistry();
        options.safety_gates = [] {
            input::SafetyGates gates{};
            const auto now = GlobalInputPlugin().NowNs();
            if (!now) return gates;
            const auto telemetry = GlobalTelemetryPlugin().ControlSnapshot(*now);
            gates.telemetry_fresh = telemetry.fresh;
            gates.speed_limit_available = telemetry.speed_limit_available;
            const auto input_status = GlobalControlHost().GetPhysicalInputStatus();
            gates.input_monitor_healthy = input_status.healthy;
            gates.manual_input_active = input_status.manual_input_active;
            // The WPF authorization path is not connected to native/input yet.
            gates.explicit_ui_authorized = false;
            gates.maximum_steering_ratio = 0.30;
            return gates;
        };
        return options;
    }()};
    return server;
}

PhysicalInputPipeServer::PhysicalInputPipeServer(
    PhysicalInputPipeServerOptions options, KeyboardInputReportState& state)
    : options_(std::move(options)), state_(state) {}

PhysicalInputPipeServer::~PhysicalInputPipeServer() {
    Stop();
}

bool PhysicalInputPipeServer::Start() noexcept {
    std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_, std::defer_lock);
    try {
        lifecycle_lock.lock();
        if (running_.load(std::memory_order_acquire)) return true;
        last_error_.store(ERROR_SUCCESS, std::memory_order_release);
        state_.Disconnect();
        canonical_client_image_ = CanonicalImagePath(options_.trusted_client_image);
        if (canonical_client_image_.empty()) {
            last_error_.store(GetLastError(), std::memory_order_release);
            return false;
        }
        running_.store(true, std::memory_order_release);
        worker_ = std::jthread([this](std::stop_token token) { Run(token); });
        return true;
    } catch (...) {
        if (!lifecycle_lock.owns_lock()) {
            try { lifecycle_lock.lock(); }
            catch (...) { return false; }
        }
        last_error_.store(ERROR_NOT_ENOUGH_MEMORY, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        state_.Disconnect();
        return false;
    }
}

void PhysicalInputPipeServer::Stop() noexcept {
    std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_, std::defer_lock);
    try {
        lifecycle_lock.lock();
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
        running_.store(false, std::memory_order_release);
        listening_.store(false, std::memory_order_release);
        state_.Disconnect();
    } catch (...) {
        if (!lifecycle_lock.owns_lock()) {
            try { lifecycle_lock.lock(); }
            catch (...) { return; }
        }
        try {
            if (worker_.joinable()) {
                worker_.request_stop();
                worker_.join();
            }
        } catch (...) {
        }
        running_.store(false, std::memory_order_release);
        listening_.store(false, std::memory_order_release);
        state_.Disconnect();
    }
}

bool PhysicalInputPipeServer::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

bool PhysicalInputPipeServer::IsListening() const noexcept {
    return listening_.load(std::memory_order_acquire);
}

std::uint32_t PhysicalInputPipeServer::LastError() const noexcept {
    return last_error_.load(std::memory_order_acquire);
}

void PhysicalInputPipeServer::Run(std::stop_token stop_token) noexcept {
    const std::wstring sid = CurrentUserSidString();
    if (sid.empty()) {
        last_error_.store(ERROR_ACCESS_DENIED, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        state_.Disconnect();
        return;
    }
    const std::wstring sddl = L"D:P(A;;GA;;;" + sid + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                               &descriptor, nullptr)) {
        last_error_.store(GetLastError(), std::memory_order_release);
        running_.store(false, std::memory_order_release);
        state_.Disconnect();
        return;
    }
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};

    while (!stop_token.stop_requested()) {
        HANDLE pipe = CreateNamedPipeW(options_.pipe_name.c_str(),
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 0, static_cast<DWORD>(kKeyboardInputFrameSize), 0, &security);
        if (pipe == INVALID_HANDLE_VALUE) {
            last_error_.store(GetLastError(), std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        listening_.store(true, std::memory_order_release);
        if (ConnectClient(pipe, stop_token, last_error_)) {
            DWORD verification_error = ERROR_ACCESS_DENIED;
            if (VerifyClient(pipe, canonical_client_image_, &verification_error)) {
                state_.BeginConnection();
                (void)ServeClient(pipe, stop_token);
            } else {
                last_error_.store(verification_error, std::memory_order_release);
            }
        }
        state_.Disconnect();
        listening_.store(false, std::memory_order_release);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    LocalFree(descriptor);
    running_.store(false, std::memory_order_release);
    state_.Disconnect();
}

bool PhysicalInputPipeServer::ServeClient(void* raw_pipe,
                                          std::stop_token stop_token) noexcept {
    HANDLE pipe = static_cast<HANDLE>(raw_pipe);
    std::uint64_t previous_sequence = 0;
    while (!stop_token.stop_requested()) {
        std::array<std::byte, kKeyboardInputFrameSize> frame{};
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds{kPhysicalInputFrameDeadlineMs};
        if (ReadExact(pipe, frame, deadline, stop_token) != IoResult::Complete) return false;
        KeyboardInputReport report{};
        if (!DecodeKeyboardInputFrame(frame, previous_sequence, report)) return false;
        const auto received_at = std::chrono::steady_clock::now();
        if (!state_.Update(report, received_at)) return false;
        previous_sequence = report.sequence;
    }
    return false;
}

PhysicalInputPipeServer& GlobalPhysicalInputPipeServer() noexcept {
    static PhysicalInputPipeServer server{[] {
        PhysicalInputPipeServerOptions options{};
        options.trusted_client_image = TrustedPhysicalInputClientImageFromInstallRegistry();
        return options;
    }(), GlobalKeyboardInputReportState()};
    return server;
}

} // namespace ets2ap::plugin
