// Edited by: OpenAI-Codex_Responsible: Source_Code
#include <ets2ap/telemetry/shared_mapping.h>
#include <ets2ap/telemetry/seqlock.h>
#include <ets2ap/test/test_framework.h>

#include <Windows.h>
#include <Aclapi.h>
#include <sddl.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace ets2ap::telemetry;

std::atomic<unsigned> next_name{0};

MappingNames UniqueNames() {
    const std::wstring suffix = std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::to_wstring(++next_name);
    return {L"Local\\ETS2AP_Test_Mapping_" + suffix,
            L"Local\\ETS2AP_Test_Mutex_" + suffix};
}

TelemetryV1 Sample() {
    TelemetryV1 value{};
    value.magic = kMagic;
    value.version_major = kVersionMajor;
    value.version_minor = kVersionMinor;
    value.struct_size = kStructSize;
    value.timestamp_ns = 12345;
    value.validity_flags = 1u << 1;
    value.speed_mps = 42;
    return value;
}

std::vector<std::byte> CurrentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }
    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    std::vector<std::byte> buffer(needed);
    const BOOL ok = GetTokenInformation(token, TokenUser, buffer.data(), needed, &needed);
    std::vector<std::byte> sid;
    if (ok) {
        const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
        sid.resize(GetLengthSid(user->User.Sid));
        if (!CopySid(static_cast<DWORD>(sid.size()), sid.data(), user->User.Sid)) {
            sid.clear();
        }
    }
    CloseHandle(token);
    return sid;
}

}  // namespace

ETS2AP_TEST(MappingRoundTripWithIndependentReadOnlyView) {
    const auto names = UniqueNames();
    TelemetryMapping writer{names};
    CHECK(writer.Create());
    CHECK(writer.Publish(Sample()));
    ReadOnlyMapping reader{names};
    CHECK(reader.OpenReadOnly());
    TelemetryV1 value{};
    CHECK_EQ(TryReadSnapshot(reader.data(), reader.size(), &value), ReadStatus::Ok);
    CHECK_EQ(value.speed_mps, 42.0);
    CHECK_EQ(value.timestamp_ns, 12345ull);
}

ETS2AP_TEST(MappingRejectsSecondWriter) {
    const auto names = UniqueNames();
    TelemetryMapping first{names};
    TelemetryMapping second{names};
    CHECK(first.Create());
    const MappingResult result = second.Create();
    CHECK_EQ(result.error, MappingError::MutexUnavailable);
}

ETS2AP_TEST(MappingDaclOnlyCurrentSid) {
    const auto names = UniqueNames();
    TelemetryMapping writer{names};
    CHECK(writer.Create());
    const auto sid = CurrentUserSid();
    CHECK(!sid.empty());
    HANDLE handle = OpenFileMappingW(READ_CONTROL, FALSE, names.mapping_name.c_str());
    CHECK(handle != nullptr);
    if (!handle || sid.empty()) {
        if (handle) CloseHandle(handle);
        return;
    }
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD code = GetSecurityInfo(handle, SE_KERNEL_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &descriptor);
    CHECK_EQ(code, static_cast<DWORD>(ERROR_SUCCESS));
    if (code == ERROR_SUCCESS) {
        CHECK(EqualSid(owner, const_cast<std::byte*>(sid.data())) != FALSE);
        CHECK(dacl != nullptr);
        if (dacl) {
            CHECK_EQ(dacl->AceCount, 1u);
            if (dacl->AceCount == 1) {
                void* raw_ace = nullptr;
                CHECK(GetAce(dacl, 0, &raw_ace) != FALSE);
                if (raw_ace) {
                    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
                    CHECK_EQ(ace->Header.AceType, ACCESS_ALLOWED_ACE_TYPE);
                    CHECK(EqualSid(reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart)),
                                   const_cast<std::byte*>(sid.data())) != FALSE);
                }
            }
        }
    }
    if (descriptor) LocalFree(descriptor);
    CloseHandle(handle);
}

