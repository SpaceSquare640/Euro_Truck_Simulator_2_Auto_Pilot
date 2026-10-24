// Edited by: OpenAI-Codex_Responsible: Source_Code
#include <ets2ap/telemetry/seqlock.h>
#include <ets2ap/test/test_framework.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <thread>

namespace {

using namespace ets2ap::telemetry;

struct alignas(8) MappingBuffer {
    std::byte bytes[kMappingSize]{};
};

TelemetryV1 ValidSample() {
    TelemetryV1 value{};
    value.magic = kMagic;
    value.version_major = kVersionMajor;
    value.version_minor = kVersionMinor;
    value.struct_size = kStructSize;
    value.validity_flags = 1u << 1;
    value.speed_mps = 12.5;
    value.moving_backward = 0;
    value.timestamp_ns = 123;
    return value;
}

}  // namespace

ETS2AP_TEST(SeqlockPublishSequenceWrapAndCanary) {
    MappingBuffer storage{};
    std::memset(storage.bytes, 0xA5, sizeof(storage.bytes));
    auto* words = reinterpret_cast<std::uint64_t*>(storage.bytes);
    words[2] = std::numeric_limits<std::uint64_t>::max() - 1;
    SeqlockWriter writer(storage.bytes, sizeof(storage.bytes));
    CHECK(writer.IsValid());

    const auto value = ValidSample();
    CHECK(writer.Publish(value));
    TelemetryV1 observed{};
    CHECK_EQ(TryReadSnapshot(storage.bytes, sizeof(storage.bytes), &observed), ReadStatus::Ok);
    CHECK_EQ(observed.sequence, 0ull);
    CHECK_EQ(observed.speed_mps, 12.5);
    CHECK(writer.Publish(value));
    CHECK_EQ(TryReadSnapshot(storage.bytes, sizeof(storage.bytes), &observed), ReadStatus::Ok);
    CHECK_EQ(observed.sequence, 2ull);
    for (std::size_t offset = kStructSize; offset < kMappingSize; ++offset) {
        CHECK_EQ(std::to_integer<unsigned>(storage.bytes[offset]), 0xA5u);
    }
}

ETS2AP_TEST(SeqlockResetClearsPayloadAndTail) {
    MappingBuffer storage{};
    std::memset(storage.bytes, 0xA5, sizeof(storage.bytes));
    auto* words = reinterpret_cast<std::uint64_t*>(storage.bytes);
    words[2] = 11;
    SeqlockWriter writer(storage.bytes, sizeof(storage.bytes));
    CHECK(writer.ResetPublish(456));
    TelemetryV1 observed{};
    CHECK_EQ(TryReadSnapshot(storage.bytes, sizeof(storage.bytes), &observed), ReadStatus::Ok);
    CHECK_EQ(observed.magic, kMagic);
    CHECK_EQ(observed.struct_size, kStructSize);
    CHECK_EQ(observed.validity_flags, 0u);
    CHECK_EQ(observed.timestamp_ns, 456ull);
    CHECK_EQ(observed.sequence % 2, 0ull);
    for (std::size_t offset = kStructSize; offset < kMappingSize; ++offset) {
        CHECK_EQ(std::to_integer<unsigned>(storage.bytes[offset]), 0u);
    }
}

ETS2AP_TEST(SeqlockReaderReportsOddTooSmallAndOk) {
    MappingBuffer storage{};
    TelemetryV1 observed{};
    CHECK_EQ(TryReadSnapshot(storage.bytes, kStructSize - 1, &observed), ReadStatus::TooSmall);
    auto* words = reinterpret_cast<std::uint64_t*>(storage.bytes);
    words[2] = 1;
    CHECK_EQ(TryReadSnapshot(storage.bytes, sizeof(storage.bytes), &observed), ReadStatus::Odd);
    words[2] = 0;
    SeqlockWriter writer(storage.bytes, sizeof(storage.bytes));
    CHECK(writer.Publish(ValidSample()));
    CHECK_EQ(TryReadSnapshot(storage.bytes, sizeof(storage.bytes), &observed), ReadStatus::Ok);
    CHECK_EQ(observed.sequence, 2ull);
}

ETS2AP_TEST(SeqlockReaderRejectsDeclaredSizeBeyondView) {
    MappingBuffer storage{};
    SeqlockWriter writer(storage.bytes, sizeof(storage.bytes));
    auto sample = ValidSample();
    sample.struct_size = static_cast<std::uint32_t>(kMappingSize);
    CHECK(writer.Publish(sample));
    TelemetryV1 observed{};
    CHECK_EQ(TryReadSnapshot(storage.bytes, kStructSize, &observed), ReadStatus::TooSmall);
    CHECK_EQ(TryReadSnapshot(storage.bytes, kMappingSize, &observed), ReadStatus::Ok);
}

