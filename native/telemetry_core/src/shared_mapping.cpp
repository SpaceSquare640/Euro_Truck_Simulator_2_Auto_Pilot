// Edited by: OpenAI-Codex_Responsible: Source_Code
#include <ets2ap/telemetry/shared_mapping.h>
#include <ets2ap/telemetry/convert.h>
#include <ets2ap/telemetry/seqlock.h>
#include <Aclapi.h>
#include <sddl.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ets2ap::telemetry {
namespace {

std::mutex owner_registry_mutex;
std::unordered_set<std::wstring> owner_registry;

MappingResult Error(MappingError error, DWORD code = GetLastError()) noexcept {
    return {error, code};
}

struct LocalDescriptor {
    PSECURITY_DESCRIPTOR value{nullptr};
    ~LocalDescriptor() { if (value) LocalFree(value); }
};

bool CurrentSid(std::vector<std::byte>& sid, DWORD& error) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        error = GetLastError();
        return false;
    }
    DWORD required = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &required);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !required) {
        error = GetLastError();
        CloseHandle(token);
        return false;
    }
    std::vector<std::byte> token_info(required);
    if (!GetTokenInformation(token, TokenUser, token_info.data(), required, &required)) {
        error = GetLastError();
        CloseHandle(token);
        return false;
    }
    auto* user = reinterpret_cast<TOKEN_USER*>(token_info.data());
    sid.resize(GetLengthSid(user->User.Sid));
    const bool copied = CopySid(static_cast<DWORD>(sid.size()), sid.data(), user->User.Sid) != FALSE;
    error = copied ? ERROR_SUCCESS : GetLastError();
    CloseHandle(token);
    return copied;
}

bool MakeDescriptor(const std::vector<std::byte>& sid, LocalDescriptor& descriptor,
                    DWORD& error) {
    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(const_cast<std::byte*>(sid.data()), &sid_text)) {
        error = GetLastError();
        return false;
    }
    const std::wstring text = L"O:" + std::wstring(sid_text) +
        L"D:P(A;;GA;;;" + std::wstring(sid_text) + L")";
    LocalFree(sid_text);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(),
            SDDL_REVISION_1, &descriptor.value, nullptr)) {
        error = GetLastError();
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

bool SecurityMatches(HANDLE handle, const std::vector<std::byte>& sid,
                     DWORD& error) noexcept {
    PSID owner = nullptr;
    PACL dacl = nullptr;
    LocalDescriptor descriptor;
    error = GetSecurityInfo(handle, SE_KERNEL_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &descriptor.value);
    if (error != ERROR_SUCCESS) return false;
    if (!owner || EqualSid(owner, const_cast<std::byte*>(sid.data())) == FALSE ||
        !dacl || dacl->AceCount == 0) return false;
    for (DWORD i = 0; i < dacl->AceCount; ++i) {
        void* raw = nullptr;
        if (!GetAce(dacl, i, &raw) || !raw) return false;
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
            EqualSid(reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart)),
                     const_cast<std::byte*>(sid.data())) == FALSE) return false;
    }
    return true;
}

std::uint64_t CurrentTimestamp() noexcept {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&frequency)) return 0;
    return QpcTicksToNs(counter.QuadPart, frequency.QuadPart).value_or(0);
}

} // namespace

TelemetryMapping::TelemetryMapping(MappingNames names) : names_(std::move(names)) {}
TelemetryMapping::~TelemetryMapping() { Close(); }

