// Edited by: OpenAI-Codex_Responsible: Source_Code
#include <ets2ap/telemetry/seqlock.h>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <numbers>

namespace ets2ap::telemetry {
namespace {
constexpr std::size_t kWords = kStructSize / sizeof(std::uint64_t);
constexpr std::size_t kSequenceWord = offsetof(TelemetryV1, sequence) / sizeof(std::uint64_t);
bool Aligned(const void* value) noexcept {
    return value && (reinterpret_cast<std::uintptr_t>(value) % alignof(std::uint64_t) == 0);
}
std::uint64_t Load(const std::uint64_t* word, std::memory_order order) noexcept {
    // atomic_ref prevents a C++ data race while another process publishes words.
    return std::atomic_ref<std::uint64_t>(*const_cast<std::uint64_t*>(word)).load(order);
}
void Store(std::uint64_t* word, std::uint64_t value, std::memory_order order) noexcept {
    std::atomic_ref<std::uint64_t>(*word).store(value, order);
}
bool InRange(double value, double min, double max) noexcept {
    return std::isfinite(value) && value >= min && value <= max;
}
bool NonNegative(double value) noexcept { return std::isfinite(value) && value >= 0; }
bool Finite(double value) noexcept { return std::isfinite(value); }
} // namespace

SeqlockWriter::SeqlockWriter(void* base, std::size_t length) noexcept
    : base_(static_cast<std::byte*>(base)), length_(length) {}

bool SeqlockWriter::IsValid() const noexcept {
    return Aligned(base_) && length_ >= kStructSize && length_ <= kMappingSize;
}

bool SeqlockWriter::Publish(const TelemetryV1& value) noexcept {
    if (!IsValid()) return false;
    auto* words = reinterpret_cast<std::uint64_t*>(base_);
    const auto prior = Load(&words[kSequenceWord], std::memory_order_relaxed);
    const auto even = (prior & 1U) ? prior + 1U : prior;
    Store(&words[kSequenceWord], even + 1U, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    const auto* source = reinterpret_cast<const std::byte*>(&value);
    for (std::size_t i = 0; i < kWords; ++i) {
        if (i == kSequenceWord) continue;
        std::uint64_t word = 0;
        std::memcpy(&word, source + i * sizeof(word), sizeof(word));
        Store(&words[i], word, std::memory_order_relaxed);
    }
    Store(&words[kSequenceWord], even + 2U, std::memory_order_release);
    return true;
}

bool SeqlockWriter::ResetPublish(std::uint64_t timestamp_ns) noexcept {
    if (!IsValid()) return false;
    auto* words = reinterpret_cast<std::uint64_t*>(base_);
    const auto prior = Load(&words[kSequenceWord], std::memory_order_relaxed);
    if (prior & 1U) Store(&words[kSequenceWord], prior + 1U, std::memory_order_release);
    const auto even = (prior & 1U) ? prior + 1U : prior;
    Store(&words[kSequenceWord], even + 1U, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    TelemetryV1 empty{};
    empty.magic = kMagic;
    empty.version_major = kVersionMajor;
    empty.version_minor = kVersionMinor;
    empty.struct_size = kStructSize;
    empty.timestamp_ns = timestamp_ns;
    // The extension region and known payload are cleared within one odd interval.
    if (length_ > kStructSize) std::memset(base_ + kStructSize, 0, length_ - kStructSize);
    const auto* source = reinterpret_cast<const std::byte*>(&empty);
    for (std::size_t i = 0; i < kWords; ++i) {
        if (i == kSequenceWord) continue;
        std::uint64_t word = 0;
        std::memcpy(&word, source + i * sizeof(word), sizeof(word));
        Store(&words[i], word, std::memory_order_relaxed);
    }
    Store(&words[kSequenceWord], even + 2U, std::memory_order_release);
    return true;
}

ReadStatus TryReadSnapshot(const void* base, std::size_t mapped_len,
                           TelemetryV1* out) noexcept {
    if (!Aligned(base) || !out || mapped_len < kStructSize) return ReadStatus::TooSmall;
    const auto* words = reinterpret_cast<const std::uint64_t*>(base);
    bool odd = false;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        const auto first = Load(&words[kSequenceWord], std::memory_order_acquire);
        if (first & 1U) { odd = true; continue; }
        std::uint64_t copy[kWords]{};
        for (std::size_t i = 0; i < kWords; ++i) {
            if (i != kSequenceWord) copy[i] = Load(&words[i], std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        const auto second = Load(&words[kSequenceWord], std::memory_order_acquire);
        if (first != second || (second & 1U)) { odd = false; continue; }
        copy[kSequenceWord] = second;
        TelemetryV1 candidate{};
        std::memcpy(&candidate, copy, kStructSize);
        if (candidate.struct_size > mapped_len) return ReadStatus::TooSmall;
        *out = candidate;
        return ReadStatus::Ok;
    }
    return odd ? ReadStatus::Odd : ReadStatus::Torn;
}

ValidationError ValidateSnapshot(const TelemetryV1& v,
    std::optional<std::uint64_t> previous_sequence,
    std::optional<std::uint64_t> previous_timestamp) noexcept {
    if (v.magic != kMagic) return ValidationError::BadMagic;
    if (v.version_major != kVersionMajor) return ValidationError::UnsupportedMajor;
    if (v.struct_size < kStructSize || v.struct_size > kMappingSize)
        return ValidationError::BadSize;
    if (previous_sequence && v.sequence < *previous_sequence)
        return ValidationError::SequenceRegression;
    if (previous_timestamp && v.timestamp_ns < *previous_timestamp)
        return ValidationError::TimestampRegression;
    const auto has = [&v](unsigned bit) { return (v.validity_flags & (1U << bit)) != 0; };
    if ((has(0) && v.paused > 1) || (has(1) && (v.moving_backward > 1 || !NonNegative(v.speed_mps))) ||
        (has(2) && !NonNegative(v.engine_speed_rad_s)) ||
        (has(4) && (!InRange(v.steering_input, -1, 1) || !InRange(v.steering_effective, -1, 1))) ||
        (has(5) && (!InRange(v.throttle_input, 0, 1) || !InRange(v.throttle_effective, 0, 1))) ||
        (has(6) && (!InRange(v.brake_input, 0, 1) || !InRange(v.brake_effective, 0, 1))) ||
        (has(7) && (!Finite(v.world_x_m) || !Finite(v.world_y_m) || !Finite(v.world_z_m) ||
                    !(std::isfinite(v.heading_rad) && v.heading_rad >= 0 &&
                      v.heading_rad < 2 * std::numbers::pi) ||
                    !InRange(v.pitch_rad, -std::numbers::pi / 2, std::numbers::pi / 2) ||
                    !InRange(v.roll_rad, -std::numbers::pi, std::numbers::pi))) ||
        (has(8) && (!Finite(v.linear_velocity_local_x_mps) || !Finite(v.linear_velocity_local_y_mps) ||
                    !Finite(v.linear_velocity_local_z_mps))) ||
        (has(9) && (!Finite(v.angular_velocity_local_x_rad_s) || !Finite(v.angular_velocity_local_y_rad_s) ||
                    !Finite(v.angular_velocity_local_z_rad_s))) ||
        (has(10) && !NonNegative(v.navigation_distance_m)) ||
        (has(11) && !NonNegative(v.navigation_time_s)) ||
        (has(12) && !NonNegative(v.speed_limit_mps)) ||
        (has(13) && !NonNegative(v.cruise_speed_mps))) return ValidationError::InvalidField;
    return ValidationError::None;
}
} // namespace ets2ap::telemetry