ETS2AP_TEST(SeqlockReaderRejectsChangingEvenSequence) {
    MappingBuffer storage{};
    auto* sequence = reinterpret_cast<std::uint64_t*>(storage.bytes) + 2;
    std::atomic_ref<std::uint64_t> shared_sequence(*sequence);
    std::atomic<bool> running{true};
    std::thread changer([&] {
        std::uint64_t next = 0;
        while (running.load(std::memory_order_relaxed)) {
            shared_sequence.store(next += 2, std::memory_order_release);
        }
    });
    bool saw_torn = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!saw_torn && std::chrono::steady_clock::now() < deadline) {
        TelemetryV1 observed{};
        saw_torn = TryReadSnapshot(storage.bytes, sizeof(storage.bytes), &observed) == ReadStatus::Torn;
    }
    running.store(false, std::memory_order_relaxed);
    changer.join();
    CHECK(saw_torn);
}

ETS2AP_TEST(SeqlockValidateSnapshotRejectsBadKnownFields) {
    auto sample = ValidSample();
    sample.sequence = 2;
    CHECK_EQ(ValidateSnapshot(sample), ValidationError::None);
    sample.validity_flags |= 1u << 31;
    CHECK_EQ(ValidateSnapshot(sample), ValidationError::None);  // Compatible minor extension.
    sample.magic = 0;
    CHECK_EQ(ValidateSnapshot(sample), ValidationError::BadMagic);
    sample.magic = kMagic;
    sample.version_major = 2;
    CHECK_EQ(ValidateSnapshot(sample), ValidationError::UnsupportedMajor);
    sample.version_major = kVersionMajor;
    sample.struct_size = kStructSize - 1;
    CHECK_EQ(ValidateSnapshot(sample), ValidationError::BadSize);
    sample.struct_size = static_cast<std::uint32_t>(kMappingSize + 1);
    CHECK_EQ(ValidateSnapshot(sample), ValidationError::BadSize);
    sample.struct_size = kStructSize;
    sample.speed_mps = std::numeric_limits<double>::quiet_NaN();
    CHECK_EQ(ValidateSnapshot(sample), ValidationError::InvalidField);
    sample.speed_mps = 12.5;
    CHECK_EQ(ValidateSnapshot(sample, 4, {}), ValidationError::SequenceRegression);
    CHECK_EQ(ValidateSnapshot(sample, {}, 124), ValidationError::TimestampRegression);
}

ETS2AP_TEST(SeqlockValidateHeadingHasExclusiveUpperBound) {
    auto sample = ValidSample();
    sample.validity_flags |= 1u << 7;
    sample.heading_rad = 2.0 * std::acos(-1.0);
    CHECK_EQ(ValidateSnapshot(sample), ValidationError::InvalidField);
    sample.heading_rad = 0;
    CHECK_EQ(ValidateSnapshot(sample), ValidationError::None);
}

ETS2AP_TEST(SeqlockStressAcceptsOnlyConsistentSnapshots) {
    MappingBuffer storage{};
    SeqlockWriter writer(storage.bytes, sizeof(storage.bytes));
    std::atomic<bool> running{true};
    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> torn_accepted{0};
    const auto start = std::chrono::steady_clock::now();
    std::thread reader([&] {
        do {
            TelemetryV1 snapshot{};
            if (TryReadSnapshot(storage.bytes, sizeof(storage.bytes), &snapshot) == ReadStatus::Ok &&
                snapshot.sequence != 0) {
                ++accepted;
                if (snapshot.speed_mps != static_cast<double>(snapshot.sequence / 2) ||
                    snapshot.navigation_distance_m != 2.0 * snapshot.speed_mps) {
                    ++torn_accepted;
                }
            }
        } while (running.load());
    });
    for (std::uint64_t index = 1; index <= 200'000; ++index) {
        auto snapshot = ValidSample();
        snapshot.speed_mps = static_cast<double>(index);
        snapshot.navigation_distance_m = 2.0 * static_cast<double>(index);
        if (!writer.Publish(snapshot)) {
            ++torn_accepted;
            break;
        }
    }
    running = false;
    reader.join();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK_EQ(torn_accepted.load(), 0ull);
    CHECK(accepted.load() > 0);
    CHECK(elapsed < std::chrono::seconds(10));
}
