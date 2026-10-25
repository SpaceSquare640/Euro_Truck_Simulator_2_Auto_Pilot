// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/plugin/control_host.h"
#include "ets2ap/plugin/control_pipe_server.h"
#include "ets2ap/test/test_framework.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <Windows.h>

using namespace ets2ap::input;
using namespace ets2ap::plugin;

namespace {
constexpr std::uint64_t kStartNs = 2'000'000'000ULL;
constexpr std::uint32_t kCommandMagic = 0x50413243U;

void PutLittleEndian(std::vector<std::byte>& bytes, std::size_t offset,
                     std::uint64_t value, std::size_t width) {
    for (std::size_t index = 0; index < width; ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8)) & 0xFFU);
    }
}

void PutDouble(std::vector<std::byte>& bytes, std::size_t offset, double value) {
    PutLittleEndian(bytes, offset, std::bit_cast<std::uint64_t>(value), 8);
}

std::vector<std::byte> Packet(std::uint64_t sequence, std::uint64_t timestamp_ns,
                              double steering = 0.0, double throttle = 0.0,
                              double brake = 0.0, bool engage = false,
                              std::uint32_t safety_flags = 0) {
    if (engage && safety_flags == 0) safety_flags = 0x1FU;
    std::vector<std::byte> bytes((engage || safety_flags != 0) ? 68 : 64);
    PutLittleEndian(bytes, 0, kCommandMagic, 4);
    PutLittleEndian(bytes, 4, 1, 2);
    PutLittleEndian(bytes, 6, bytes.size() > 64 ? 1 : 0, 2);
    PutLittleEndian(bytes, 8, bytes.size(), 4);
    PutLittleEndian(bytes, 16, sequence, 8);
    PutLittleEndian(bytes, 24, timestamp_ns, 8);
    PutDouble(bytes, 32, steering);
    PutDouble(bytes, 40, throttle);
    PutDouble(bytes, 48, brake);
    PutLittleEndian(bytes, 56, engage ? 1 : 0, 1);
    if (bytes.size() > 64) PutLittleEndian(bytes, 64, safety_flags, 4);
    return bytes;
}

SafetyGates SafeGates() {
    return {true, true, true, 1.0, true, true, true, true, false};
}

struct CapturedOutput {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<ControlSnapshot> snapshots;
    std::size_t neutralizations{};
};

ControlHostOptions OptionsFor(std::atomic<std::uint64_t>& now_ns,
                              CapturedOutput& output) {
    ControlHostOptions options{};
    options.now_ns = [&now_ns] {
        return std::optional<std::uint64_t>{now_ns.load(std::memory_order_acquire)};
    };
    options.apply_output = [&output](const ControlSnapshot& snapshot, std::uint64_t) {
        {
            const std::lock_guard lock(output.mutex);
            output.snapshots.push_back(snapshot);
        }
        output.changed.notify_all();
        return true;
    };
    options.force_neutral = [&output] {
        {
            const std::lock_guard lock(output.mutex);
            ++output.neutralizations;
        }
        output.changed.notify_all();
    };
    options.tick_interval = std::chrono::milliseconds{2};
    return options;
}

bool WaitForSnapshot(CapturedOutput& output,
                     const std::function<bool(const ControlSnapshot&)>& predicate) {
    std::unique_lock lock(output.mutex);
    return output.changed.wait_for(lock, std::chrono::seconds{2}, [&] {
        return std::any_of(output.snapshots.begin(), output.snapshots.end(), predicate);
    });
}

std::optional<std::uint64_t> QpcNowNs() {
    LARGE_INTEGER ticks{};
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceCounter(&ticks) || !QueryPerformanceFrequency(&frequency) ||
        ticks.QuadPart < 0 || frequency.QuadPart <= 0) return std::nullopt;
    const auto ticks_u = static_cast<std::uint64_t>(ticks.QuadPart);
    const auto frequency_u = static_cast<std::uint64_t>(frequency.QuadPart);
    const auto whole = ticks_u / frequency_u;
    const auto remainder = ticks_u % frequency_u;
    return whole * 1'000'000'000ULL + (remainder * 1'000'000'000ULL) / frequency_u;
}

