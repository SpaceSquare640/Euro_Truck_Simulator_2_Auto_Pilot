// Edited by: OpenAI-Codex_Responsible: Source_Code
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

#include "scssdk_input.h"
#include "scssdk_telemetry.h"
#include "common/scssdk_telemetry_truck_common_channels.h"
#include "script.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
using ets2ap_spike::Mode;
using ets2ap_spike::Values;
using ets2ap_spike::evaluate_script;
using ets2ap_spike::script_duration;
constexpr std::uint64_t kNsPerSecond = 1'000'000'000ULL;
constexpr std::uint64_t kModePollNs = 500'000'000ULL;
constexpr std::uint64_t kStatsNs = 5'000'000'000ULL;
constexpr std::uint64_t kCsvLimit = 50ULL * 1024ULL * 1024ULL;
constexpr DWORD kMaxPathChars = 32768;

HMODULE g_module = nullptr;
LARGE_INTEGER g_frequency{};

std::uint64_t qpc_ticks() noexcept {
    LARGE_INTEGER value{};
    return QueryPerformanceCounter(&value) && value.QuadPart >= 0
        ? static_cast<std::uint64_t>(value.QuadPart) : 0;
}

std::uint64_t ticks_to_ns(std::uint64_t ticks) noexcept {
    const auto frequency = g_frequency.QuadPart > 0
        ? static_cast<std::uint64_t>(g_frequency.QuadPart) : 1ULL;
    const auto seconds = ticks / frequency;
    const auto remainder = ticks % frequency;
    if (seconds > std::numeric_limits<std::uint64_t>::max() / kNsPerSecond) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const auto whole = seconds * kNsPerSecond;
    // Normal QPC frequencies fit this exact integer calculation. The guarded
    // fallback prevents overflow on an unusually high reported frequency.
    const auto fraction = remainder <= std::numeric_limits<std::uint64_t>::max() / kNsPerSecond
        ? (remainder * kNsPerSecond) / frequency
        : static_cast<std::uint64_t>((static_cast<long double>(remainder) * kNsPerSecond) / frequency);
    return whole > std::numeric_limits<std::uint64_t>::max() - fraction
        ? std::numeric_limits<std::uint64_t>::max() : whole + fraction;
}

std::uint64_t now_ns() noexcept { return ticks_to_ns(qpc_ticks()); }

double elapsed_us(std::uint64_t start_ticks) noexcept {
    const auto end = qpc_ticks();
    if (end < start_ticks || g_frequency.QuadPart <= 0) return 0.0;
    return static_cast<double>(end - start_ticks) * 1'000'000.0 /
        static_cast<double>(g_frequency.QuadPart);
}

std::wstring local_stamp(bool filename) {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t buffer[40]{};
    if (filename) {
        swprintf_s(buffer, L"%04u%02u%02u_%02u%02u%02u", time.wYear,
            time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    } else {
        swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", time.wYear,
            time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
            time.wMilliseconds);
    }
    return buffer;
}

std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring join(const std::wstring& directory, const wchar_t* name) {
    return directory + L"\\" + name;
}

bool is_absolute_directory(const std::wstring& path) noexcept {
    const bool drive = path.size() >= 3 &&
        ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
        path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
    if (!drive) return false; // Never open network shares from the game process.
    const wchar_t root[] = {path[0], L':', L'\\', L'\0'};
    if (GetDriveTypeW(root) == DRIVE_REMOTE) return false;
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::string trim(std::string_view source) {
    size_t begin = 0;
    while (begin < source.size() && std::isspace(static_cast<unsigned char>(source[begin]))) ++begin;
    size_t end = source.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(source[end - 1]))) --end;
    return std::string(source.substr(begin, end - begin));
}

