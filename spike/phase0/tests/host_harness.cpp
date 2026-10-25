// Edited by: OpenAI-Codex_Responsible: Source_Code
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "scssdk_input.h"
#include "scssdk_telemetry.h"
#include "common/scssdk_telemetry_truck_common_channels.h"
#include "../src/script.h"

#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

namespace {
int checks = 0;
int failures = 0;
int invalid_callback_results = 0;
int callback_calls = 0;
int residual_temp_directories = 0;
unsigned temp_counter = 0;

void check(bool condition, const char* label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label);
    }
}

struct Channel {
    std::string name;
    scs_u32_t index;
    scs_value_type_t type;
    scs_u32_t flags;
    scs_telemetry_channel_callback_t callback;
    scs_context_t context;
};
struct Input {
    std::string name;
    std::string display;
    scs_value_type_t type;
};
struct Device {
    std::string name;
    std::string display;
    scs_input_device_type_t type;
    std::vector<Input> inputs;
    scs_input_event_callback_t event_callback;
    scs_input_active_callback_t active_callback;
    scs_context_t context;
};
struct Event {
    scs_event_t type;
    scs_telemetry_event_callback_t callback;
    scs_context_t context;
};
std::vector<Channel> channels;
std::vector<Device> devices;
std::vector<Event> events;
int fail_device_index = -1;
bool fail_channel = false;

SCSAPI_VOID game_log(scs_log_type_t, scs_string_t) {}
SCSAPI_RESULT register_event(scs_event_t event, scs_telemetry_event_callback_t callback, scs_context_t context) {
    events.push_back({event, callback, context});
    return SCS_RESULT_ok;
}
SCSAPI_RESULT unregister_event(scs_event_t) { return SCS_RESULT_ok; }
SCSAPI_RESULT register_channel(scs_string_t name, scs_u32_t index, scs_value_type_t type, scs_u32_t flags,
                               scs_telemetry_channel_callback_t callback, scs_context_t context) {
    channels.push_back({name ? name : "", index, type, flags, callback, context});
    return fail_channel ? SCS_RESULT_not_found : SCS_RESULT_ok;
}
SCSAPI_RESULT unregister_channel(scs_string_t, scs_u32_t, scs_value_type_t) { return SCS_RESULT_ok; }
SCSAPI_RESULT register_device(const scs_input_device_t* info) {
    Device device{info->name ? info->name : "", info->display_name ? info->display_name : "", info->type,
                  {}, info->input_event_callback, info->input_active_callback, info->callback_context};
    for (scs_u32_t i = 0; i < info->input_count; ++i) {
        const auto& input = info->inputs[i];
        device.inputs.push_back({input.name ? input.name : "", input.display_name ? input.display_name : "", input.value_type});
    }
    devices.push_back(std::move(device));
    return fail_device_index == -2 || fail_device_index == static_cast<int>(devices.size()) - 1
        ? SCS_RESULT_generic_error : SCS_RESULT_ok;
}

using TelemetryInit = scs_result_t (SCSAPIFUNC*)(scs_u32_t, const scs_telemetry_init_params_t*);
using InputInit = scs_result_t (SCSAPIFUNC*)(scs_u32_t, const scs_input_init_params_t*);
using Shutdown = void (SCSAPIFUNC*)();

void reset() {
    events.clear(); channels.clear(); devices.clear();
    fail_device_index = -1;
    fail_channel = false;
}

struct CaseDirectory {
    std::filesystem::path path;
    bool valid = false;
    explicit CaseDirectory(const char* name) {
        wchar_t buffer[MAX_PATH + 1]{};
        const DWORD count = GetTempPathW(MAX_PATH, buffer);
        if (count == 0 || count >= MAX_PATH) return;
        path = std::filesystem::path(buffer) /
            (L"ets2ap_harness_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
             std::to_wstring(++temp_counter));
        valid = CreateDirectoryW(path.c_str(), nullptr) != 0;
        check(valid, name);
        if (valid) SetEnvironmentVariableW(L"ETS2AP_SPIKE_DIR", path.c_str());
    }
    ~CaseDirectory() {
        SetEnvironmentVariableW(L"ETS2AP_SPIKE_DIR", nullptr);
        if (valid) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
            if (error || std::filesystem::exists(path)) ++residual_temp_directories;
        }
    }
    void write(const char* file, std::string_view content) const {
        std::ofstream out(path / file, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        check(out.good(), "test fixture write");
    }
};

std::vector<std::string> lines(const std::filesystem::path& path) {
    std::vector<std::string> result;
    std::ifstream file(path, std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        result.push_back(line);
    }
    return result;
}

std::vector<std::filesystem::path> matching(const std::filesystem::path& dir, std::string_view prefix) {
    std::vector<std::filesystem::path> result;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().filename().string().starts_with(prefix)) result.push_back(entry.path());
    }
    return result;
}

