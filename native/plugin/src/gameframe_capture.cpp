// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/plugin/gameframe_capture.h"

#include <Aclapi.h>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <sddl.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ets2ap::plugin {
namespace {

constexpr wchar_t kGameImageName[] = L"eurotrucks2.exe";
constexpr wchar_t kProbeClassName[] = L"ETS2AP_GameFrameProbe_v1";
constexpr wchar_t kMappingName[] = L"Local\\ETS2AP_GameFrame_v1";
constexpr wchar_t kWriterSemaphoreName[] = L"Local\\ETS2AP_GameFrame_v1.writer";
constexpr std::uint16_t kVersionMajor = 1;
constexpr std::uint16_t kVersionMinor = 0;
constexpr std::uint32_t kHeaderSize = 64;
constexpr std::uint32_t kFrameValid = 1U;
constexpr std::uint16_t kWidth = 160;
constexpr std::uint16_t kHeight = 90;
constexpr std::uint32_t kPixelFormatRgb24 = 1U;
constexpr std::uint32_t kPixelBytes = static_cast<std::uint32_t>(kWidth) * kHeight * 3U;
constexpr std::size_t kPacketSize = kHeaderSize + kPixelBytes;
constexpr std::size_t kPresentSlot = 8;
constexpr std::size_t kReadbackCount = 2;
constexpr std::uint64_t kCaptureIntervalNs = 50'000'000ULL;

#pragma pack(push, 8)
struct GameFrameHeader {
    char magic[4];
    std::uint16_t version_major;
    std::uint16_t version_minor;
    std::uint32_t header_size;
    std::uint32_t flags;
    std::uint64_t sequence;
    std::uint64_t timestamp_ns;
    std::uint16_t width;
    std::uint16_t height;
    std::uint32_t pixel_format;
    std::uint32_t payload_size;
    std::uint8_t reserved[20];
};

struct GameFramePacket {
    GameFrameHeader header;
    std::array<std::uint8_t, kPixelBytes> pixels;
};
#pragma pack(pop)

static_assert(sizeof(GameFrameHeader) == kHeaderSize);
static_assert(offsetof(GameFrameHeader, sequence) == 16);
static_assert(offsetof(GameFrameHeader, timestamp_ns) == 24);
static_assert(sizeof(GameFramePacket) == kPacketSize);

using PresentFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
std::atomic<PresentFunction> g_original_present{nullptr};
std::atomic<void*> g_capture_owner{nullptr};
std::atomic_bool g_mapping_owner_claimed{false};

struct LocalSecurityDescriptor {
    PSECURITY_DESCRIPTOR value{nullptr};
    ~LocalSecurityDescriptor() { if (value) LocalFree(value); }
};

bool CurrentUserSid(std::vector<std::byte>& sid, DWORD& error) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        error = GetLastError();
        return false;
    }
    DWORD required = 0;
    (void)GetTokenInformation(token, TokenUser, nullptr, 0, &required);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0) {
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
    const auto* user = reinterpret_cast<const TOKEN_USER*>(token_info.data());
    const DWORD sid_size = GetLengthSid(user->User.Sid);
    sid.resize(sid_size);
    const bool copied = CopySid(sid_size, sid.data(), user->User.Sid) != FALSE;
    error = copied ? ERROR_SUCCESS : GetLastError();
    CloseHandle(token);
    return copied;
}

bool MakeUserDescriptor(const std::vector<std::byte>& sid,
                        LocalSecurityDescriptor& descriptor, DWORD& error) {
    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(const_cast<std::byte*>(sid.data()), &sid_text)) {
        error = GetLastError();
        return false;
    }
    const std::wstring sddl = L"O:" + std::wstring(sid_text) +
        L"D:P(A;;GA;;;" + std::wstring(sid_text) + L")";
    LocalFree(sid_text);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),
            SDDL_REVISION_1, &descriptor.value, nullptr)) {
        error = GetLastError();
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

bool SecurityMatchesCurrentUser(HANDLE handle, const std::vector<std::byte>& sid,
                                DWORD& error) noexcept {
    PSID owner = nullptr;
    PACL dacl = nullptr;
    LocalSecurityDescriptor descriptor;
    error = GetSecurityInfo(handle, SE_KERNEL_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &descriptor.value);
    if (error != ERROR_SUCCESS) return false;
    if (!owner || EqualSid(owner, const_cast<std::byte*>(sid.data())) == FALSE ||
        !dacl || dacl->AceCount != 1) return false;
    void* raw_ace = nullptr;
    if (!GetAce(dacl, 0, &raw_ace) || !raw_ace) return false;
    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
    return ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
        EqualSid(reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart)),
                 const_cast<std::byte*>(sid.data())) != FALSE;
}

