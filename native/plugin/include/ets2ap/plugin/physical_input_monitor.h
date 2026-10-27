// Edited by: OpenAI-Codex (OpenAI-Codex_Responsible: Source_Code)
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace ets2ap::plugin {

inline constexpr std::size_t kKeyboardInputFrameSize = 24;
inline constexpr auto kKeyboardInputReportTimeout = std::chrono::milliseconds{250};

struct KeyboardInputReport {
    std::uint64_t sequence{};
    bool raw_input_available{};
    bool manual_input_active{true};
};

struct KeyboardInputObservation {
    bool available{};
    bool manual_input_active{true};
};

bool DecodeKeyboardInputFrame(std::span<const std::byte> frame,
                              std::uint64_t previous_sequence,
                              KeyboardInputReport& report) noexcept;

class KeyboardInputReportState {
public:
    void BeginConnection() noexcept;
    bool Update(const KeyboardInputReport& report,
                std::chrono::steady_clock::time_point received_at) noexcept;
    void Disconnect() noexcept;
    KeyboardInputObservation Snapshot(
        std::chrono::steady_clock::time_point now) const noexcept;

private:
    mutable std::mutex mutex_{};
    KeyboardInputReport report_{};
    std::chrono::steady_clock::time_point received_at_{};
    bool connected_{};
    bool has_report_{};
};

KeyboardInputReportState& GlobalKeyboardInputReportState() noexcept;

struct PhysicalInputSample {
    bool keyboard_state_observed{};
    bool xinput_probe_available{};
    bool device_inventory_available{};
    bool keyboard_active{};
    bool xinput_active{};
    bool unclassified_hid_present{};
    bool coverage_validated{}; // Production remains false until device coverage is verified in game.
};

using KeyboardStateReader = std::uint16_t (*)(int key) noexcept;

// GetAsyncKeyState cannot distinguish an all-zero key scan from access failure.
// Only a high-order key-down bit is positive evidence of an observable state.
bool HasObservableKeyboardActivity(KeyboardStateReader read_key_state) noexcept;

struct PhysicalInputStatus {
    bool sample_valid{};
    bool healthy{};
    bool manual_input_active{true};
};

PhysicalInputStatus EvaluatePhysicalInputSample(const PhysicalInputSample& sample) noexcept;
PhysicalInputSample ReadSystemPhysicalInputSample(
    KeyboardInputObservation keyboard_input = {}) noexcept;

} // namespace ets2ap::plugin