size_t field_count(std::string_view line) {
    return 1 + static_cast<size_t>(std::count(line.begin(), line.end(), ','));
}
std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> result;
    while (true) {
        const size_t comma = line.find(',');
        if (comma == std::string_view::npos) {
            result.push_back(line);
            return result;
        }
        result.push_back(line.substr(0, comma));
        line.remove_prefix(comma + 1);
    }
}

struct Api {
    TelemetryInit telemetry_init = nullptr;
    Shutdown telemetry_shutdown = nullptr;
    InputInit input_init = nullptr;
    Shutdown input_shutdown = nullptr;
} api;

scs_telemetry_init_params_v101_t telemetry_params() {
    scs_telemetry_init_params_v101_t params{};
    params.common.game_name = "Euro Truck Simulator 2";
    params.common.game_id = "eut2";
    params.common.game_version = 0;
    params.common.log = game_log;
    params.register_for_event = register_event;
    params.unregister_from_event = unregister_event;
    params.register_for_channel = register_channel;
    params.unregister_from_channel = unregister_channel;
    return params;
}
scs_input_init_params_v100_t input_params() {
    scs_input_init_params_v100_t params{};
    params.common.game_name = "Euro Truck Simulator 2";
    params.common.game_id = "eut2";
    params.common.game_version = 0;
    params.common.log = game_log;
    params.register_device = register_device;
    return params;
}

void invoke_event(scs_event_t type, const void* info = nullptr) {
    for (const auto& event : events) {
        if (event.type == type && event.callback) event.callback(type, info, event.context);
    }
}
void telemetry_frame(unsigned number, bool omit_speed = false) {
    scs_telemetry_frame_start_t start{};
    start.render_time = static_cast<scs_timestamp_t>(number) * 2000;
    start.simulation_time = start.render_time;
    start.paused_simulation_time = start.render_time;
    invoke_event(SCS_TELEMETRY_EVENT_frame_start, &start);
    for (const auto& channel : channels) {
        if (!channel.callback) continue;
        if (omit_speed && channel.name == SCS_TELEMETRY_TRUCK_CHANNEL_speed) {
            continue;
        }
        scs_value_t value{};
        value.type = channel.type;
        if (channel.type == SCS_VALUE_TYPE_float) value.value_float.value = static_cast<float>(number) / 10.0F;
        else if (channel.type == SCS_VALUE_TYPE_bool) value.value_bool.value = 1;
        else if (channel.type == SCS_VALUE_TYPE_s32) value.value_s32.value = 3;
        channel.callback(channel.name.c_str(), channel.index, &value, channel.context);
    }
    invoke_event(SCS_TELEMETRY_EVENT_frame_end);
}

struct FrameResult {
    std::vector<scs_input_event_t> emitted;
    bool terminated = false;
};
FrameResult input_frame(const Device& device, bool first_activation = false) {
    FrameResult frame;
    if (!device.event_callback) {
        check(false, "input callback exists");
        return frame;
    }
    for (int call = 0; call < 32; ++call) {
        scs_input_event_t event{};
        const scs_u32_t flags = call == 0
            ? (SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame |
               (first_activation ? SCS_INPUT_EVENT_CALLBACK_FLAG_first_after_activation : 0U))
            : 0U;
        const scs_result_t result = device.event_callback(&event, flags, device.context);
        ++callback_calls;
        if (result != SCS_RESULT_ok && result != SCS_RESULT_not_found) ++invalid_callback_results;
        if (result == SCS_RESULT_not_found) {
            frame.terminated = true;
            break;
        }
        if (result != SCS_RESULT_ok) break;
        check(event.input_index == frame.emitted.size(), "input index sequence");
        frame.emitted.push_back(event);
    }
    check(frame.terminated, "input frame terminates");
    return frame;
}

float axis(const FrameResult& frame, size_t index) {
    return index < frame.emitted.size() ? frame.emitted[index].value_float.value : -999.0F;
}
bool approximately(float value, float expected) { return value > expected - 0.001F && value < expected + 0.001F; }

void case_result(const char* name, int before) {
    check(checks > before, "case has at least one check");
    std::printf("%s checks = %d\n", name, checks - before);
}