std::string lower(std::string value) {
    for (auto& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

struct CsvFile {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::string pending;
    std::uint64_t written = 0;
    std::uint64_t last_flush_ns = 0;
    bool limited = false;

    void close() noexcept {
        flush();
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
        pending.clear();
        written = 0;
        limited = false;
    }
    void flush() noexcept {
        if (handle == INVALID_HANDLE_VALUE || pending.empty()) return;
        DWORD sent = 0;
        if (!WriteFile(handle, pending.data(), static_cast<DWORD>(pending.size()), &sent, nullptr) ||
            sent != pending.size()) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        } else {
            written += sent;
        }
        pending.clear();
        last_flush_ns = now_ns();
    }
};

enum class Devices { generic, semantical, both, none };
enum class Idle { zero, silent };

struct SemInput { std::string name; scs_value_type_t type; };
struct Config {
    Devices devices = Devices::generic;
    Idle idle = Idle::zero;
    double script_speed = 1.0;
    std::vector<SemInput> semantical_inputs;
};

struct TimerStats {
    std::uint64_t count = 0;
    double sum_us = 0.0;
    double max_us = 0.0;
    void add(double value) noexcept {
        ++count;
        sum_us += value;
        max_us = std::max(max_us, value);
    }
    double average() const noexcept { return count ? sum_us / static_cast<double>(count) : 0.0; }
};

struct DeviceRuntime {
    const char* name = nullptr;
    bool semantical = false;
    bool active = false;
    bool active_at_frame = false;
    bool registered = false;
    bool emit = false;
    bool terminal_zero_pending = false;
    bool pending_row = false;
    std::uint64_t input_frame = 0;
    std::uint64_t frame_ns = 0;
    unsigned next_index = 0;
    unsigned input_count = 0;
    unsigned events_reported = 0;
    Values values{};
    std::string mode = "off";
    double script_t = 0.0;
};

struct ChannelRuntime {
    const char* name;
    scs_value_type_t type;
    bool available = false;
    float float_value = 0.0f;
    int int_value = 0;
};

std::array<ChannelRuntime, 13> g_channels{{
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

struct State {
    std::wstring directory;
    HANDLE log = INVALID_HANDLE_VALUE;
    CsvFile frames;
    CsvFile inputs;
    Config config;
    DeviceRuntime generic{"ets2ap_spike", false};
    DeviceRuntime semantical{"ets2ap_spike_sem", true};
    bool telemetry_initialized = false;
    bool input_initialized = false;
    bool paused = true;
    std::uint64_t frame = 0;
    scs_timestamp_t render_time = 0;
    scs_timestamp_t simulation_time = 0;
    scs_timestamp_t paused_simulation_time = 0;
    Mode mode = Mode::off;
    bool script_ever = false;
    std::string mode_content = "off";
    std::uint64_t mode_polled_ns = 0;
    std::uint64_t script_start_ns = 0;
    std::uint64_t stats_start_ns = 0;
    std::uint64_t stats_telemetry_frames = 0;
    std::uint64_t stats_input_frames = 0;
    TimerStats input_callback_stats;
    TimerStats channel_callback_stats;
    TimerStats frame_end_stats;
    bool injection_available = false;
    bool input_faulted = false;
    std::uint64_t injection_ns = 0;
    Values injection{};
};

State g;

void log_line(const char* level, std::string_view message) noexcept {
    if (g.log == INVALID_HANDLE_VALUE) return;
    try {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << utf8(local_stamp(false)) << " qpc_ns=" << now_ns() << ' '
            << level << ' ' << message << '\n';
        const auto line = out.str();
        DWORD sent = 0;
        if (!WriteFile(g.log, line.data(), static_cast<DWORD>(line.size()), &sent, nullptr) ||
            sent != line.size()) return;
        FlushFileBuffers(g.log);
    } catch (...) { /* Logging must not affect the game. */ }
}

void csv_add(CsvFile& file, const std::string& row, const char* label) noexcept {
    if (file.handle == INVALID_HANDLE_VALUE || file.limited) return;
    try {
        const auto used = file.written + file.pending.size();
        if (used > kCsvLimit || row.size() > kCsvLimit - used) {
            file.limited = true;
            log_line("WARN", std::string(label) + " exceeded 50 MiB; recording stopped");
            return;
        }
        file.pending += row;
        const auto now = now_ns();
        if (now >= file.last_flush_ns && now - file.last_flush_ns >= kNsPerSecond) file.flush();
    } catch (...) { file.close(); log_line("WARN", "CSV write failed"); }
}

void flush_csv() noexcept { g.frames.flush(); g.inputs.flush(); }

void stats_line(bool force) noexcept {
    const auto now = now_ns();
    if (!force && now >= g.stats_start_ns && now - g.stats_start_ns < kStatsNs) return;
    const double interval = now > g.stats_start_ns
        ? static_cast<double>(now - g.stats_start_ns) / static_cast<double>(kNsPerSecond) : 0.0;
    try {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::fixed << std::setprecision(3)
            << "stats input_event count=" << g.input_callback_stats.count
            << " avg_us=" << g.input_callback_stats.average()
            << " max_us=" << g.input_callback_stats.max_us
            << " channel count=" << g.channel_callback_stats.count
            << " avg_us=" << g.channel_callback_stats.average()
            << " max_us=" << g.channel_callback_stats.max_us
            << " frame_end count=" << g.frame_end_stats.count
            << " avg_us=" << g.frame_end_stats.average()
            << " max_us=" << g.frame_end_stats.max_us
            << " telemetry_fps=" << (interval > 0 ? g.stats_telemetry_frames / interval : 0.0)
            << " input_fps=" << (interval > 0 ? g.stats_input_frames / interval : 0.0);
        log_line("INFO", out.str());
    } catch (...) { /* Statistics must not affect the game. */ }
    g.stats_start_ns = now;
    g.stats_telemetry_frames = 0;
    g.stats_input_frames = 0;
    g.input_callback_stats = {};
    g.channel_callback_stats = {};
    g.frame_end_stats = {};
}

std::string read_bounded_file(const std::wstring& path, DWORD limit, bool& exists,
                              bool& truncated) {
    exists = false;
    truncated = false;
    if (limit > 4096) return {};
    std::array<char, 4097> bytes{};
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    exists = true;
    DWORD read = 0;
    const BOOL ok = ReadFile(file, bytes.data(), limit + 1, &read, nullptr);
    CloseHandle(file);
    if (!ok) { exists = false; return {}; }
    truncated = read > limit;
    return std::string(bytes.data(), std::min(read, limit));
}

bool parse_semantical(const std::string& value, std::vector<SemInput>& target) {
    if (value.empty()) { target.clear(); return true; }
    std::vector<SemInput> parsed;
    size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(',', start);
        const auto item = trim(std::string_view(value).substr(start,
            end == std::string::npos ? std::string::npos : end - start));
        const auto colon = item.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 == item.size() ||
            parsed.size() >= 16) return false;
        const auto name = item.substr(0, colon);
        if (!std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        })) return false;
        const auto type = item.substr(colon + 1);
        if (type != "bool" && type != "float") return false;
        parsed.push_back({name, type == "bool" ? SCS_VALUE_TYPE_bool : SCS_VALUE_TYPE_float});
        if (end == std::string::npos) break;
        start = end + 1;
    }
    target = std::move(parsed);
    return true;
}