ETS2AP_TEST(MappingReadOnlyHandleCannotMapWritableView) {
    const auto names = UniqueNames();
    TelemetryMapping writer{names};
    CHECK(writer.Create());
    HANDLE read_handle = OpenFileMappingW(FILE_MAP_READ, FALSE, names.mapping_name.c_str());
    CHECK(read_handle != nullptr);
    if (read_handle) {
        void* writable = MapViewOfFile(read_handle, FILE_MAP_WRITE, 0, 0, kMappingSize);
        CHECK(writable == nullptr);
        CHECK_EQ(GetLastError(), ERROR_ACCESS_DENIED);
        if (writable) UnmapViewOfFile(writable);
        CloseHandle(read_handle);
    }
}

ETS2AP_TEST(MappingOpenReadOnlyDoesNotCreateMissingObject) {
    const auto names = UniqueNames();
    ReadOnlyMapping reader{names};
    CHECK_EQ(reader.OpenReadOnly().error, MappingError::MappingUnavailable);
    HANDLE handle = OpenFileMappingW(FILE_MAP_READ, FALSE, names.mapping_name.c_str());
    CHECK(handle == nullptr);
    CHECK_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
    if (handle) CloseHandle(handle);
}

ETS2AP_TEST(MappingRecoversAbandonedOddSequence) {
    const auto names = UniqueNames();
    TelemetryMapping first{names};
    CHECK(first.Create());
    ReadOnlyMapping reader{names};
    CHECK(reader.OpenReadOnly());
    HANDLE handle = OpenFileMappingW(FILE_MAP_WRITE, FALSE, names.mapping_name.c_str());
    CHECK(handle != nullptr);
    if (!handle) return;
    void* view = MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, kMappingSize);
    CHECK(view != nullptr);
    if (!view) {
        CloseHandle(handle);
        return;
    }
    first.Close();
    auto* words = static_cast<std::uint64_t*>(view);
    words[2] = 9;
    auto* bytes = static_cast<std::byte*>(view);
    bytes[kMappingSize - 1] = std::byte{0xA5};
    TelemetryMapping recovered{names};
    CHECK(recovered.Create());
    TelemetryV1 value{};
    CHECK_EQ(TryReadSnapshot(reader.data(), reader.size(), &value), ReadStatus::Ok);
    CHECK_EQ(value.sequence % 2, 0ull);
    CHECK_EQ(value.validity_flags, 0u);
    CHECK_EQ(value.speed_mps, 0.0);
    CHECK(value.timestamp_ns > 0);
    CHECK_EQ(std::to_integer<unsigned>(bytes[kMappingSize - 1]), 0u);
    UnmapViewOfFile(view);
    CloseHandle(handle);
}

ETS2AP_TEST(MappingRejectsForeignDacl) {
    const auto names = UniqueNames();
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const BOOL converted = ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;;GA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr);
    CHECK(converted != FALSE);
    if (!converted) return;
    SECURITY_ATTRIBUTES attrs{sizeof(attrs), descriptor, FALSE};
    HANDLE foreign = CreateFileMappingW(INVALID_HANDLE_VALUE, &attrs, PAGE_READWRITE,
        0, kMappingSize, names.mapping_name.c_str());
    CHECK(foreign != nullptr);
    if (foreign) {
        TelemetryMapping writer{names};
        CHECK_EQ(writer.Create().error, MappingError::SecurityMismatch);
        CloseHandle(foreign);
    }
    LocalFree(descriptor);
}

ETS2AP_TEST(MappingReadOnlyViewSpansFullMapping) {
    const auto names = UniqueNames();
    TelemetryMapping writer{names};
    CHECK(writer.Create());
    ReadOnlyMapping reader{names};
    CHECK(reader.OpenReadOnly());
    CHECK(reader.size() >= kMappingSize);
}

ETS2AP_TEST(MappingCloseAndDestructorReleaseOwnership) {
    const auto names = UniqueNames();
    {
        TelemetryMapping first{names};
        CHECK(first.Create());
        first.Close();
        first.Close();
        CHECK(first.Create());
    }
    TelemetryMapping replacement{names};
    CHECK(replacement.Create());
}

ETS2AP_TEST_MAIN()
