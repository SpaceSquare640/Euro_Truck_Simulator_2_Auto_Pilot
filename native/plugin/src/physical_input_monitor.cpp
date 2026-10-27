// Edited by: OpenAI-Codex (OpenAI-Codex_Responsible: Source_Code)
#include "ets2ap/plugin/physical_input_monitor.h"

#include <Windows.h>
#include <Xinput.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>

namespace ets2ap::plugin {
namespace {

std::uint16_t ReadU16(const std::byte* data) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[0])) |
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[1]) << 8);
}

std::uint32_t ReadU32(const std::byte* data) noexcept {
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[0])) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[1])) << 8) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[2])) << 16) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[3])) << 24);
}

std::uint64_t ReadU64(const std::byte* data) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index)
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(data[index])) <<
                 (index * 8);
    return value;
}

using XInputGetStateFunction = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

struct XInputApi {
    HMODULE module{};
    XInputGetStateFunction get_state{};
};

XInputApi LoadSystemXInput() noexcept {
    HMODULE module = LoadLibraryExW(L"xinput1_4.dll", nullptr,
                                    LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        module = LoadLibraryExW(L"xinput9_1_0.dll", nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (!module) return {};

    const auto get_state = reinterpret_cast<XInputGetStateFunction>(
        GetProcAddress(module, "XInputGetState"));
    if (!get_state) {
        FreeLibrary(module);
        return {};
    }
    return {module, get_state};
}

const XInputApi& SystemXInput() noexcept {
    static const XInputApi api = LoadSystemXInput();
    return api;
}

bool IsXInputActive(const XINPUT_GAMEPAD& gamepad) noexcept {
    return gamepad.wButtons != 0 ||
        gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
        gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
        std::abs(static_cast<int>(gamepad.sThumbLX)) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
        std::abs(static_cast<int>(gamepad.sThumbLY)) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
        std::abs(static_cast<int>(gamepad.sThumbRX)) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE ||
        std::abs(static_cast<int>(gamepad.sThumbRY)) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
}

std::uint16_t ReadAsyncKeyState(int key) noexcept {
    return static_cast<std::uint16_t>(GetAsyncKeyState(key));
}

bool ReadDeviceInventory(bool& unclassified_hid_present) noexcept {
    UINT count = 0;
    constexpr UINT kFailure = std::numeric_limits<UINT>::max();
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == kFailure ||
        count > 4096) return false;

    try {
        std::vector<RAWINPUTDEVICELIST> devices(count);
        if (count != 0) {
            UINT actual_count = count;
            if (GetRawInputDeviceList(devices.data(), &actual_count,
                                      sizeof(RAWINPUTDEVICELIST)) == kFailure ||
                actual_count > count) return false;
            devices.resize(actual_count);
        }
        unclassified_hid_present = std::any_of(devices.begin(), devices.end(),
            [](const RAWINPUTDEVICELIST& device) { return device.dwType == RIM_TYPEHID; });
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

bool DecodeKeyboardInputFrame(std::span<const std::byte> frame,
                              std::uint64_t previous_sequence,
                              KeyboardInputReport& report) noexcept {
    if (frame.size() != kKeyboardInputFrameSize ||
        frame[0] != std::byte{'E'} || frame[1] != std::byte{'2'} ||
        frame[2] != std::byte{'P'} || frame[3] != std::byte{'I'} ||
        ReadU16(frame.data() + 4) != 1 ||
        ReadU32(frame.data() + 8) != kKeyboardInputFrameSize ||
        ReadU32(frame.data() + 20) != 0) return false;

    const std::uint16_t flags = ReadU16(frame.data() + 6);
    if ((flags & ~0x0003U) != 0) return false;
    const bool available = (flags & 0x0001U) != 0;
    const bool manual_input_active = (flags & 0x0002U) != 0;
    if (!available && !manual_input_active) return false;

    const std::uint64_t sequence = ReadU64(frame.data() + 12);
    if (sequence == 0 || sequence <= previous_sequence) return false;
    report = {sequence, available, manual_input_active};
    return true;
}

void KeyboardInputReportState::BeginConnection() noexcept {
    try {
        const std::lock_guard lock(mutex_);
        report_ = {};
        received_at_ = {};
        connected_ = true;
        has_report_ = false;
    } catch (...) {
    }
}

bool KeyboardInputReportState::Update(
    const KeyboardInputReport& report,
    std::chrono::steady_clock::time_point received_at) noexcept {
    try {
        const std::lock_guard lock(mutex_);
        if (!connected_ || report.sequence == 0 ||
            (has_report_ && report.sequence <= report_.sequence) ||
            (!report.raw_input_available && !report.manual_input_active)) return false;
        report_ = report;
        received_at_ = received_at;
        has_report_ = true;
        return true;
    } catch (...) {
        return false;
    }
}

void KeyboardInputReportState::Disconnect() noexcept {
    try {
        const std::lock_guard lock(mutex_);
        report_ = {};
        received_at_ = {};
        connected_ = false;
        has_report_ = false;
    } catch (...) {
    }
}

KeyboardInputObservation KeyboardInputReportState::Snapshot(
    std::chrono::steady_clock::time_point now) const noexcept {
    try {
        const std::lock_guard lock(mutex_);
        if (!connected_ || !has_report_ || !report_.raw_input_available ||
            now < received_at_ || now - received_at_ >= kKeyboardInputReportTimeout) {
            return {};
        }
        return {true, report_.manual_input_active};
    } catch (...) {
        return {};
    }
}

KeyboardInputReportState& GlobalKeyboardInputReportState() noexcept {
    static KeyboardInputReportState state;
    return state;
}

bool HasObservableKeyboardActivity(KeyboardStateReader read_key_state) noexcept {
    if (!read_key_state) return false;
    for (int key = 1; key <= 0xFE; ++key) {
        if ((read_key_state(key) & 0x8000U) != 0) return true;
    }
    return false;
}

PhysicalInputStatus EvaluatePhysicalInputSample(const PhysicalInputSample& sample) noexcept {
    const bool sample_valid = sample.keyboard_state_observed &&
        sample.xinput_probe_available && sample.device_inventory_available;
    return {
        sample_valid,
        sample_valid && sample.coverage_validated && !sample.unclassified_hid_present,
        !sample_valid || sample.keyboard_active || sample.xinput_active};
}

PhysicalInputSample ReadSystemPhysicalInputSample(
    KeyboardInputObservation keyboard_input) noexcept {
    PhysicalInputSample sample{};
    sample.keyboard_state_observed = keyboard_input.available;
    sample.keyboard_active = keyboard_input.manual_input_active ||
        HasObservableKeyboardActivity(&ReadAsyncKeyState);

    const XInputApi& xinput = SystemXInput();
    sample.xinput_probe_available = xinput.get_state != nullptr;
    if (xinput.get_state) {
        for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
            XINPUT_STATE state{};
            const DWORD result = xinput.get_state(index, &state);
            if (result == ERROR_SUCCESS) {
                sample.xinput_active = sample.xinput_active || IsXInputActive(state.Gamepad);
            } else if (result != ERROR_DEVICE_NOT_CONNECTED) {
                sample.xinput_probe_available = false;
            }
        }
    }

    sample.device_inventory_available = ReadDeviceInventory(sample.unclassified_hid_present);
    // Device and keyboard/controller coverage still needs in-game validation.
    sample.coverage_validated = false;
    return sample;
}

} // namespace ets2ap::plugin