std::uint64_t MonotonicNanoseconds() noexcept {
    LARGE_INTEGER ticks{};
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceCounter(&ticks) || !QueryPerformanceFrequency(&frequency) ||
        frequency.QuadPart <= 0 || ticks.QuadPart < 0) return 0;
    const auto whole_seconds = static_cast<std::uint64_t>(ticks.QuadPart / frequency.QuadPart);
    const auto remainder = static_cast<std::uint64_t>(ticks.QuadPart % frequency.QuadPart);
    return whole_seconds * 1'000'000'000ULL +
        remainder * 1'000'000'000ULL / static_cast<std::uint64_t>(frequency.QuadPart);
}

class GameFramePublisher {
public:
    bool Open() noexcept try {
        bool expected = false;
        if (!g_mapping_owner_claimed.compare_exchange_strong(expected, true)) return false;
        owns_registry_claim_ = true;

        DWORD error = ERROR_SUCCESS;
        std::vector<std::byte> sid;
        if (!CurrentUserSid(sid, error)) { Close(); return false; }
        LocalSecurityDescriptor descriptor;
        if (!MakeUserDescriptor(sid, descriptor, error)) { Close(); return false; }
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor.value, FALSE};

        SetLastError(ERROR_SUCCESS);
        writer_semaphore_ = CreateSemaphoreW(&attributes, 1, 1, kWriterSemaphoreName);
        if (!writer_semaphore_) { Close(); return false; }
        const bool existing_semaphore = GetLastError() == ERROR_ALREADY_EXISTS;
        if (existing_semaphore &&
            !SecurityMatchesCurrentUser(writer_semaphore_, sid, error)) {
            Close();
            return false;
        }
        if (WaitForSingleObject(writer_semaphore_, 0) != WAIT_OBJECT_0) {
            Close();
            return false;
        }
        owns_semaphore_ = true;

        SetLastError(ERROR_SUCCESS);
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE,
            0, static_cast<DWORD>(kPacketSize), kMappingName);
        if (!mapping_) { Close(); return false; }
        const bool existing_mapping = GetLastError() == ERROR_ALREADY_EXISTS;
        if (existing_mapping && !SecurityMatchesCurrentUser(mapping_, sid, error)) {
            Close();
            return false;
        }
        view_ = MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, kPacketSize);
        if (!view_) { Close(); return false; }
        MEMORY_BASIC_INFORMATION information{};
        if (!VirtualQuery(view_, &information, sizeof(information)) ||
            information.RegionSize < kPacketSize) {
            Close();
            return false;
        }
        std::array<std::uint8_t, kPixelBytes> blank{};
        return Publish(blank.data(), MonotonicNanoseconds(), false);
    } catch (...) {
        Close();
        return false;
    }

    bool Publish(const std::uint8_t* pixels, std::uint64_t timestamp_ns,
                 bool valid) noexcept {
        if (!view_ || timestamp_ns == 0 || (valid && !pixels)) return false;
        GameFramePacket packet{};
        std::memcpy(packet.header.magic, "E2GF", 4);
        packet.header.version_major = kVersionMajor;
        packet.header.version_minor = kVersionMinor;
        packet.header.header_size = kHeaderSize;
        packet.header.flags = valid ? kFrameValid : 0U;
        packet.header.timestamp_ns = timestamp_ns;
        packet.header.width = kWidth;
        packet.header.height = kHeight;
        packet.header.pixel_format = kPixelFormatRgb24;
        packet.header.payload_size = kPixelBytes;
        if (valid) std::memcpy(packet.pixels.data(), pixels, kPixelBytes);

        auto* bytes = static_cast<std::byte*>(view_);
        auto* sequence_address = reinterpret_cast<std::uint64_t*>(bytes +
            offsetof(GameFrameHeader, sequence));
        std::atomic_ref<std::uint64_t> sequence(*sequence_address);
        std::uint64_t even = (sequence.load(std::memory_order_relaxed) & ~1ULL) + 2ULL;
        if (even == 0) even = 2;
        packet.header.sequence = even;
        sequence.store(even - 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(bytes, &packet, offsetof(GameFrameHeader, sequence));
        std::memcpy(bytes + offsetof(GameFrameHeader, timestamp_ns),
            reinterpret_cast<const std::byte*>(&packet) +
                offsetof(GameFrameHeader, timestamp_ns),
            kPacketSize - offsetof(GameFrameHeader, timestamp_ns));
        std::atomic_thread_fence(std::memory_order_release);
        sequence.store(even, std::memory_order_release);
        return true;
    }

    void Close() noexcept {
        if (view_) { UnmapViewOfFile(view_); view_ = nullptr; }
        if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
        if (writer_semaphore_) {
            if (owns_semaphore_) (void)ReleaseSemaphore(writer_semaphore_, 1, nullptr);
            CloseHandle(writer_semaphore_);
            writer_semaphore_ = nullptr;
        }
        owns_semaphore_ = false;
        if (owns_registry_claim_) {
            g_mapping_owner_claimed.store(false, std::memory_order_release);
            owns_registry_claim_ = false;
        }
    }

    ~GameFramePublisher() { Close(); }

private:
    HANDLE writer_semaphore_{nullptr};
    HANDLE mapping_{nullptr};
    void* view_{nullptr};
    bool owns_semaphore_{false};
    bool owns_registry_claim_{false};
};