void test_script_function() {
    using ets2ap_spike::Mode;
    using ets2ap_spike::evaluate_script;
    using ets2ap_spike::semantical_float;
    using ets2ap_spike::script_duration;
    const int before = checks;
    const auto steer = [](double t) { return evaluate_script(Mode::steer, t).steering; };
    const auto throttle = [](double t) { return evaluate_script(Mode::throttle, t).throttle; };
    const auto brake = [](double t) { return evaluate_script(Mode::brake, t).brake; };
    const auto button = [](double t) { return evaluate_script(Mode::buttons, t).test_button; };
    check(semantical_float("steering", evaluate_script(Mode::steer, 3.0)) == 0.5F,
          "semantic steering maps steering");
    check(semantical_float("aforward", evaluate_script(Mode::throttle, 3.0)) == 0.3F,
          "semantic aforward maps throttle");
    check(semantical_float("abackward", evaluate_script(Mode::brake, 3.0)) == 0.3F,
          "semantic abackward maps brake");
    check(semantical_float("unknown", evaluate_script(Mode::steer, 3.0)) == 0.0F,
          "unknown semantic input is neutral");
    check(steer(0.0) == 0.0F, "script steer at 0");
    check(steer(2.0) == 0.5F, "script steer at 2");
    check(steer(4.999) == 0.5F, "script steer before 5");
    check(steer(5.0) == 0.0F, "script steer at 5");
    check(steer(7.0) == -0.5F, "script steer at 7");
    check(steer(10.0) == 0.0F, "script steer at 10");
    check(steer(12.0) == 0.0F, "script steer at 12");
    check(steer(12.39) == 0.0F && std::fabs(static_cast<double>(steer(12.4)) - 0.1) < 1e-6,
          "script steer ramp holds each step");
    check(std::fabs(static_cast<double>(steer(14.0)) - 0.5) < 1e-9, "script steer at 14");
    check(steer(15.999) >= 0.9F, "script steer near 16");
    check(steer(16.0) == 0.0F, "script steer at 16");
    check(steer(18.0) == 0.0F && script_duration(Mode::steer) == 18.0,
          "script steer ends at 18");
    check(throttle(0.0) == 0.0F, "script throttle at 0");
    check(throttle(2.0) == 0.3F, "script throttle at 2");
    check(throttle(4.999) == 0.3F, "script throttle before 5");
    check(throttle(5.0) == 0.0F, "script throttle at 5");
    check(throttle(7.0) == 0.6F, "script throttle at 7");
    check(throttle(9.999) == 0.6F, "script throttle before 10");
    check(throttle(10.0) == 0.0F, "script throttle at 10");
    check(throttle(12.0) == 0.0F && script_duration(Mode::throttle) == 12.0,
          "script throttle ends at 12");
    check(brake(0.0) == 0.0F, "script brake at 0");
    check(brake(2.0) == 0.3F, "script brake at 2");
    check(brake(4.999) == 0.3F, "script brake before 5");
    check(brake(5.0) == 0.0F, "script brake at 5");
    check(brake(7.0) == 0.6F, "script brake at 7");
    check(brake(9.999) == 0.6F, "script brake before 10");
    check(brake(10.0) == 0.0F, "script brake at 10");
    check(brake(12.0) == 0.0F && script_duration(Mode::brake) == 12.0,
          "script brake ends at 12");
    check(!button(0.0), "script button at 0");
    check(button(1.0), "script button at 1");
    check(button(1.999), "script button before 2");
    check(!button(2.0), "script button at 2");
    check(button(4.0), "script button at 4");
    check(!button(5.0), "script button at 5");
    check(button(7.0), "script button at 7");
    check(!button(8.0), "script button at 8");
    check(!button(10.0) && script_duration(Mode::buttons) == 10.0,
          "script button ends at 10");
    check(evaluate_script(Mode::off, 3.0).steering == 0.0F, "script off is neutral");
    std::printf("Script function checks = %d\n", checks - before);
}