std::optional<DWORD> RunPythonPipeSmoke(const std::wstring& python_path,
                                        const std::wstring& pipe_name,
                                        const std::wstring& server_image,
                                        const std::filesystem::path& source_root,
                                        std::string& output) {
    const auto script = source_root / L"python" / L"tests" / L"control_pipe_smoke_client.py";
    const auto working_directory = source_root / L"python";
    std::wstring command = L"\"" + python_path + L"\" -I \"" + script.wstring() +
                           L"\" \"" + pipe_name + L"\" \"" + server_image + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    SECURITY_ATTRIBUTES pipe_security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output_read = nullptr;
    HANDLE output_write = nullptr;
    if (!CreatePipe(&output_read, &output_write, &pipe_security, 0)) return std::nullopt;
    SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0);
    startup.dwFlags |= STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = output_write;
    startup.hStdError = output_write;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, working_directory.c_str(), &startup, &process)) {
        CloseHandle(output_read);
        CloseHandle(output_write);
        return std::nullopt;
    }
    CloseHandle(output_write);
    const DWORD wait = WaitForSingleObject(process.hProcess, 10'000);
    DWORD exit_code = 1;
    if (wait == WAIT_OBJECT_0) {
        (void)GetExitCodeProcess(process.hProcess, &exit_code);
    } else {
        TerminateProcess(process.hProcess, 1);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::array<char, 2048> captured{};
    DWORD read = 0;
    while (ReadFile(output_read, captured.data(), static_cast<DWORD>(captured.size()), &read,
                    nullptr) && read > 0) {
        output.append(captured.data(), read);
    }
    CloseHandle(output_read);
    return wait == WAIT_OBJECT_0 ? std::optional<DWORD>{exit_code} : std::nullopt;
}

} // namespace

ETS2AP_TEST(ControlHostWatchdogRunsWithoutNewCommands) {
    std::atomic<std::uint64_t> now_ns{kStartNs};
    CapturedOutput output;
    ControlHost host(OptionsFor(now_ns, output));
    CHECK(host.Start());
    CHECK(host.IsRunning());
    CHECK(host.Connect());

    now_ns.store(kStartNs + 1, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(1, kStartNs + 1)).outcome,
             CommandOutcome::Accepted);
    now_ns.store(kStartNs + 2, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(2, kStartNs + 2, 0, 0.5, 0, true), SafeGates()).outcome,
             CommandOutcome::Accepted);
    now_ns.store(kStartNs + 20'000'002ULL, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(3, kStartNs + 20'000'002ULL, 0, 0.5, 0, true),
                                SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::Engaged && snapshot.throttle > 0.0;
    }));

    now_ns.store(kStartNs + 200'000'000ULL, std::memory_order_release);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::Timeout && snapshot.steering == 0.0 &&
               snapshot.throttle == 0.0 && snapshot.brake == 0.0;
    }));
    host.Stop();
    CHECK(!host.IsRunning());
    CHECK_EQ(host.AcceptCommand(Packet(4, kStartNs + 200'000'001ULL)).outcome,
             CommandOutcome::ConnectionFatal);
    {
        const std::lock_guard lock(output.mutex);
        CHECK(output.neutralizations > 0);
    }
}

ETS2AP_TEST(ControlHostManualOverridePublishesNeutralImmediately) {
    std::atomic<std::uint64_t> now_ns{kStartNs};
    CapturedOutput output;
    ControlHost host(OptionsFor(now_ns, output));
    CHECK(host.Start());
    CHECK(host.Connect());

    now_ns.store(kStartNs + 1, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(1, kStartNs + 1)).outcome,
             CommandOutcome::Accepted);
    now_ns.store(kStartNs + 2, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(2, kStartNs + 2, 0, 0.5, 0, true), SafeGates()).outcome,
             CommandOutcome::Accepted);
    now_ns.store(kStartNs + 20'000'002ULL, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(3, kStartNs + 20'000'002ULL, 0, 0.5, 0, true),
                                SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::Engaged && snapshot.throttle > 0.0;
    }));

    host.SetManualInput(true);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::ManualOverrideLatched &&
               snapshot.steering == 0.0 && snapshot.throttle == 0.0 && snapshot.brake == 0.0;
    }));
    host.SetManualInput(false);
    now_ns.store(kStartNs + 20'000'003ULL, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(4, kStartNs + 20'000'003ULL, 0, 0, 0,
                                       false, InputMonitorHealthy),
                                SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::Disengaged && snapshot.throttle == 0.0;
    }));

    host.Stop();
}

ETS2AP_TEST(ControlHostStartFailsClosedWhenClockIsUnavailable) {
    std::atomic<std::uint64_t> now_ns{kStartNs};
    CapturedOutput output;
    ControlHostOptions options = OptionsFor(now_ns, output);
    options.now_ns = [] { return std::optional<std::uint64_t>{}; };
    ControlHost host(std::move(options));
    CHECK(!host.Start());
    CHECK(!host.IsRunning());
    const std::lock_guard lock(output.mutex);
    CHECK_EQ(output.neutralizations, std::size_t{1});
}

