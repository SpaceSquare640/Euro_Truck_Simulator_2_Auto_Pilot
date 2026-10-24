// Edited by: OpenAI-Codex_Responsible: Source_Code
#pragma once

#include <ets2ap/telemetry/layout.h>
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ets2ap::telemetry {

struct MappingNames {
    std::wstring mapping_name{kMappingName};
    std::wstring mutex_name{kMutexName};
};

enum class MappingError {
    None, TokenUnavailable, SecuritySetupFailed, SecurityMismatch,
    MutexUnavailable, MappingUnavailable, ViewUnavailable, ViewTooSmall,
    ClockUnavailable, NotOpen, OutOfMemory
};

struct MappingResult {
    MappingError error{MappingError::None};
    DWORD win32_error{ERROR_SUCCESS};
    explicit operator bool() const noexcept { return error == MappingError::None; }
};

class TelemetryMapping {
public:
    explicit TelemetryMapping(MappingNames names = {});
    ~TelemetryMapping();
    TelemetryMapping(const TelemetryMapping&) = delete;
    TelemetryMapping& operator=(const TelemetryMapping&) = delete;
    MappingResult Create() noexcept;
    MappingResult Publish(const TelemetryV1& value) noexcept;
    void Close() noexcept;
private:
    MappingNames names_;
    HANDLE mutex_{nullptr};
    HANDLE mapping_{nullptr};
    void* view_{nullptr};
    bool owns_mutex_{false};
    bool registered_{false};
};

class ReadOnlyMapping {
public:
    explicit ReadOnlyMapping(MappingNames names = {});
    ~ReadOnlyMapping();
    ReadOnlyMapping(const ReadOnlyMapping&) = delete;
    ReadOnlyMapping& operator=(const ReadOnlyMapping&) = delete;
    MappingResult OpenReadOnly() noexcept;
    void Close() noexcept;
    const void* data() const noexcept { return view_; }
    std::size_t size() const noexcept { return size_; }
private:
    MappingNames names_;
    HANDLE mapping_{nullptr};
    const void* view_{nullptr};
    std::size_t size_{0};
};

} // namespace ets2ap::telemetry