MappingResult TelemetryMapping::Create() noexcept try {
    if (view_) return {};
    {
        std::lock_guard lock(owner_registry_mutex);
        // Win32 mutexes are recursive on one thread; reject a second local owner.
        if (!owner_registry.insert(names_.mutex_name).second)
            return Error(MappingError::MutexUnavailable, WAIT_TIMEOUT);
        registered_ = true;
    }
    DWORD code = ERROR_SUCCESS;
    std::vector<std::byte> sid;
    if (!CurrentSid(sid, code)) {
        Close();
        return Error(MappingError::TokenUnavailable, code);
    }
    LocalDescriptor descriptor;
    if (!MakeDescriptor(sid, descriptor, code)) {
        Close();
        return Error(MappingError::SecuritySetupFailed, code);
    }
    SECURITY_ATTRIBUTES attrs{sizeof(attrs), descriptor.value, FALSE};
    mutex_ = CreateMutexW(&attrs, FALSE, names_.mutex_name.c_str());
    if (!mutex_) {
        code = GetLastError();
        Close();
        return Error(MappingError::MutexUnavailable, code);
    }
    const bool existing_mutex = GetLastError() == ERROR_ALREADY_EXISTS;
    if (existing_mutex && !SecurityMatches(mutex_, sid, code)) {
        Close();
        return Error(MappingError::SecurityMismatch, code);
    }
    const DWORD wait = WaitForSingleObject(mutex_, 0);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        code = wait == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError();
        Close();
        return Error(MappingError::MutexUnavailable, code);
    }
    owns_mutex_ = true;
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &attrs, PAGE_READWRITE,
                                  0, static_cast<DWORD>(kMappingSize), names_.mapping_name.c_str());
    if (!mapping_) {
        code = GetLastError();
        Close();
        return Error(MappingError::MappingUnavailable, code);
    }
    const bool existing_mapping = GetLastError() == ERROR_ALREADY_EXISTS;
    if (existing_mapping && !SecurityMatches(mapping_, sid, code)) {
        Close();
        return Error(MappingError::SecurityMismatch, code);
    }
    view_ = MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, kMappingSize);
    if (!view_) {
        code = GetLastError();
        Close();
        return Error(MappingError::ViewUnavailable, code);
    }
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(view_, &info, sizeof(info)) || info.RegionSize < kMappingSize) {
        code = GetLastError();
        Close();
        return Error(MappingError::ViewTooSmall, code);
    }
    const auto timestamp = CurrentTimestamp();
    if (!timestamp) {
        Close();
        return Error(MappingError::ClockUnavailable, ERROR_INVALID_DATA);
    }
    SeqlockWriter writer(view_, kMappingSize);
    if (existing_mapping) {
        if (!writer.ResetPublish(timestamp)) {
            Close();
            return Error(MappingError::ViewUnavailable, ERROR_INVALID_DATA);
        }
    } else {
        // A newly created pagefile mapping is zero-filled; sequence remains zero.
        TelemetryV1 initial{};
        initial.magic = kMagic;
        initial.version_major = kVersionMajor;
        initial.version_minor = kVersionMinor;
        initial.struct_size = kStructSize;
        initial.timestamp_ns = timestamp;
        auto* bytes = static_cast<std::byte*>(view_);
        auto* source = reinterpret_cast<const std::byte*>(&initial);
        for (std::size_t i = 0; i < kStructSize / 8; ++i) {
            if (i == offsetof(TelemetryV1, sequence) / 8) continue;
            std::uint64_t word = 0;
            std::memcpy(&word, source + i * 8, sizeof(word));
            std::atomic_ref<std::uint64_t>(*reinterpret_cast<std::uint64_t*>(bytes + i * 8))
                .store(word, std::memory_order_relaxed);
        }
    }
    return {};
} catch (const std::bad_alloc&) {
    Close();
    return Error(MappingError::OutOfMemory, ERROR_NOT_ENOUGH_MEMORY);
} catch (...) {
    Close();
    return Error(MappingError::SecuritySetupFailed, ERROR_INVALID_DATA);
}

MappingResult TelemetryMapping::Publish(const TelemetryV1& value) noexcept {
    if (!view_ || !owns_mutex_) return Error(MappingError::NotOpen, ERROR_INVALID_HANDLE);
    return SeqlockWriter(view_, kMappingSize).Publish(value) ? MappingResult{} :
        Error(MappingError::ViewUnavailable, ERROR_INVALID_DATA);
}

void TelemetryMapping::Close() noexcept {
    if (view_) { UnmapViewOfFile(view_); view_ = nullptr; }
    if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
    if (mutex_) {
        if (owns_mutex_) ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
    owns_mutex_ = false;
    if (registered_) {
        std::lock_guard lock(owner_registry_mutex);
        owner_registry.erase(names_.mutex_name);
        registered_ = false;
    }
}

ReadOnlyMapping::ReadOnlyMapping(MappingNames names) : names_(std::move(names)) {}
ReadOnlyMapping::~ReadOnlyMapping() { Close(); }

MappingResult ReadOnlyMapping::OpenReadOnly() noexcept {
    if (view_) return {};
    mapping_ = OpenFileMappingW(FILE_MAP_READ, FALSE, names_.mapping_name.c_str());
    if (!mapping_) return Error(MappingError::MappingUnavailable);
    view_ = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, kMappingSize);
    if (!view_) {
        const auto code = GetLastError();
        Close();
        return Error(MappingError::ViewUnavailable, code);
    }
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(view_, &info, sizeof(info)) || info.RegionSize < kMappingSize) {
        const auto code = GetLastError();
        Close();
        return Error(MappingError::ViewTooSmall, code);
    }
    size_ = kMappingSize;
    return {};
}

void ReadOnlyMapping::Close() noexcept {
    if (view_) { UnmapViewOfFile(view_); view_ = nullptr; }
    if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
    size_ = 0;
}
} // namespace ets2ap::telemetry