ETS2AP_TEST(ControlHostRejectsSlowSchedulerAndFailedOutputSink) {
    std::atomic<std::uint64_t> now_ns{kStartNs};
    CapturedOutput slow_output;
    auto slow_options = OptionsFor(now_ns, slow_output);
    slow_options.tick_interval = std::chrono::milliseconds{11};
    ControlHost slow_host(std::move(slow_options));
    CHECK(!slow_host.Start());
    CHECK_EQ(slow_output.neutralizations, std::size_t{1});

    CapturedOutput failed_output;
    auto failed_options = OptionsFor(now_ns, failed_output);
    failed_options.apply_output = [](const ControlSnapshot&, std::uint64_t) { return false; };
    ControlHost failed_host(std::move(failed_options));
    CHECK(!failed_host.Start());
    CHECK(!failed_host.IsRunning());
    CHECK_EQ(failed_output.neutralizations, std::size_t{1});
}

ETS2AP_TEST(ControlPipeServerAuthenticatesPeerAndStaysNeutralWithoutSafetyGates) {
    std::atomic<std::uint64_t> now_ns{kStartNs};
    CapturedOutput output;
    auto host_options = OptionsFor(now_ns, output);
    host_options.now_ns = QpcNowNs;
    ControlHost host(std::move(host_options));
    CHECK(host.Start());
    CHECK(host.IsRunning());

    std::array<wchar_t, 32768> image_buffer{};
    const DWORD image_length = GetModuleFileNameW(nullptr, image_buffer.data(),
                                                  static_cast<DWORD>(image_buffer.size()));
    CHECK(image_length > 0 && image_length < image_buffer.size());
    if (image_length == 0 || image_length >= image_buffer.size()) {
        host.Stop();
        return;
    }
    std::array<wchar_t, 32768> python_buffer{};
    const DWORD python_length = GetEnvironmentVariableW(L"ETS2AP_TEST_PYTHON",
        python_buffer.data(), static_cast<DWORD>(python_buffer.size()));
    if (python_length == 0 || python_length >= python_buffer.size()) {
        ControlPipeServer unconfigured_server(host, ControlPipeServerOptions{});
        CHECK(!unconfigured_server.Start());
        host.Stop();
        return;
    }

    const std::filesystem::path executable_path(image_buffer.data());
    auto source_root = executable_path.parent_path();
    for (int parent = 0; parent < 4; ++parent) source_root = source_root.parent_path();
    const std::wstring pipe_name = L"\\\\.\\pipe\\ETS2AP_Control_Test_" +
                                   std::to_wstring(GetCurrentProcessId());
    ControlPipeServer server(host, ControlPipeServerOptions{
        std::wstring(python_buffer.data(), python_length), pipe_name});
    CHECK(server.Start());
    if (!server.IsRunning()) {
        host.Stop();
        return;
    }
    for (int attempt = 0; attempt < 200 && !server.IsListening(); ++attempt) Sleep(10);
    CHECK(server.IsListening());
    const bool pipe_visible_to_test = WaitNamedPipeW(pipe_name.c_str(), 1000) != FALSE;
    CHECK(pipe_visible_to_test);
    if (!pipe_visible_to_test) {
        const DWORD wait_error = GetLastError();
        HANDLE probe = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
        const DWORD open_error = probe == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
        if (probe != INVALID_HANDLE_VALUE) CloseHandle(probe);
        std::cout << "WaitNamedPipe Win32 error: " << wait_error
                  << "; CreateFile Win32 error: " << open_error
                  << "; server listening: " << server.IsListening()
                  << "; server Win32 error: " << server.LastError() << '\n';
        server.Stop();
        host.Stop();
        return;
    }

    std::string client_output;
    const auto client_exit = RunPythonPipeSmoke(
        std::wstring(python_buffer.data(), python_length), pipe_name,
        std::wstring(image_buffer.data(), image_length), source_root, client_output);
    CHECK(client_exit.has_value());
    if (client_exit && *client_exit != 0) {
        std::wcout << L"Control.v1 pipe name: " << pipe_name << L'\n';
        std::cout << "Control.v1 smoke client: " << client_output
                  << "ControlPipeServer Win32 error: " << server.LastError() << '\n';
    }
    if (client_exit) CHECK_EQ(*client_exit, 0u);
    server.Stop();
    host.Stop();
}