struct ReadbackSlot {
    ID3D11Texture2D* staging{nullptr};
    ID3D11Texture2D* resolved{nullptr};
    ID3D11Query* completion{nullptr};
    std::uint64_t timestamp_ns{0};
    bool pending{false};
};

template <typename Interface>
void Release(Interface*& value) noexcept {
    if (value) { value->Release(); value = nullptr; }
}

class CaptureState {
public:
    bool Start() noexcept try {
        if (active_.load(std::memory_order_acquire)) return true;
        if (hook_installed_ || hook_conflicted_) return false;
        if (!IsEuroTruckSimulatorProcess()) return false;
        if (!publisher_.Open() || !PinModuleForProcessLifetime() ||
            !CreateProbeSwapChain() || !InstallPresentHook()) {
            Stop();
            return false;
        }
        active_.store(true, std::memory_order_release);
        return true;
    } catch (...) {
        Stop();
        return false;
    }

    void Stop() noexcept {
        active_.store(false, std::memory_order_release);
        RemovePresentHook();
        std::lock_guard lock(capture_mutex_);
        ReleaseDeviceResources();
        target_window_ = nullptr;
        // Keep the hidden probe HWND and swap chain for this process lifetime.
        // The HWND belongs to the thread that created it, which may differ from shutdown.
        publisher_.Close();
        next_capture_ns_ = 0;
        resource_retry_after_ns_ = 0;
        last_published_timestamp_ns_ = 0;
    }

    void OnPresent(IDXGISwapChain* chain) noexcept {
        if (!active_.load(std::memory_order_acquire) || !chain) return;
        try {
            std::unique_lock<std::mutex> lock(capture_mutex_, std::try_to_lock);
            if (!lock.owns_lock() || !active_.load(std::memory_order_acquire) ||
                !IsGameWindowSwapChain(chain)) return;
            Capture(chain);
        } catch (...) {}
    }

    static HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* chain,
                                                 UINT interval, UINT flags) noexcept {
        auto* owner = static_cast<CaptureState*>(g_capture_owner.load(std::memory_order_acquire));
        if (owner) owner->OnPresent(chain);
        const PresentFunction original = g_original_present.load(std::memory_order_acquire);
        return original ? original(chain, interval, flags) : E_FAIL;
    }

