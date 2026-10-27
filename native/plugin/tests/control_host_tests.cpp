// Edited by: OpenAI-Codex (OpenAI-Codex_Responsible: Source_Code)
#include "ets2ap/plugin/control_host.h"
#include "ets2ap/plugin/control_pipe_server.h"
#include "ets2ap/plugin/physical_input_pipe_server.h"
#include "ets2ap/plugin/physical_input_monitor.h"
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
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <Windows.h>

using namespace ets2ap::input;
using namespace ets2ap::plugin;

namespace {
constexpr std::uint64_t kStartNs = 2'000'000'000ULL;
constexpr std::uint32_t kCommandMagic = 0x50413243U;

std::optional<bool> IsCurrentProcessAppContainer() noexcept {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return std::nullopt;
    BOOL is_app_container = FALSE;
    DWORD returned = 0;
    const BOOL queried = GetTokenInformation(token, TokenIsAppContainer, &is_app_container,
        sizeof(is_app_container), &returned);
    CloseHandle(token);
    if (!queried || returned < sizeof(is_app_container)) return std::nullopt;
    return is_app_container != FALSE;
}

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
    options.sample_physical_input = [] {
        return PhysicalInputStatus{.sample_valid = true, .healthy = true,
                                   .manual_input_active = false};
    };
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

std::uint16_t NoKeyboardKeyDown(int) noexcept { return 0; }

std::uint16_t OnlyLegacyPressedBit(int) noexcept { return 0x0001U; }

std::uint16_t OneKeyboardKeyDown(int key) noexcept {
    return key == 0x41 ? 0x8000U : 0U;
}

std::array<std::byte, kKeyboardInputFrameSize> KeyboardInputFrame(
    std::uint64_t sequence, std::uint16_t flags) {
    std::array<std::byte, kKeyboardInputFrameSize> bytes{};
    bytes[0] = std::byte{'E'};
    bytes[1] = std::byte{'2'};
    bytes[2] = std::byte{'P'};
    bytes[3] = std::byte{'I'};
    const auto put = [&bytes](std::size_t offset, std::uint64_t value, std::size_t width) {
        for (std::size_t index = 0; index < width; ++index) {
            bytes[offset + index] = static_cast<std::byte>((value >> (index * 8)) & 0xFFU);
        }
    };
    put(4, 1, 2);
    put(6, flags, 2);
    put(8, kKeyboardInputFrameSize, 4);
    put(12, sequence, 8);
    return bytes;
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

ETS2AP_TEST(PhysicalInputMonitorRequiresValidatedCoverageAndRejectsUnknownHid) {
    const PhysicalInputSample verified{
        .keyboard_state_observed = true,
        .xinput_probe_available = true,
        .device_inventory_available = true,
        .keyboard_active = false,
        .xinput_active = false,
        .unclassified_hid_present = false,
        .coverage_validated = true};
    const auto verified_status = EvaluatePhysicalInputSample(verified);
    CHECK(verified_status.sample_valid);
    CHECK(verified_status.healthy);
    CHECK(!verified_status.manual_input_active);

    auto unknown_device = verified;
    unknown_device.unclassified_hid_present = true;
    const auto unknown_status = EvaluatePhysicalInputSample(unknown_device);
    CHECK(unknown_status.sample_valid);
    CHECK(!unknown_status.healthy);

    auto unvalidated_coverage = verified;
    unvalidated_coverage.coverage_validated = false;
    CHECK(!EvaluatePhysicalInputSample(unvalidated_coverage).healthy);
}

ETS2AP_TEST(PhysicalInputMonitorReportsKeyboardAndXInputActivity) {
    PhysicalInputSample sample{
        .keyboard_state_observed = true,
        .xinput_probe_available = true,
        .device_inventory_available = true,
        .keyboard_active = true,
        .xinput_active = false,
        .unclassified_hid_present = false,
        .coverage_validated = false};
    CHECK(EvaluatePhysicalInputSample(sample).manual_input_active);

    sample.keyboard_active = false;
    sample.xinput_active = true;
    CHECK(EvaluatePhysicalInputSample(sample).manual_input_active);
}

ETS2AP_TEST(KeyboardProbeRequiresPositiveKeyDownEvidence) {
    CHECK(!HasObservableKeyboardActivity(&NoKeyboardKeyDown));
    CHECK(!HasObservableKeyboardActivity(&OnlyLegacyPressedBit));
    CHECK(HasObservableKeyboardActivity(&OneKeyboardKeyDown));
    CHECK(!HasObservableKeyboardActivity(nullptr));
}

ETS2AP_TEST(KeyboardInputPipeRejectsInvalidFramesAndExpiresStaleReports) {
    auto frame = KeyboardInputFrame(1, 0x0001U);
    KeyboardInputReport report{};
    CHECK(DecodeKeyboardInputFrame(frame, 0, report));
    CHECK_EQ(report.sequence, 1ULL);
    CHECK(report.raw_input_available);
    CHECK(!report.manual_input_active);
    CHECK(!DecodeKeyboardInputFrame(frame, report.sequence, report));

    auto unknown_flags = KeyboardInputFrame(2, 0x0004U);
    CHECK(!DecodeKeyboardInputFrame(unknown_flags, 1, report));
    auto zero_sequence = KeyboardInputFrame(0, 0x0001U);
    CHECK(!DecodeKeyboardInputFrame(zero_sequence, 0, report));
    frame.back() = std::byte{1};
    CHECK(!DecodeKeyboardInputFrame(frame, 0, report));

    KeyboardInputReportState state;
    const auto start = std::chrono::steady_clock::time_point{};
    state.BeginConnection();
    auto observation = state.Snapshot(start);
    CHECK(!observation.available);
    CHECK(observation.manual_input_active);
    CHECK(state.Update(report = KeyboardInputReport{1, true, false}, start));
    observation = state.Snapshot(start + std::chrono::milliseconds{249});
    CHECK(observation.available);
    CHECK(!observation.manual_input_active);
    observation = state.Snapshot(start + std::chrono::milliseconds{250});
    CHECK(!observation.available);
    CHECK(observation.manual_input_active);
    CHECK(!state.Update(report, start + std::chrono::milliseconds{251}));
    state.Disconnect();
    observation = state.Snapshot(start + std::chrono::milliseconds{252});
    CHECK(!observation.available);
    CHECK(observation.manual_input_active);
}

ETS2AP_TEST(PhysicalInputPipeAuthenticatesLocalImageAndFailsClosedOnUnresolvablePath) {
    std::array<wchar_t, 32768> image{};
    const DWORD image_length = GetModuleFileNameW(nullptr, image.data(),
                                                   static_cast<DWORD>(image.size()));
    CHECK(image_length > 0 && image_length < image.size());
    if (image_length == 0 || image_length >= image.size()) return;
    const std::wstring image_path(image.data(), image_length);
    HANDLE image_handle = CreateFileW(image_path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    std::array<wchar_t, 32768> canonical_image{};
    const DWORD canonical_length = image_handle == INVALID_HANDLE_VALUE ? 0 :
        GetFinalPathNameByHandleW(image_handle, canonical_image.data(),
            static_cast<DWORD>(canonical_image.size()), 0x00000002U);
    const DWORD canonical_error = GetLastError();
    if (image_handle != INVALID_HANDLE_VALUE) CloseHandle(image_handle);
    if (canonical_length == 0 || canonical_length >= canonical_image.size()) {
        const std::wstring unavailable_pipe = L"\\\\.\\pipe\\ETS2AP_PhysicalInput_test_" +
            std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
        KeyboardInputReportState unavailable_state;
        PhysicalInputPipeServer unavailable_server(
            {image_path, unavailable_pipe}, unavailable_state);
        CHECK(!unavailable_server.Start());
        CHECK_EQ(unavailable_server.LastError(), canonical_error);
        const auto unavailable = unavailable_state.Snapshot(std::chrono::steady_clock::now());
        CHECK(!unavailable.available);
        CHECK(unavailable.manual_input_active);
        std::cout << "PhysicalInputPipe peer flow not exercised: test image path "
                  << "cannot be canonicalized (Win32 " << canonical_error << ")\n";
        return;
    }

    const std::wstring pipe_name = L"\\\\.\\pipe\\ETS2AP_PhysicalInput_test_" +
        std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    const auto app_container = IsCurrentProcessAppContainer();
    CHECK(app_container.has_value());
    if (!app_container.has_value()) return;

    KeyboardInputReportState state;
    PhysicalInputPipeServer server({image_path, pipe_name}, state);
    const bool server_started = server.Start();
    if (!server_started) std::cerr << "Physical input server start error: " << server.LastError() << '\n';
    CHECK(server_started);
    if (!server_started) return;

    const auto listen_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!server.IsListening() && std::chrono::steady_clock::now() < listen_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    CHECK(server.IsListening());
    if (!server.IsListening()) {
        server.Stop();
        return;
    }

    if (*app_container) {
        HANDLE denied_client = CreateFileW(pipe_name.c_str(), GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const DWORD connect_error = denied_client == INVALID_HANDLE_VALUE
            ? GetLastError() : ERROR_SUCCESS;
        CHECK_EQ(denied_client, INVALID_HANDLE_VALUE);
        CHECK_EQ(connect_error, static_cast<DWORD>(ERROR_ACCESS_DENIED));
        if (denied_client != INVALID_HANDLE_VALUE) CloseHandle(denied_client);
        const auto unavailable = state.Snapshot(std::chrono::steady_clock::now());
        CHECK(!unavailable.available);
        CHECK(unavailable.manual_input_active);
        std::cout << "PhysicalInputPipe positive peer flow not exercised: current-user ACL "
                  << "rejects AppContainer clients; fail-closed state verified\n";
        server.Stop();
        return;
    }

    HANDLE client = INVALID_HANDLE_VALUE;
    const auto connect_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (client == INVALID_HANDLE_VALUE && std::chrono::steady_clock::now() < connect_deadline) {
        client = CreateFileW(pipe_name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (client == INVALID_HANDLE_VALUE) std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    if (client == INVALID_HANDLE_VALUE) {
        std::cout << "PhysicalInput pipe connect error=" << GetLastError()
                  << "; server running=" << server.IsRunning()
                  << "; listening=" << server.IsListening()
                  << "; server error=" << server.LastError() << '\n';
    }
    CHECK(client != INVALID_HANDLE_VALUE);
    if (client == INVALID_HANDLE_VALUE) {
        server.Stop();
        return;
    }

    auto frame = KeyboardInputFrame(1, 0x0001U);
    DWORD written = 0;
    CHECK(WriteFile(client, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr));
    CHECK_EQ(written, static_cast<DWORD>(frame.size()));

    KeyboardInputObservation observation{};
    const auto report_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    do {
        observation = state.Snapshot(std::chrono::steady_clock::now());
        if (observation.available) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    } while (std::chrono::steady_clock::now() < report_deadline);
    if (!observation.available) {
        std::cout << "PhysicalInput report timeout; server running=" << server.IsRunning()
                  << "; listening=" << server.IsListening()
                  << "; server error=" << server.LastError() << '\n';
    }
    CHECK(observation.available);
    CHECK(!observation.manual_input_active);

    CloseHandle(client);
    const auto disconnect_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    do {
        observation = state.Snapshot(std::chrono::steady_clock::now());
        if (!observation.available) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    } while (std::chrono::steady_clock::now() < disconnect_deadline);
    CHECK(!observation.available);
    CHECK(observation.manual_input_active);
    server.Stop();
}

ETS2AP_TEST(PhysicalInputMonitorFailsClosedWhenAProbeIsUnavailable) {
    const PhysicalInputSample sample{
        .keyboard_state_observed = true,
        .xinput_probe_available = false,
        .device_inventory_available = true,
        .keyboard_active = false,
        .xinput_active = false,
        .unclassified_hid_present = false,
        .coverage_validated = true};
    const auto status = EvaluatePhysicalInputSample(sample);
    CHECK(!status.sample_valid);
    CHECK(!status.healthy);
    CHECK(status.manual_input_active);
}

ETS2AP_TEST(PhysicalInputMonitorFailsClosedWhenKeyboardStateIsUnobservable) {
    const PhysicalInputSample sample{
        .keyboard_state_observed = false,
        .xinput_probe_available = true,
        .device_inventory_available = true,
        .keyboard_active = false,
        .xinput_active = false,
        .unclassified_hid_present = false,
        .coverage_validated = true};
    const auto status = EvaluatePhysicalInputSample(sample);
    CHECK(!status.sample_valid);
    CHECK(!status.healthy);
    CHECK(status.manual_input_active);
}

ETS2AP_TEST(SystemPhysicalInputMonitorStaysFailClosedUntilCoverageIsValidated) {
    const auto sample = ReadSystemPhysicalInputSample();
    CHECK(!sample.coverage_validated);
    CHECK(!EvaluatePhysicalInputSample(sample).healthy);
}

ETS2AP_TEST(ControlHostLocalInputMonitorBlocksEngagement) {
    std::atomic<std::uint64_t> now_ns{kStartNs};
    std::atomic<bool> monitor_healthy{false};
    CapturedOutput output;
    auto options = OptionsFor(now_ns, output);
    options.sample_physical_input = [&monitor_healthy] {
        return PhysicalInputStatus{.sample_valid = true,
                                   .healthy = monitor_healthy.load(std::memory_order_acquire),
                                   .manual_input_active = false};
    };
    ControlHost host(std::move(options));
    CHECK(host.Start());
    CHECK(host.Connect());
    now_ns.store(kStartNs + 1, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(1, kStartNs + 1)).outcome,
             CommandOutcome::Accepted);
    now_ns.store(kStartNs + 2, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(2, kStartNs + 2, 0, 0.5, 0, true), SafeGates()).outcome,
             CommandOutcome::SafetyBlocked);
    const auto snapshot = host.Snapshot();
    CHECK(snapshot.state == ControlState::Disengaged);
    CHECK(snapshot.steering == 0.0 && snapshot.throttle == 0.0 && snapshot.brake == 0.0);
    host.Stop();
}

ETS2AP_TEST(ControlHostInputMonitorDisengagesWithoutANewCommand) {
    std::atomic<std::uint64_t> now_ns{kStartNs};
    std::atomic<bool> physical_input_active{false};
    CapturedOutput output;
    auto options = OptionsFor(now_ns, output);
    options.sample_physical_input = [&physical_input_active] {
        return PhysicalInputStatus{.sample_valid = true, .healthy = true,
                                   .manual_input_active = physical_input_active.load(
                                       std::memory_order_acquire)};
    };
    ControlHost host(std::move(options));
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

    physical_input_active.store(true, std::memory_order_release);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::ManualOverrideLatched &&
               snapshot.steering == 0.0 && snapshot.throttle == 0.0 && snapshot.brake == 0.0;
    }));
    host.Stop();
}

ETS2AP_TEST(ControlHostLosingHealthyInputMonitorNeutralizesOutputs) {
    std::atomic<std::uint64_t> now_ns{kStartNs};
    std::atomic<bool> monitor_healthy{true};
    CapturedOutput output;
    auto options = OptionsFor(now_ns, output);
    options.sample_physical_input = [&monitor_healthy] {
        return PhysicalInputStatus{.sample_valid = true,
                                   .healthy = monitor_healthy.load(std::memory_order_acquire),
                                   .manual_input_active = false};
    };
    ControlHost host(std::move(options));
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

    monitor_healthy.store(false, std::memory_order_release);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::ManualOverrideLatched &&
               snapshot.steering == 0.0 && snapshot.throttle == 0.0 && snapshot.brake == 0.0;
    }));
    host.Stop();
}

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
    std::atomic<bool> physical_input_active{false};
    CapturedOutput output;
    auto options = OptionsFor(now_ns, output);
    options.sample_physical_input = [&physical_input_active] {
        return PhysicalInputStatus{.sample_valid = true, .healthy = true,
                                   .manual_input_active = physical_input_active.load(
                                       std::memory_order_acquire)};
    };
    ControlHost host(std::move(options));
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

    physical_input_active.store(true, std::memory_order_release);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::ManualOverrideLatched &&
               snapshot.steering == 0.0 && snapshot.throttle == 0.0 && snapshot.brake == 0.0;
    }));
    physical_input_active.store(false, std::memory_order_release);
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