Config read_config() {
    Config result;
    if (g.directory.empty()) return result;
    bool exists = false, truncated = false;
    const auto text = read_bounded_file(join(g.directory, L"config.txt"), 4096, exists, truncated);
    if (!exists) return result;
    if (truncated) log_line("WARN", "config.txt exceeded 4096 bytes; trailing bytes ignored");
    size_t begin = 0;
    while (begin <= text.size()) {
        const auto end = text.find('\n', begin);
        auto line = trim(std::string_view(text).substr(begin,
            end == std::string::npos ? std::string::npos : end - begin));
        if (!line.empty() && line.front() != '#') {
            const bool ascii = std::all_of(line.begin(), line.end(), [](unsigned char c) { return c < 128; });
            const auto equal = line.find('=');
            if (!ascii || equal == std::string::npos) {
                log_line("WARN", "invalid config.txt line; default retained");
            } else {
                auto key = lower(trim(std::string_view(line).substr(0, equal)));
                auto value = trim(std::string_view(line).substr(equal + 1));
                bool valid = true;
                if (key == "devices") {
                    if (value == "generic") result.devices = Devices::generic;
                    else if (value == "semantical") result.devices = Devices::semantical;
                    else if (value == "both") result.devices = Devices::both;
                    else if (value == "none") result.devices = Devices::none;
                    else valid = false;
                } else if (key == "idle") {
                    if (value == "zero") result.idle = Idle::zero;
                    else if (value == "silent") result.idle = Idle::silent;
                    else valid = false;
                } else if (key == "script_speed") {
                    const bool decimal = !value.empty() &&
                        std::all_of(value.begin(), value.end(), [](unsigned char c) {
                            return (c >= '0' && c <= '9') || c == '.';
                        }) &&
                        std::count(value.begin(), value.end(), '.') <= 1 &&
                        std::any_of(value.begin(), value.end(), [](unsigned char c) {
                            return c >= '0' && c <= '9';
                        });
                    double parsed = 0.0;
                    const auto conversion = decimal
                        ? std::from_chars(value.data(), value.data() + value.size(),
                            parsed, std::chars_format::fixed)
                        : std::from_chars_result{value.data(), std::errc::invalid_argument};
                    if (!decimal || conversion.ec != std::errc{} ||
                        conversion.ptr != value.data() + value.size() ||
                        !std::isfinite(parsed) || parsed < 0.01 || parsed > 100.0)
                        valid = false;
                    else result.script_speed = parsed;
                } else if (key == "semantical_inputs") {
                    valid = parse_semantical(value, result.semantical_inputs);
                } else valid = false;
                if (!valid) log_line("WARN", "invalid config.txt value; default retained");
            }
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

const char* devices_name(Devices value) noexcept {
    switch (value) {
    case Devices::generic: return "generic";
    case Devices::semantical: return "semantical";
    case Devices::both: return "both";
    case Devices::none: return "none";
    }
    return "generic";
}

void log_config(const Config& config) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "config devices=" << devices_name(config.devices)
        << " idle=" << (config.idle == Idle::zero ? "zero" : "silent")
        << " script_speed=" << config.script_speed
        << " semantical_inputs=" << config.semantical_inputs.size();
    log_line("INFO", out.str());
}

void open_session() {
    QueryPerformanceFrequency(&g_frequency);
    g.frame = 0;
    g.paused = true;
    g.mode = Mode::off;
    g.mode_content = "off";
    g.mode_polled_ns = 0;
    g.script_start_ns = 0;
    g.script_ever = false;
    g.injection_available = false;
    g.input_faulted = false;
    g.generic = DeviceRuntime{"ets2ap_spike", false};
    g.semantical = DeviceRuntime{"ets2ap_spike_sem", true};
    g.stats_start_ns = now_ns();
    g.stats_telemetry_frames = g.stats_input_frames = 0;
    g.input_callback_stats = g.channel_callback_stats = g.frame_end_stats = {};

    bool invalid_override = false;
    const DWORD required = GetEnvironmentVariableW(L"ETS2AP_SPIKE_DIR", nullptr, 0);
    if (required > 0) {
        if (required <= kMaxPathChars) {
            std::wstring override_path(static_cast<size_t>(required), L'\0');
            const DWORD copied = GetEnvironmentVariableW(L"ETS2AP_SPIKE_DIR",
                override_path.data(), required);
            if (copied > 0 && copied < required) {
                override_path.resize(copied);
                if (is_absolute_directory(override_path)) g.directory = override_path;
                else invalid_override = true;
            } else invalid_override = true;
        } else invalid_override = true;
    } else if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) invalid_override = true;
    if (g.directory.empty()) {
        PWSTR known = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known)) && known) {
            g.directory = join(known, L"ETS2AP_Spike");
            CoTaskMemFree(known);
            if (!CreateDirectoryW(g.directory.c_str(), nullptr) &&
                GetLastError() != ERROR_ALREADY_EXISTS) g.directory.clear();
        }
    }
    if (g.directory.empty()) return;
    g.log = CreateFileW(join(g.directory, L"spike.log").c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (invalid_override) log_line("WARN", "ETS2AP_SPIKE_DIR ignored: not an existing absolute directory");

    constexpr char frame_header[] =
        "frame,qpc_ns,render_time_us,sim_time_us,paused_sim_time_us,paused,speed,steer_in,steer_eff,thr_in,thr_eff,brk_in,brk_eff,clu_in,clu_eff,parking_brake,gear_displayed,cruise,rpm,inj_steering,inj_throttle,inj_brake,inj_test_button,inj_age_us\n";
    constexpr char input_header[] =
        "device,input_frame,qpc_ns,active,mode,script_t_s,steering,throttle,brake,test_button,events_reported\n";
    for (int attempt = 0; attempt < 30; ++attempt) {
        const auto stamp = local_stamp(true);
        const auto frame_name = join(g.directory, (L"frames_" + stamp + L".csv").c_str());
        const auto input_name = join(g.directory, (L"inputs_" + stamp + L".csv").c_str());
        if (GetFileAttributesW(frame_name.c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(input_name.c_str()) != INVALID_FILE_ATTRIBUTES) {
            Sleep(50);
            continue;
        }
        g.frames.handle = CreateFileW(frame_name.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g.frames.handle == INVALID_HANDLE_VALUE) { Sleep(50); continue; }
        g.inputs.handle = CreateFileW(input_name.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g.inputs.handle == INVALID_HANDLE_VALUE) {
            g.frames.close();
            log_line("WARN", "inputs CSV open failed");
            break;
        }
        csv_add(g.frames, frame_header, "frames CSV");
        csv_add(g.inputs, input_header, "inputs CSV");
        flush_csv();
        break;
    }
    if (g.frames.handle == INVALID_HANDLE_VALUE || g.inputs.handle == INVALID_HANDLE_VALUE)
        log_line("WARN", "CSV open failed; continuing without affected output");
}

void close_session() noexcept {
    stats_line(true);
    g.frames.close();
    g.inputs.close();
    log_line("INFO", "session shutdown");
    if (g.log != INVALID_HANDLE_VALUE) CloseHandle(g.log);
    g.log = INVALID_HANDLE_VALUE;
    g.directory.clear();
}

void log_init(const char* api, scs_u32_t version, const scs_sdk_init_params_v100_t& common) {
    wchar_t module_path[kMaxPathChars]{};
    const DWORD length = GetModuleFileNameW(g_module, module_path, kMaxPathChars);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << api << " init module=" << (length > 0 && length < kMaxPathChars
        ? utf8(std::wstring(module_path, length)) : "<unavailable>")
        << " compiler=MSVC-" << _MSC_VER << " built=" << __DATE__
        << " game_id=" << (common.game_id ? std::string(common.game_id,
            strnlen_s(common.game_id, 64)) : "<null>")
        << " game_version=" << common.game_version
        << " api_version=" << version;
    log_line("INFO", out.str());
}

const char* mode_name(Mode mode) noexcept {
    switch (mode) {
    case Mode::off: return "off";
    case Mode::steer: return "steer";
    case Mode::throttle: return "throttle";
    case Mode::brake: return "brake";
    case Mode::buttons: return "buttons";
    }
    return "off";
}

void poll_mode(std::uint64_t now) {
    if (g.mode_polled_ns && now >= g.mode_polled_ns && now - g.mode_polled_ns < kModePollNs) return;
    g.mode_polled_ns = now;
    bool exists = false, truncated = false;
    const auto content = g.directory.empty() ? std::string{} :
        read_bounded_file(join(g.directory, L"mode.txt"), 256, exists, truncated);
    const auto normalized = exists ? content : std::string("off");
    if (normalized == g.mode_content) return;
    g.mode_content = normalized;
    auto line = trim(normalized);
    const auto separator = line.find_first_of(" \t\r\n");
    const auto name = line.substr(0, separator);
    Mode selected = Mode::off;
    bool valid = !truncated;
    if (name == "steer") selected = Mode::steer;
    else if (name == "throttle") selected = Mode::throttle;
    else if (name == "brake") selected = Mode::brake;
    else if (name == "buttons") selected = Mode::buttons;
    else if (name != "off") valid = false;
    if (!valid) {
        selected = Mode::off;
        log_line("WARN", "invalid mode.txt; treating as off");
    }
    if (g.mode != Mode::off && selected == Mode::off) {
        g.generic.terminal_zero_pending = true;
        g.semantical.terminal_zero_pending = true;
    }
    g.mode = selected;
    if (selected != Mode::off) {
        g.script_ever = true;
        g.script_start_ns = now;
        g.generic.terminal_zero_pending = false;
        g.semantical.terminal_zero_pending = false;
    }
    log_line("INFO", std::string("mode=") + mode_name(g.mode));
}

Values scripted_values(std::uint64_t now, double& script_t) {
    Values result;
    script_t = 0.0;
    if (g.mode == Mode::off) return result;
    if (now >= g.script_start_ns) {
        script_t = static_cast<double>(now - g.script_start_ns) /
            static_cast<double>(kNsPerSecond) * g.config.script_speed;
    }
    if (script_t >= script_duration(g.mode)) {
        log_line("INFO", std::string("script finished mode=") + mode_name(g.mode));
        g.mode = Mode::off;
        g.generic.terminal_zero_pending = true;
        g.semantical.terminal_zero_pending = true;
        return result;
    }
    return evaluate_script(g.mode, script_t);
}

void finish_input_row(DeviceRuntime& device) {
    if (!device.pending_row) return;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << device.name << ',' << device.input_frame << ',' << device.frame_ns << ','
        << (device.active_at_frame ? 1 : 0) << ',' << device.mode << ',' << std::setprecision(9)
        << device.script_t << ',' << device.values.steering << ','
        << device.values.throttle << ',' << device.values.brake << ','
        << (device.values.test_button ? 1 : 0) << ',' << device.events_reported << '\n';
    csv_add(g.inputs, out.str(), "inputs CSV");
    device.pending_row = false;
}

void begin_input_frame(DeviceRuntime& device) {
    finish_input_row(device);
    device.input_frame++;
    g.stats_input_frames++;
    device.active_at_frame = device.active;
    const auto now = now_ns();
    device.frame_ns = now;
    poll_mode(now);
    device.values = scripted_values(now, device.script_t);
    device.mode = mode_name(g.mode);
    device.next_index = 0;
    device.events_reported = 0;
    device.emit = g.config.idle == Idle::zero || g.mode != Mode::off || device.terminal_zero_pending;
    device.terminal_zero_pending = false;
    device.pending_row = true;
    if (!device.semantical) {
        g.injection_available = true;
        g.injection = device.values;
        g.injection_ns = now;
    }
    stats_line(false);
}

SCSAPI_VOID input_active_callback(scs_u8_t active, scs_context_t context) {
    try {
        auto* device = static_cast<DeviceRuntime*>(context);
        if (!device) return;
        device->active = active != 0;
        log_line("INFO", std::string("device_active name=") + device->name +
            " active=" + (device->active ? "1" : "0") + " qpc_ns=" + std::to_string(now_ns()));
    } catch (...) { /* Never propagate into the game. */ }
}

scs_result_t neutral_event(scs_input_event_t* event_info, scs_u32_t flags,
                           DeviceRuntime* device) noexcept {
    if (!device || !event_info) return SCS_RESULT_not_found;
    if (flags & SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame) {
        device->next_index = 0;
        device->events_reported = 0;
        device->values = {};
        device->pending_row = false;
        ++device->input_frame;
        ++g.stats_input_frames;
        device->frame_ns = now_ns();
        if (!device->semantical) {
            g.injection_available = true;
            g.injection = {};
            g.injection_ns = device->frame_ns;
        }
    }
    if (device->next_index >= device->input_count) return SCS_RESULT_not_found;
    const unsigned index = device->next_index++;
    event_info->input_index = index;
    if (device->semantical) {
        if (index >= g.config.semantical_inputs.size()) return SCS_RESULT_not_found;
        if (g.config.semantical_inputs[index].type == SCS_VALUE_TYPE_bool)
            event_info->value_bool.value = 0;
        else event_info->value_float.value = 0.0f;
    } else if (index < 3) {
        event_info->value_float.value = 0.0f;
    } else if (index == 3) {
        event_info->value_bool.value = 0;
    } else {
        return SCS_RESULT_not_found;
    }
    ++device->events_reported;
    return SCS_RESULT_ok;
}

SCSAPI_RESULT input_event_callback(scs_input_event_t* event_info, scs_u32_t flags,
                                   scs_context_t context) {
    const auto started = qpc_ticks();
    if (g.input_faulted) {
        const auto result = neutral_event(event_info, flags,
            static_cast<DeviceRuntime*>(context));
        g.input_callback_stats.add(elapsed_us(started));
        return result;
    }
    scs_result_t result = SCS_RESULT_not_found;
    try {
        auto* device = static_cast<DeviceRuntime*>(context);
        if (device) {
            if (flags & SCS_INPUT_EVENT_CALLBACK_FLAG_first_after_activation)
                log_line("INFO", std::string("first_after_activation name=") + device->name);
            if (flags & SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame) begin_input_frame(*device);
            if (device->emit && event_info && device->next_index < device->input_count) {
                const unsigned index = device->next_index++;
                event_info->input_index = index;
                if (device->semantical) {
                    const auto& input = g.config.semantical_inputs[index];
                    if (input.type == SCS_VALUE_TYPE_bool)
                        event_info->value_bool.value = device->values.test_button ? 1 : 0;
                    else event_info->value_float.value = device->values.steering;
                } else {
                    switch (index) {
                    case 0: event_info->value_float.value = device->values.steering; break;
                    case 1: event_info->value_float.value = device->values.throttle; break;
                    case 2: event_info->value_float.value = device->values.brake; break;
                    case 3: event_info->value_bool.value = device->values.test_button ? 1 : 0; break;
                    default: result = SCS_RESULT_not_found; break;
                    }
                }
                if (index < device->input_count) {
                    device->events_reported++;
                    result = SCS_RESULT_ok;
                }
            }
        }
    } catch (...) {
        if (!g.input_faulted) {
            g.input_faulted = true;
            log_line("ERROR", "input callback exception; neutral output on subsequent frames");
        }
        result = SCS_RESULT_not_found;
    }
    g.input_callback_stats.add(elapsed_us(started));
    return result;
}

SCSAPI_VOID channel_callback(scs_string_t, scs_u32_t, const scs_value_t* value,
                             scs_context_t context) {
    const auto started = qpc_ticks();
    try {
        auto* channel = static_cast<ChannelRuntime*>(context);
        if (channel) {
            channel->available = value != nullptr && value->type == channel->type;
            if (channel->available) {
                if (channel->type == SCS_VALUE_TYPE_float) channel->float_value = value->value_float.value;
                else if (channel->type == SCS_VALUE_TYPE_bool) channel->int_value = value->value_bool.value ? 1 : 0;
                else if (channel->type == SCS_VALUE_TYPE_s32) channel->int_value = value->value_s32.value;
            }
        }
    } catch (...) { /* Never propagate into the game. */ }
    g.channel_callback_stats.add(elapsed_us(started));
}

void write_frame() {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(9) << ++g.frame << ',' << now_ns() << ',' <<
        g.render_time << ',' << g.simulation_time << ',' << g.paused_simulation_time << ',' <<
        (g.paused ? 1 : 0);
    constexpr std::array<size_t, 13> order{0, 1, 5, 2, 6, 3, 7, 4, 8, 9, 10, 11, 12};
    for (const auto index : order) {
        out << ',';
        const auto& channel = g_channels[index];
        if (channel.available) {
            if (channel.type == SCS_VALUE_TYPE_float) out << channel.float_value;
            else out << channel.int_value;
        }
    }
    out << ',';
    if (g.injection_available) out << g.injection.steering;
    out << ',';
    if (g.injection_available) out << g.injection.throttle;
    out << ',';
    if (g.injection_available) out << g.injection.brake;
    out << ',';
    if (g.injection_available) out << (g.injection.test_button ? 1 : 0);
    out << ',';
    if (g.injection_available) {
        const auto current = now_ns();
        if (current >= g.injection_ns) out << (current - g.injection_ns) / 1000;
    }
    out << '\n';
    csv_add(g.frames, out.str(), "frames CSV");
}

SCSAPI_VOID telemetry_event_callback(scs_event_t event, const void* event_info,
                                      scs_context_t) {
    const auto started = qpc_ticks();
    try {
        switch (event) {
        case SCS_TELEMETRY_EVENT_frame_start:
            for (auto& channel : g_channels) channel.available = false;
            if (event_info) {
                const auto* frame = static_cast<const scs_telemetry_frame_start_t*>(event_info);
                g.render_time = frame->render_time;
                g.simulation_time = frame->simulation_time;
                g.paused_simulation_time = frame->paused_simulation_time;
            }
            break;
        case SCS_TELEMETRY_EVENT_frame_end:
            write_frame();
            g.stats_telemetry_frames++;
            g.frame_end_stats.add(elapsed_us(started));
            stats_line(false);
            break;
        case SCS_TELEMETRY_EVENT_paused:
            g.paused = true;
            log_line("INFO", "telemetry paused");
            break;
        case SCS_TELEMETRY_EVENT_started:
            g.paused = false;
            log_line("INFO", "telemetry started");
            break;
        case SCS_TELEMETRY_EVENT_configuration:
            if (event_info) {
                const auto* config = static_cast<const scs_telemetry_configuration_t*>(event_info);
                log_line("INFO", std::string("configuration id=") +
                    (config->id ? std::string(config->id, strnlen_s(config->id, 128)) : "<null>"));
            }
            break;
        default: break;
        }
    } catch (...) { /* Never propagate into the game. */ }
}

const scs_input_device_input_t kGenericInputs[] = {
    {"steering", "Spike steering", SCS_VALUE_TYPE_float},
    {"throttle", "Spike throttle", SCS_VALUE_TYPE_float},
    {"brake", "Spike brake", SCS_VALUE_TYPE_float},
    {"test_button", "Spike test button", SCS_VALUE_TYPE_bool}
};

scs_result_t register_input_device(const scs_input_init_params_v100_t& params,
                                   DeviceRuntime& runtime,
                                   const scs_input_device_input_t* inputs,
                                   scs_u32_t input_count) {
    scs_input_device_t device{};
    device.name = runtime.name;
    device.display_name = runtime.semantical ? "ETS2AP Spike Semantical" : "ETS2AP Spike";
    device.type = runtime.semantical ? SCS_INPUT_DEVICE_TYPE_semantical : SCS_INPUT_DEVICE_TYPE_generic;
    device.input_count = input_count;
    device.inputs = inputs;
    device.callback_context = &runtime;
    device.input_active_callback = input_active_callback;
    device.input_event_callback = input_event_callback;
    const auto result = params.register_device(&device);
    log_line("INFO", std::string("register_device name=") + runtime.name +
        " result=" + std::to_string(result));
    runtime.registered = result == SCS_RESULT_ok;
    runtime.input_count = input_count;
    return result;
}
} // namespace

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version,
                                 const scs_telemetry_init_params_t* const params) {
    try {
        if (version != SCS_TELEMETRY_VERSION_1_01 && version != SCS_TELEMETRY_VERSION_1_00)
            return SCS_RESULT_unsupported;
        if (!params) return SCS_RESULT_generic_error;
        const auto* init = static_cast<const scs_telemetry_init_params_v100_t*>(params);
        if (!init->register_for_event || !init->register_for_channel) return SCS_RESULT_generic_error;
        if (!g.telemetry_initialized && !g.input_initialized) open_session();
        const auto config = read_config();
        log_init("telemetry", version, init->common);
        log_config(config);
        constexpr std::array<scs_event_t, 5> events{
            SCS_TELEMETRY_EVENT_frame_start, SCS_TELEMETRY_EVENT_frame_end,
            SCS_TELEMETRY_EVENT_paused, SCS_TELEMETRY_EVENT_started,
            SCS_TELEMETRY_EVENT_configuration};
        for (auto event : events) {
            const auto result = init->register_for_event(event, telemetry_event_callback, nullptr);
            log_line("INFO", "register_event event=" + std::to_string(event) +
                " result=" + std::to_string(result));
        }
        for (auto& channel : g_channels) {
            const auto result = init->register_for_channel(channel.name, SCS_U32_NIL,
                channel.type, SCS_TELEMETRY_CHANNEL_FLAG_each_frame,
                channel_callback, &channel);
            log_line("INFO", std::string("register_channel name=") + channel.name +
                " result=" + std::to_string(result));
        }
        g.telemetry_initialized = true;
        return SCS_RESULT_ok;
    } catch (...) {
        if (!g.telemetry_initialized && !g.input_initialized) close_session();
        return SCS_RESULT_generic_error;
    }
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    try {
        g.telemetry_initialized = false;
        if (!g.input_initialized) {
            finish_input_row(g.generic);
            finish_input_row(g.semantical);
            close_session();
        }
    } catch (...) { /* Never propagate into the game. */ }
}

