// Edited by: OpenAI-Codex_Responsible: Source_Code
#pragma once

#include <ets2ap/telemetry/layout.h>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ets2ap::telemetry {

enum class ReadStatus { Ok, Odd, Torn, TooSmall };
enum class ValidationError {
    None, BadMagic, UnsupportedMajor, BadSize, InvalidField,
    SequenceRegression, TimestampRegression
};

class SeqlockWriter {
public:
    SeqlockWriter(void* base, std::size_t length) noexcept;
    bool IsValid() const noexcept;
    bool Publish(const TelemetryV1& value) noexcept;
    bool ResetPublish(std::uint64_t timestamp_ns) noexcept;
private:
    std::byte* base_;
    std::size_t length_;
};

ReadStatus TryReadSnapshot(const void* base, std::size_t mapped_len,
                           TelemetryV1* out) noexcept;
ValidationError ValidateSnapshot(const TelemetryV1& value,
    std::optional<std::uint64_t> previous_sequence = {},
    std::optional<std::uint64_t> previous_timestamp = {}) noexcept;

} // namespace ets2ap::telemetry