private:
    static LRESULT CALLBACK ProbeWindowProc(HWND window, UINT message,
                                             WPARAM wparam, LPARAM lparam) noexcept {
        return DefWindowProcW(window, message, wparam, lparam);
    }

    bool PinModuleForProcessLifetime() noexcept {
        if (self_module_) return true;
        const auto address = reinterpret_cast<LPCWSTR>(
            reinterpret_cast<void*>(PresentHook));
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_PIN,
            address, &self_module_) != FALSE;
    }

    static bool IsEuroTruckSimulatorProcess() noexcept {
        std::array<wchar_t, 32768> path{};
        const DWORD length = GetModuleFileNameW(nullptr, path.data(),
            static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size()) return false;
        const wchar_t* base = wcsrchr(path.data(), L'\\');
        base = base ? base + 1 : path.data();
        return _wcsicmp(base, kGameImageName) == 0;
    }

    bool CreateProbeSwapChain() noexcept {
        if (probe_chain_) return probe_window_ && IsWindow(probe_window_) != FALSE;
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        if (!probe_window_) {
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = ProbeWindowProc;
            window_class.hInstance = instance;
            window_class.lpszClassName = kProbeClassName;
            SetLastError(ERROR_SUCCESS);
            const ATOM atom = RegisterClassExW(&window_class);
            if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
            probe_window_ = CreateWindowExW(0, kProbeClassName, L"", WS_POPUP,
                0, 0, 8, 8, nullptr, nullptr, instance, nullptr);
        }
        if (!probe_window_) return false;
        Release(probe_device_);
        Release(probe_context_);

        DXGI_SWAP_CHAIN_DESC description{};
        description.BufferDesc.Width = 8;
        description.BufferDesc.Height = 8;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 1;
        description.OutputWindow = probe_window_;
        description.Windowed = TRUE;
        description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr std::array<D3D_FEATURE_LEVEL, 3> levels{
            D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL selected{};
        const HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP,
            nullptr, 0, levels.data(), static_cast<UINT>(levels.size()), D3D11_SDK_VERSION,
            &description, &probe_chain_, &probe_device_, &selected, &probe_context_);
        return SUCCEEDED(result) && probe_chain_ != nullptr;
    }

    bool InstallPresentHook() noexcept {
        if (!probe_chain_) return false;
        auto** vtable = *reinterpret_cast<void***>(probe_chain_);
        void** entry = &vtable[kPresentSlot];
        auto* slot = reinterpret_cast<void* volatile*>(entry);
        DWORD old_protection = 0;
        if (!VirtualProtect(entry, sizeof(void*), PAGE_EXECUTE_READWRITE,
                &old_protection)) return false;

        void* current = InterlockedCompareExchangePointer(slot, nullptr, nullptr);
        if (!current || current == reinterpret_cast<void*>(PresentHook)) {
            DWORD ignored = 0;
            (void)VirtualProtect(entry, sizeof(void*), old_protection, &ignored);
            return false;
        }
        void* empty = nullptr;
        if (!g_capture_owner.compare_exchange_strong(empty, this,
                std::memory_order_acq_rel)) {
            DWORD ignored = 0;
            (void)VirtualProtect(entry, sizeof(void*), old_protection, &ignored);
            return false;
        }
        g_original_present.store(reinterpret_cast<PresentFunction>(current),
            std::memory_order_release);
        void* previous = InterlockedCompareExchangePointer(slot,
            reinterpret_cast<void*>(PresentHook), current);
        const bool installed = previous == current;
        if (installed) {
            present_vtable_ = vtable;
            hook_installed_ = true;
        } else {
            g_capture_owner.store(nullptr, std::memory_order_release);
        }
        DWORD ignored = 0;
        (void)VirtualProtect(entry, sizeof(void*), old_protection, &ignored);
        return installed;
    }

    void RemovePresentHook() noexcept {
        if (hook_installed_ && present_vtable_) {
            void** entry = &present_vtable_[kPresentSlot];
            auto* slot = reinterpret_cast<void* volatile*>(entry);
            DWORD old_protection = 0;
            if (VirtualProtect(entry, sizeof(void*), PAGE_EXECUTE_READWRITE,
                    &old_protection)) {
                const PresentFunction original = g_original_present.load(std::memory_order_acquire);
                void* current = InterlockedCompareExchangePointer(slot,
                    reinterpret_cast<void*>(original), reinterpret_cast<void*>(PresentHook));
                if (current != reinterpret_cast<void*>(PresentHook) &&
                    current != reinterpret_cast<void*>(original)) hook_conflicted_ = true;
                DWORD ignored = 0;
                (void)VirtualProtect(entry, sizeof(void*), old_protection, &ignored);
            } else hook_conflicted_ = true;
            hook_installed_ = false;
            present_vtable_ = nullptr;
        }
        void* expected = this;
        (void)g_capture_owner.compare_exchange_strong(expected, nullptr,
            std::memory_order_acq_rel);
    }

    bool IsGameWindowSwapChain(IDXGISwapChain* chain) noexcept {
        DXGI_SWAP_CHAIN_DESC description{};
        if (FAILED(chain->GetDesc(&description)) || !description.OutputWindow ||
            !IsWindowVisible(description.OutputWindow)) return false;
        DWORD process_id = 0;
        (void)GetWindowThreadProcessId(description.OutputWindow, &process_id);
        if (process_id != GetCurrentProcessId()) return false;
        const HWND root = GetAncestor(description.OutputWindow, GA_ROOT);
        if (root != description.OutputWindow || description.BufferDesc.Width < 640 ||
            description.BufferDesc.Height < 360) return false;
        if (target_window_ && !IsWindow(target_window_)) target_window_ = nullptr;
        if (!target_window_) {
            if (GetForegroundWindow() != root) return false;
            target_window_ = root;
        }
        return target_window_ == root;
    }

    static bool IsSupportedFormat(DXGI_FORMAT format) noexcept {
        return format == DXGI_FORMAT_B8G8R8A8_UNORM ||
            format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }

    void ReleaseDeviceResources() noexcept {
        for (auto& slot : slots_) {
            Release(slot.completion);
            Release(slot.resolved);
            Release(slot.staging);
            slot.timestamp_ns = 0;
            slot.pending = false;
        }
        Release(context_);
        Release(device_);
        width_ = 0;
        height_ = 0;
        sample_count_ = 0;
        format_ = DXGI_FORMAT_UNKNOWN;
        next_capture_ns_ = 0;
        resource_retry_after_ns_ = 0;
    }

    bool EnsureDeviceResources(IDXGISwapChain* chain,
                               const D3D11_TEXTURE2D_DESC& back_desc) noexcept {
        if (!IsSupportedFormat(back_desc.Format) || back_desc.Width == 0 ||
            back_desc.Height == 0 || back_desc.ArraySize != 1 ||
            back_desc.SampleDesc.Count == 0) return false;
        ID3D11Device* candidate = nullptr;
        if (FAILED(chain->GetDevice(__uuidof(ID3D11Device),
                reinterpret_cast<void**>(&candidate))) || !candidate) return false;
        const bool unchanged = candidate == device_ && width_ == back_desc.Width &&
            height_ == back_desc.Height && format_ == back_desc.Format &&
            sample_count_ == back_desc.SampleDesc.Count;
        if (unchanged) { candidate->Release(); return true; }

        ReleaseDeviceResources();
        device_ = candidate;
        device_->GetImmediateContext(&context_);
        if (!context_) { ReleaseDeviceResources(); return false; }
        width_ = back_desc.Width;
        height_ = back_desc.Height;
        format_ = back_desc.Format;
        sample_count_ = back_desc.SampleDesc.Count;

        D3D11_TEXTURE2D_DESC staging_desc = back_desc;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging_desc.MiscFlags = 0;
        staging_desc.SampleDesc.Count = 1;
        staging_desc.SampleDesc.Quality = 0;
        D3D11_TEXTURE2D_DESC resolved_desc = back_desc;
        resolved_desc.Usage = D3D11_USAGE_DEFAULT;
        resolved_desc.BindFlags = 0;
        resolved_desc.CPUAccessFlags = 0;
        resolved_desc.MiscFlags = 0;
        resolved_desc.SampleDesc.Count = 1;
        resolved_desc.SampleDesc.Quality = 0;

        for (auto& slot : slots_) {
            const D3D11_QUERY_DESC query_desc{D3D11_QUERY_EVENT, 0};
            if (FAILED(device_->CreateTexture2D(&staging_desc, nullptr, &slot.staging)) ||
                FAILED(device_->CreateQuery(&query_desc, &slot.completion))) {
                ReleaseDeviceResources();
                return false;
            }
            if (sample_count_ > 1 && FAILED(device_->CreateTexture2D(&resolved_desc,
                    nullptr, &slot.resolved))) {
                ReleaseDeviceResources();
                return false;
            }
        }
        return true;
    }

    void ConvertToRgb(const D3D11_MAPPED_SUBRESOURCE& mapped) noexcept {
        const bool bgra = format_ == DXGI_FORMAT_B8G8R8A8_UNORM ||
            format_ == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
        for (std::uint32_t y = 0; y < kHeight; ++y) {
            const std::uint32_t source_y = ((2U * y + 1U) * height_) / (2U * kHeight);
            const auto* row = source + static_cast<std::size_t>(source_y) * mapped.RowPitch;
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const std::uint32_t source_x = ((2U * x + 1U) * width_) / (2U * kWidth);
                const auto* pixel = row + static_cast<std::size_t>(source_x) * 4U;
                const std::size_t target = (static_cast<std::size_t>(y) * kWidth + x) * 3U;
                rgb_[target] = pixel[bgra ? 2 : 0];
                rgb_[target + 1] = pixel[1];
                rgb_[target + 2] = pixel[bgra ? 0 : 2];
            }
        }
    }

    void ReadCompletedSlots() noexcept {
        for (auto& slot : slots_) {
            if (!slot.pending) continue;
            const HRESULT query = context_->GetData(slot.completion, nullptr, 0,
                D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (query == S_FALSE) continue;
            if (FAILED(query)) { slot.pending = false; continue; }
            if (slot.timestamp_ns <= last_published_timestamp_ns_) {
                slot.pending = false;
                continue;
            }
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const HRESULT result = context_->Map(slot.staging, 0, D3D11_MAP_READ,
                D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            if (result == DXGI_ERROR_WAS_STILL_DRAWING) continue;
            slot.pending = false;
            if (FAILED(result)) continue;
            ConvertToRgb(mapped);
            context_->Unmap(slot.staging, 0);
            if (publisher_.Publish(rgb_.data(), slot.timestamp_ns, true))
                last_published_timestamp_ns_ = slot.timestamp_ns;
        }
    }

    void ScheduleReadback(ID3D11Texture2D* back_buffer,
                          std::uint64_t timestamp_ns) noexcept {
        ReadbackSlot* available = nullptr;
        for (auto& slot : slots_) {
            if (!slot.pending) { available = &slot; break; }
        }
        if (!available) return;
        if (sample_count_ > 1) {
            context_->ResolveSubresource(available->resolved, 0, back_buffer, 0, format_);
            context_->CopyResource(available->staging, available->resolved);
        } else {
            context_->CopyResource(available->staging, back_buffer);
        }
        context_->End(available->completion);
        available->timestamp_ns = timestamp_ns;
        available->pending = true;
    }

    void Capture(IDXGISwapChain* chain) noexcept {
        const std::uint64_t now = MonotonicNanoseconds();
        if (now == 0 || now < resource_retry_after_ns_) return;
        ID3D11Texture2D* back_buffer = nullptr;
        if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&back_buffer))) || !back_buffer) return;
        D3D11_TEXTURE2D_DESC back_desc{};
        back_buffer->GetDesc(&back_desc);
        if (!EnsureDeviceResources(chain, back_desc)) {
            resource_retry_after_ns_ = now + 1'000'000'000ULL;
            back_buffer->Release();
            return;
        }
        resource_retry_after_ns_ = 0;
        ReadCompletedSlots();
        if (next_capture_ns_ == 0 || now >= next_capture_ns_) {
            ScheduleReadback(back_buffer, now);
            next_capture_ns_ = now + kCaptureIntervalNs;
        }
        back_buffer->Release();
    }

    GameFramePublisher publisher_;
    std::mutex capture_mutex_;
    std::atomic_bool active_{false};
    IDXGISwapChain* probe_chain_{nullptr};
    ID3D11Device* probe_device_{nullptr};
    ID3D11DeviceContext* probe_context_{nullptr};
    HMODULE self_module_{nullptr};
    HWND probe_window_{nullptr};
    HWND target_window_{nullptr};
    void** present_vtable_{nullptr};
    bool hook_installed_{false};
    bool hook_conflicted_{false};
    ID3D11Device* device_{nullptr};
    ID3D11DeviceContext* context_{nullptr};
    std::array<ReadbackSlot, kReadbackCount> slots_{};
    std::array<std::uint8_t, kPixelBytes> rgb_{};
    UINT width_{0};
    UINT height_{0};
    UINT sample_count_{0};
    DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
    std::uint64_t next_capture_ns_{0};
    std::uint64_t resource_retry_after_ns_{0};
    std::uint64_t last_published_timestamp_ns_{0};
};

CaptureState g_capture_state;

} // namespace

bool StartGameFrameCapture() noexcept { return g_capture_state.Start(); }
void StopGameFrameCapture() noexcept { g_capture_state.Stop(); }

} // namespace ets2ap::plugin