SCSAPI_RESULT scs_input_init(const scs_u32_t version,
                             const scs_input_init_params_t* const params) {
    try {
        if (version != SCS_INPUT_VERSION_1_00) return SCS_RESULT_unsupported;
        if (!params) return SCS_RESULT_generic_error;
        const auto* init = static_cast<const scs_input_init_params_v100_t*>(params);
        if (!init->register_device) return SCS_RESULT_generic_error;
        if (!g.telemetry_initialized && !g.input_initialized) open_session();
        g.input_faulted = false;
        g.config = read_config();
        log_init("input", version, init->common);
        log_config(g.config);
        int success = 0;
        if (g.config.devices == Devices::generic || g.config.devices == Devices::both) {
            if (register_input_device(*init, g.generic, kGenericInputs,
                static_cast<scs_u32_t>(std::size(kGenericInputs))) == SCS_RESULT_ok) ++success;
        }
        if (g.config.devices == Devices::semantical || g.config.devices == Devices::both) {
            if (g.config.semantical_inputs.empty()) {
                log_line("INFO", "semantical_inputs empty; device not registered");
            } else {
                std::vector<scs_input_device_input_t> inputs;
                inputs.reserve(g.config.semantical_inputs.size());
                for (const auto& item : g.config.semantical_inputs)
                    inputs.push_back({item.name.c_str(), item.name.c_str(), item.type});
                if (register_input_device(*init, g.semantical, inputs.data(),
                    static_cast<scs_u32_t>(inputs.size())) == SCS_RESULT_ok) ++success;
            }
        }
        if (g.config.devices != Devices::none && success == 0) {
            log_line("WARN", "all input device registrations failed");
            if (!g.telemetry_initialized) close_session();
            return SCS_RESULT_generic_error;
        }
        g.input_initialized = true;
        return SCS_RESULT_ok;
    } catch (...) {
        if (!g.telemetry_initialized && !g.input_initialized) close_session();
        return SCS_RESULT_generic_error;
    }
}

SCSAPI_VOID scs_input_shutdown(void) {
    try {
        finish_input_row(g.generic);
        finish_input_row(g.semantical);
        g.input_initialized = false;
        g.generic.registered = false;
        g.semantical.registered = false;
        g.injection_available = false;
        if (!g.telemetry_initialized) close_session();
    } catch (...) { /* Never propagate into the game. */ }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_module = module;
    if (reason == DLL_PROCESS_DETACH) g_module = nullptr;
    return TRUE;
}