void test_scripts() {
    const int before = checks;
    CaseDirectory dir("T4 directory created");
    if (!dir.valid) return;
    dir.write("config.txt", "devices=generic\nidle=zero\nscript_speed=20\n");
    dir.write("mode.txt", "steer 1\n");
    reset();
    auto telemetry = telemetry_params();
    auto input = input_params();
    check(api.telemetry_init(SCS_TELEMETRY_VERSION_1_01, &telemetry) == SCS_RESULT_ok, "T4 telemetry init");
    check(api.input_init(SCS_INPUT_VERSION_1_00, &input) == SCS_RESULT_ok, "T4 input init");
    if (devices.empty()) {
        check(false, "T4 generic device exists");
        api.input_shutdown();
        api.telemetry_shutdown();
        case_result("T4", before);
        return;
    }
    if (devices[0].active_callback) devices[0].active_callback(1, devices[0].context);
    bool positive = false, negative = false, peak = false, ramp_seen = false;
    bool ramp_monotonic = true, returned_zero = false;
    bool ramp_sample_aligned = false;
    float previous_ramp = 0.0F;
    float max_steering = 0.0F;
    for (unsigned i = 0; i < 550; ++i) {
        bool align_after_sleep = false;
        std::chrono::steady_clock::time_point ramp_target{};
        const auto frame = input_frame(devices[0], i == 0);
        check(frame.emitted.size() == 4, "T4 four events per frame");
        if (frame.emitted.size() == 4) {
            const float steering = -axis(frame, 0);
            positive |= approximately(steering, 0.5F);
            negative |= approximately(steering, -0.5F);
            peak |= steering >= 0.9F;
            if (steering > 0.5F && steering < 1.0F) {
                if (ramp_seen && steering + 0.00001F < previous_ramp) ramp_monotonic = false;
                previous_ramp = steering;
            }
            if (!ramp_sample_aligned && steering > 0.55F && steering < 0.7F) {
                // Sleep(2) can be preempted beyond the 20 ms window for >=0.9.
                // Align one later frame to the middle of that window while
                // retaining the required two-millisecond sleep per frame.
                const double script_t = 12.0 + 4.0 * (static_cast<double>(steering) + 0.05);
                const double seconds_until_target = (15.8 - script_t) / 20.0;
                ramp_target = std::chrono::steady_clock::now() +
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>(seconds_until_target));
                align_after_sleep = true;
                ramp_sample_aligned = true;
            }
            ramp_seen |= steering > 0.8F;
            max_steering = std::max(max_steering, steering);
            if (ramp_seen && approximately(steering, 0.0F)) returned_zero = true;
        }
        telemetry_frame(i);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (align_after_sleep) {
            while (std::chrono::steady_clock::now() < ramp_target) { /* Test-only timing alignment. */ }
        }
    }
    check(positive, "T4 steering positive interval");
    check(negative, "T4 steering negative interval");
    check(peak, "T4 steering ramp reaches 0.9");
    check(ramp_seen && ramp_monotonic, "T4 sampled ramp is monotonic");
    check(returned_zero, "T4 steering returns to zero");
    std::printf("T4 max sampled steering = %.6f\n", static_cast<double>(max_steering));

    struct ModeCheck { const char* mode; size_t index; float expected; };
    for (const ModeCheck mode : {ModeCheck{"throttle", 1, 0.3F}, ModeCheck{"brake", 2, 0.3F}}) {
        dir.write("mode.txt", std::string(mode.mode) + " 2\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        bool found_first = false, found_second = false;
        for (unsigned i = 0; i < 150; ++i) {
            const auto frame = input_frame(devices[0]);
            if (frame.emitted.size() == 4) {
                found_first |= approximately(axis(frame, mode.index), mode.expected);
                found_second |= approximately(axis(frame, mode.index), 0.6F);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(found_first, "T4 first pedal output");
        check(found_second, "T4 second pedal output");
    }
    dir.write("mode.txt", "buttons 3\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    bool button_on = false, button_off = false;
    for (unsigned i = 0; i < 150; ++i) {
        const auto frame = input_frame(devices[0]);
        if (frame.emitted.size() == 4) {
            button_on |= frame.emitted[3].value_bool.value != 0;
            button_off |= frame.emitted[3].value_bool.value == 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(button_on && button_off, "T4 button pulses and rests");
    dir.write("mode.txt", "steer 4\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    bool restarted = false;
    for (unsigned i = 0; i < 70; ++i) {
        const auto frame = input_frame(devices[0]);
        restarted |= approximately(axis(frame, 0), -0.5F);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(restarted, "T4 changed token restarts script");
    dir.write("mode.txt", "invalid 5\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    const auto invalid = input_frame(devices[0]);
    check(invalid.emitted.size() == 4 && approximately(axis(invalid, 0), 0.0F), "T4 invalid mode is off");
    api.input_shutdown();
    api.telemetry_shutdown();
    case_result("T4", before);
}

void test_silent() {
    const int before = checks;
    CaseDirectory dir("T5 directory created");
    if (!dir.valid) return;
    dir.write("config.txt", "devices=generic\nidle=silent\nscript_speed=100\n");
    reset();
    auto input = input_params();
    check(api.input_init(SCS_INPUT_VERSION_1_00, &input) == SCS_RESULT_ok, "T5 input init");
    if (devices.empty()) {
        check(false, "T5 device exists");
        api.input_shutdown();
        case_result("T5", before);
        return;
    }
    if (devices[0].active_callback) devices[0].active_callback(1, devices[0].context);
    check(input_frame(devices[0], true).emitted.empty(), "T5 silent before script");
    dir.write("mode.txt", "steer 1\n");
    bool nonzero = false, neutral_after = false, silent_after = false;
    for (unsigned i = 0; i < 500; ++i) {
        const auto frame = input_frame(devices[0]);
        if (frame.emitted.size() == 4 && !approximately(axis(frame, 0), 0.0F)) nonzero = true;
        if (nonzero && frame.emitted.size() == 4 && approximately(axis(frame, 0), 0.0F)) neutral_after = true;
        if (neutral_after && frame.emitted.empty()) silent_after = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(nonzero, "T5 script emitted nonzero");
    check(neutral_after, "T5 neutral frame after script");
    check(silent_after, "T5 no events after neutral frame");
    api.input_shutdown();
    case_result("T5", before);
}

void test_files() {
    const int before = checks;
    CaseDirectory dir("T6 directory created");
    if (!dir.valid) return;
    dir.write("config.txt", "devices=generic\n");
    reset();
    auto telemetry = telemetry_params();
    auto input = input_params();
    check(api.telemetry_init(SCS_TELEMETRY_VERSION_1_01, &telemetry) == SCS_RESULT_ok, "T6 telemetry init");
    check(api.input_init(SCS_INPUT_VERSION_1_00, &input) == SCS_RESULT_ok, "T6 input init");
    if (devices.empty()) {
        check(false, "T6 device exists");
        api.input_shutdown();
        api.telemetry_shutdown();
        case_result("T6", before);
        return;
    }
    for (unsigned i = 0; i < 10; ++i) {
        input_frame(devices[0], i == 0);
        telemetry_frame(i, i == 3);
    }
    api.input_shutdown();
    api.telemetry_shutdown();
    const auto frame_files = matching(dir.path, "frames_");
    const auto input_files = matching(dir.path, "inputs_");
    check(frame_files.size() == 1, "T6 one frames CSV");
    check(input_files.size() == 1, "T6 one inputs CSV");
    if (!frame_files.empty()) {
        const auto rows = lines(frame_files[0]);
        check(rows.size() == 11, "T6 ten telemetry rows");
        if (!rows.empty()) {
            for (const auto& row : rows) check(field_count(row) == field_count(rows[0]), "T6 frame CSV width");
        }
        if (rows.size() > 4) {
            const auto columns = fields(rows[4]);
            check(columns.size() > 6 && columns[6].empty(), "T6 unavailable speed blank");
        }
        std::printf("T6 frames CSV rows = %zu\n", rows.empty() ? 0 : rows.size() - 1);
    }
    if (!input_files.empty()) {
        const auto rows = lines(input_files[0]);
        check(rows.size() == 11, "T6 ten input rows");
        if (!rows.empty()) for (const auto& row : rows) check(field_count(row) == field_count(rows[0]), "T6 input CSV width");
        if (rows.size() > 1) {
            check(rows[0].find("steering_event_reported") != std::string::npos,
                  "T6 steering event column");
            const auto columns = fields(rows[1]);
            check(!columns.empty() && columns.back() == "1", "T6 generic steering event emitted");
        }
        std::printf("T6 inputs CSV rows = %zu\n", rows.empty() ? 0 : rows.size() - 1);
    }
    const auto logs = lines(dir.path / "spike.log");
    bool init = false, registration = false, stats = false;
    for (const auto& line : logs) {
        init |= line.find("init") != std::string::npos;
        registration |= line.find("register") != std::string::npos;
        stats |= line.find("stats") != std::string::npos || line.find("statistics") != std::string::npos;
    }
    check(init, "T6 log contains init");
    check(registration, "T6 log contains registration");
    check(stats, "T6 log contains statistics");
    std::printf("T6 log rows = %zu\n", logs.size());
    case_result("T6", before);
}

void test_registration_errors() {
    const int before = checks;
    CaseDirectory dir("T7 directory created");
    if (!dir.valid) return;
    dir.write("config.txt", "devices=both\nsemantical_inputs=lightoff:bool,aux:float\n");
    auto input = input_params();
    reset();
    fail_device_index = 1;
    check(api.input_init(SCS_INPUT_VERSION_1_00, &input) == SCS_RESULT_ok,
          "T7 second device rejection keeps first");
    check(devices.size() == 2, "T7 two device attempts");
    api.input_shutdown();
    reset();
    fail_device_index = -2;
    check(api.input_init(SCS_INPUT_VERSION_1_00, &input) == SCS_RESULT_generic_error,
          "T7 all device registrations rejected");
    check(!devices.empty(), "T7 device attempt observed");
    api.input_shutdown();
    reset();
    fail_channel = true;
    auto telemetry = telemetry_params();
    check(api.telemetry_init(SCS_TELEMETRY_VERSION_1_01, &telemetry) == SCS_RESULT_ok,
          "T7 missing telemetry channel does not abort init");
    check(channels.size() == 13, "T7 thirteen channels attempted");
    api.telemetry_shutdown();
    const auto logs = lines(dir.path / "spike.log");
    bool rejected = false;
    for (const auto& line : logs) rejected |= line.find("-7") != std::string::npos || line.find("not_found") != std::string::npos;
    check(rejected, "T7 registration rejection logged");
    case_result("T7", before);
}

void test_bad_config() {
    const int before = checks;
    CaseDirectory dir("T8 directory created");
    if (!dir.valid) return;
    auto input = input_params();
    const std::array<std::string, 4> configs = {
        std::string{}, std::string(5000, 'x'), "devices=wrong\nscript_speed=0\nidle=wrong\n",
        std::string("devices=generic\n") + static_cast<char>(0xFF) + "\n"
    };
    size_t config_index = 0;
    for (const auto& config : configs) {
        dir.write("config.txt", config);
        dir.write("mode.txt", std::string(300, 'x'));
        reset();
        check(api.input_init(SCS_INPUT_VERSION_1_00, &input) == SCS_RESULT_ok,
              "T8 invalid configuration does not abort init");
        const bool generic_only = config_index == 3;
        check(devices.size() == (generic_only ? 1U : 2U), "T8 default device count");
        if (generic_only && devices.size() == 1) {
                check(devices[0].name == "ets2ap_spike", "T8 valid generic setting survives malformed line");
                check(devices[0].type == SCS_INPUT_DEVICE_TYPE_generic, "T8 retained generic device type");
                check(devices[0].inputs.size() == 4, "T8 retained generic input count");
        } else if (!generic_only && devices.size() == 2) {
                check(devices[0].name == "ets2ap_spike", "T8 default generic device name");
                check(devices[0].type == SCS_INPUT_DEVICE_TYPE_generic, "T8 default generic device type");
                check(devices[0].inputs.size() == 4, "T8 default generic input count");
                check(devices[1].name == "ets2ap_spike_sem", "T8 default semantic device name");
                check(devices[1].type == SCS_INPUT_DEVICE_TYPE_semantical, "T8 default semantic device type");
                check(devices[1].inputs.size() == 3, "T8 default semantic input count");
                if (devices[1].inputs.size() == 3) {
                    check(devices[1].inputs[0].name == "steering", "T8 default steering mix");
                    check(devices[1].inputs[1].name == "aforward", "T8 default throttle mix");
                    check(devices[1].inputs[2].name == "abackward", "T8 default brake mix");
                }
        }
        for (auto& device : devices) input_frame(device);
        api.input_shutdown();
        ++config_index;
    }
    const auto logs = lines(dir.path / "spike.log");
    bool invalid = false;
    for (const auto& line : logs) {
        invalid |= line.find("invalid") != std::string::npos || line.find("trunc") != std::string::npos ||
                   line.find("ignored") != std::string::npos;
    }
    check(invalid, "T8 bad input logged");
    case_result("T8", before);
}

void test_reinit() {
    const int before = checks;
    CaseDirectory dir("T9 directory created");
    if (!dir.valid) return;
    dir.write("config.txt", "devices=generic\n");
    auto telemetry = telemetry_params();
    auto input = input_params();
    size_t previous_frames = 0, previous_inputs = 0;
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        reset();
        check(api.telemetry_init(SCS_TELEMETRY_VERSION_1_01, &telemetry) == SCS_RESULT_ok,
              "T9 telemetry reinit");
        check(api.input_init(SCS_INPUT_VERSION_1_00, &input) == SCS_RESULT_ok,
              "T9 input reinit");
        if (!devices.empty()) input_frame(devices[0]);
        telemetry_frame(cycle);
        api.input_shutdown();
        api.telemetry_shutdown();
        const auto frame_files = matching(dir.path, "frames_");
        const auto input_files = matching(dir.path, "inputs_");
        check(frame_files.size() > previous_frames, "T9 new frames CSV on reinit");
        check(input_files.size() > previous_inputs, "T9 new inputs CSV on reinit");
        previous_frames = frame_files.size();
        previous_inputs = input_files.size();
    }
    std::printf("T9 frames CSV files = %zu, inputs CSV files = %zu\n", previous_frames, previous_inputs);
    case_result("T9", before);
}
}

int main(int argc, char** argv) {
    test_script_function();
    if (argc != 2) {
        std::fprintf(stderr, "usage: phase0_host_harness <dll>\n");
        return 2;
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, nullptr, 0);
    if (length <= 0) return 2;
    std::wstring path(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, path.data(), length);
    HMODULE library = LoadLibraryW(path.c_str());
    {
        const int before = checks;
        CaseDirectory dir("T1 directory created");
        if (!dir.valid) { if (library) FreeLibrary(library); return 1; }
        check(library != nullptr, "T1 LoadLibraryW");
        if (!library) {
            std::fprintf(stderr, "LoadLibraryW error = %lu\n", GetLastError());
            return 1;
        }
        api.telemetry_init = reinterpret_cast<TelemetryInit>(GetProcAddress(library, "scs_telemetry_init"));
        api.telemetry_shutdown = reinterpret_cast<Shutdown>(GetProcAddress(library, "scs_telemetry_shutdown"));
        api.input_init = reinterpret_cast<InputInit>(GetProcAddress(library, "scs_input_init"));
        api.input_shutdown = reinterpret_cast<Shutdown>(GetProcAddress(library, "scs_input_shutdown"));
        check(api.telemetry_init != nullptr, "T1 telemetry init export");
        check(api.telemetry_shutdown != nullptr, "T1 telemetry shutdown export");
        check(api.input_init != nullptr, "T1 input init export");
        check(api.input_shutdown != nullptr, "T1 input shutdown export");
        case_result("T1", before);
    }
    if (api.telemetry_init && api.telemetry_shutdown) {
        const int before = checks;
        CaseDirectory dir("T2 directory created");
        if (!dir.valid) { FreeLibrary(library); return 1; }
        reset();
        auto params = telemetry_params();
        check(api.telemetry_init(SCS_TELEMETRY_VERSION_1_01, &params) == SCS_RESULT_ok, "T2 telemetry init");
        check(events.size() == 5, "T2 five events");
        check(channels.size() == 13, "T2 thirteen channels");
        constexpr std::array<std::pair<const char*, scs_value_type_t>, 13> expected = {{
            {SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_VALUE_TYPE_float},
            {SCS_TELEMETRY_TRUCK_CHANNEL_input_steering, SCS_VALUE_TYPE_float},
            {SCS_TELEMETRY_TRUCK_CHANNEL_input_throttle, SCS_VALUE_TYPE_float},
            {SCS_TELEMETRY_TRUCK_CHANNEL_input_brake, SCS_VALUE_TYPE_float},
            {SCS_TELEMETRY_TRUCK_CHANNEL_input_clutch, SCS_VALUE_TYPE_float},
            {SCS_TELEMETRY_TRUCK_CHANNEL_effective_steering, SCS_VALUE_TYPE_float},
            {SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle, SCS_VALUE_TYPE_float},
            {SCS_TELEMETRY_TRUCK_CHANNEL_effective_brake, SCS_VALUE_TYPE_float},
            {SCS_TELEMETRY_TRUCK_CHANNEL_effective_clutch, SCS_VALUE_TYPE_float},
            {SCS_TELEMETRY_TRUCK_CHANNEL_parking_brake, SCS_VALUE_TYPE_bool},
            {SCS_TELEMETRY_TRUCK_CHANNEL_displayed_gear, SCS_VALUE_TYPE_s32},
            {SCS_TELEMETRY_TRUCK_CHANNEL_cruise_control, SCS_VALUE_TYPE_float},
            {SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm, SCS_VALUE_TYPE_float}
        }};
        for (const auto& [name, type] : expected) {
            int matches = 0;
            for (const auto& channel : channels) {
                if (channel.name == name) {
                    ++matches;
                    check(channel.index == SCS_U32_NIL, "T2 nil channel index");
                    check(channel.type == type, "T2 channel type");
                    check((channel.flags & SCS_TELEMETRY_CHANNEL_FLAG_each_frame) != 0, "T2 each-frame channel");
                    check(channel.callback != nullptr, "T2 channel callback");
                }
            }
            check(matches == 1, "T2 channel name once");
        }
        constexpr std::array<scs_event_t, 5> expected_events = {
            SCS_TELEMETRY_EVENT_frame_start, SCS_TELEMETRY_EVENT_frame_end,
            SCS_TELEMETRY_EVENT_paused, SCS_TELEMETRY_EVENT_started,
            SCS_TELEMETRY_EVENT_configuration
        };
        for (const auto expected_event : expected_events) {
            check(std::count_if(events.begin(), events.end(), [=](const Event& event) {
                return event.type == expected_event && event.callback != nullptr;
            }) == 1, "T2 event registered once");
        }
        api.telemetry_shutdown();
        case_result("T2", before);
    }
    if (api.input_init && api.input_shutdown) {
        const int before = checks;
        CaseDirectory dir("T3 directory created");
        if (!dir.valid) { FreeLibrary(library); return 1; }
        dir.write("config.txt", "devices=both\nsemantical_inputs=lightoff:bool,aux:float\n");
        reset();
        auto params = input_params();
        check(api.input_init(SCS_INPUT_VERSION_1_00, &params) == SCS_RESULT_ok, "T3 input init");
        check(devices.size() == 2, "T3 two devices");
        if (devices.size() == 2) {
            check(devices[0].name == "ets2ap_spike", "T3 generic name");
            check(devices[0].display == "ETS2AP Spike", "T3 generic display");
            check(devices[0].type == SCS_INPUT_DEVICE_TYPE_generic, "T3 generic type");
            check(devices[0].inputs.size() == 4, "T3 generic input count");
            const std::array<const char*, 4> names = {"steering", "throttle", "brake", "test_button"};
            const std::array<const char*, 4> displays = {
                "Spike steering", "Spike throttle", "Spike brake", "Spike test button"
            };
            if (devices[0].inputs.size() == 4) for (size_t i = 0; i < 4; ++i) {
                check(devices[0].inputs[i].name == names[i], "T3 generic input name");
                check(devices[0].inputs[i].display == displays[i], "T3 generic input display");
                check(devices[0].inputs[i].type == (i == 3 ? SCS_VALUE_TYPE_bool : SCS_VALUE_TYPE_float),
                      "T3 generic input type");
            }
            check(devices[1].name == "ets2ap_spike_sem", "T3 semantical name");
            check(devices[1].display == "ETS2AP Spike Semantical", "T3 semantical display");
            check(devices[1].type == SCS_INPUT_DEVICE_TYPE_semantical, "T3 semantical type");
            check(devices[1].inputs.size() == 2, "T3 semantical input count");
            if (devices[1].inputs.size() == 2) {
                check(devices[1].inputs[0].name == "lightoff" && devices[1].inputs[0].display == "lightoff" &&
                      devices[1].inputs[0].type == SCS_VALUE_TYPE_bool, "T3 semantical bool");
                check(devices[1].inputs[1].name == "aux" && devices[1].inputs[1].display == "aux" &&
                      devices[1].inputs[1].type == SCS_VALUE_TYPE_float, "T3 semantical float");
            }
        }
        api.input_shutdown();
        case_result("T3", before);
    }
    if (api.telemetry_init && api.telemetry_shutdown && api.input_init && api.input_shutdown) {
        test_scripts();
        test_silent();
        test_files();
        test_registration_errors();
        test_bad_config();
        test_reinit();
    }
    if (api.input_init && api.input_shutdown) {
        const int before = checks;
        CaseDirectory dir("T11 directory created");
        if (!dir.valid) { FreeLibrary(library); return 1; }
        reset();
        auto params = input_params();
        check(api.input_init(SCS_INPUT_VERSION_1_00, &params) == SCS_RESULT_ok, "T11 init");
        check(devices.size() == 2, "T11 default generic and semantic devices");
        if (devices.size() == 2) {
            check(devices[0].name == "ets2ap_spike", "T11 generic device name");
            check(devices[0].type == SCS_INPUT_DEVICE_TYPE_generic, "T11 generic device type");
            check(devices[1].type == SCS_INPUT_DEVICE_TYPE_semantical, "T11 semantic no-bind type");
            check(devices[1].inputs.size() == 3, "T11 default registers three control mixes");
            if (devices[1].inputs.size() == 3) {
                check(devices[1].inputs[0].name == "steering", "T11 steering mix name");
                check(devices[1].inputs[1].name == "aforward", "T11 throttle mix name");
                check(devices[1].inputs[2].name == "abackward", "T11 brake mix name");
            }
            if (devices[1].active_callback) devices[1].active_callback(1, devices[1].context);
            dir.write("mode.txt", "throttle test-1\n");
            const auto throttle_frame = input_frame(devices[1], true);
            check(throttle_frame.emitted.size() == 3, "T11 three events");
            check(approximately(axis(throttle_frame, 0), 0.0F), "T11 steering neutral");
            check(approximately(axis(throttle_frame, 1), 0.0F), "T11 throttle starts neutral");
            check(approximately(axis(throttle_frame, 2), 0.0F), "T11 brake neutral");
        }
        api.input_shutdown();
        const auto input_files = matching(dir.path, "inputs_");
        check(input_files.size() == 1, "T11 input CSV exists");
        if (!input_files.empty()) {
            const auto rows = lines(input_files[0]);
            check(rows.size() >= 2, "T11 input CSV has a frame");
            if (rows.size() >= 2) {
                const auto columns = fields(rows[1]);
                check(!columns.empty() && columns.back() == "1", "T11 semantic steering event emitted");
            }
        }
        case_result("T11", before);
    }
    {
        const int before = checks;
        CaseDirectory dir("T10 directory created");
        check(invalid_callback_results == 0, "T10 callback results limited to ok/not_found");
        check(callback_calls > 0, "T10 callbacks exercised in prior cases");
        std::printf("T10 callback calls = %d, violations = %d\n", callback_calls, invalid_callback_results);
        case_result("T10", before);
    }
    FreeLibrary(library);
    std::printf("%d checks, %d failures\n", checks, failures);
    std::printf("%d residual temporary directories\n", residual_temp_directories);
    if (residual_temp_directories != 0) return 1;
    return (failures != 0 || checks == 0) ? 1 : 0;
}
