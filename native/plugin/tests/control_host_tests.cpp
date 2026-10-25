// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/plugin/control_host.h"
#include "ets2ap/test/test_framework.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

using namespace ets2ap::input;
using namespace ets2ap::plugin;

namespace {
constexpr std::uint64_t kStartNs = 2'000'000'000ULL;
constexpr std::uint32_t kCommandMagic = 0x50413243U;

void PutLittleEndian(std::vector<std::byte>& bytes, std::size_t offset,
                     std::uint64_t value, std::size_t width) {
    for (std::size_t index = 0; index < width; ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8)) & 0xFFU);
    }
}

void PutDouble(std::vector<std::byte>& bytes, std::size_t offset, double value) {
    PutLittleEndian(bytes, offset, std::bit_cast<std::uint64_t>(value), 8);
}

std::vector<std::byte> Packet(std::uint64_t sequence, std::uint64_t timestamp_ns,
                              double steering = 0.0, double throttle = 0.0,
                              double brake = 0.0, bool engage = false) {
    std::vector<std::byte> bytes(64);
    PutLittleEndian(bytes, 0, kCommandMagic, 4);
    PutLittleEndian(bytes, 4, 1, 2);
    PutLittleEndian(bytes, 6, 0, 2);
    PutLittleEndian(bytes, 8, 64, 4);
    PutLittleEndian(bytes, 16, sequence, 8);
    PutLittleEndian(bytes, 24, timestamp_ns, 8);
    PutDouble(bytes, 32, steering);
    PutDouble(bytes, 40, throttle);
    PutDouble(bytes, 48, brake);
    PutLittleEndian(bytes, 56, engage ? 1 : 0, 1);
    return bytes;
}

SafetyGates SafeGates() {
    return {true, true, true, 1.0};
}

struct CapturedOutput {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<ControlSnapshot> snapshots;
    std::size_t neutralizations{};
};

ControlHostOptions OptionsFor(std::atomic<std::uint64_t>& now_ns,
                              CapturedOutput& output) {
    ControlHostOptions options{};
    options.now_ns = [&now_ns] {
        return std::optional<std::uint64_t>{now_ns.load(std::memory_order_acquire)};
    };
    options.apply_output = [&output](const ControlSnapshot& snapshot, std::uint64_t) {
        {
            const std::lock_guard lock(output.mutex);
            output.snapshots.push_back(snapshot);
        }
        output.changed.notify_all();
        return true;
    };
    options.force_neutral = [&output] {
        {
            const std::lock_guard lock(output.mutex);
            ++output.neutralizations;
        }
        output.changed.notify_all();
    };
    options.tick_interval = std::chrono::milliseconds{2};
    return options;
}

bool WaitForSnapshot(CapturedOutput& output,
                     const std::function<bool(const ControlSnapshot&)>& predicate) {
    std::unique_lock lock(output.mutex);
    return output.changed.wait_for(lock, std::chrono::seconds{2}, [&] {
        return std::any_of(output.snapshots.begin(), output.snapshots.end(), predicate);
    });
}

} // namespace

ETS2AP_TEST(ControlHostWatchdogRunsWithoutNewCommands) {
    std::atomic<std::uint64_t> now_ns{kStartNs};
    CapturedOutput output;
    ControlHost host(OptionsFor(now_ns, output));
    CHECK(host.Start());
    CHECK(host.IsRunning());
    CHECK(host.Connect());

    now_ns.store(kStartNs + 1, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(1, kStartNs + 1)).outcome,
             CommandOutcome::Accepted);
    now_ns.store(kStartNs + 2, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(2, kStartNs + 2, 0, 0.5, 0, true), SafeGates()).outcome,
             CommandOutcome::Accepted);
    now_ns.store(kStartNs + 20'000'002ULL, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(3, kStartNs + 20'000'002ULL, 0, 0.5, 0, true),
                                SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::Engaged && snapshot.throttle > 0.0;
    }));

    now_ns.store(kStartNs + 200'000'000ULL, std::memory_order_release);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::Timeout && snapshot.steering == 0.0 &&
               snapshot.throttle == 0.0 && snapshot.brake == 0.0;
    }));
    host.Stop();
    CHECK(!host.IsRunning());
    CHECK_EQ(host.AcceptCommand(Packet(4, kStartNs + 200'000'001ULL)).outcome,
             CommandOutcome::ConnectionFatal);
    {
        const std::lock_guard lock(output.mutex);
        CHECK(output.neutralizations > 0);
    }
}

ETS2AP_TEST(ControlHostManualOverridePublishesNeutralImmediately) {
    std::atomic<std::uint64_t> now_ns{kStartNs};
    CapturedOutput output;
    ControlHost host(OptionsFor(now_ns, output));
    CHECK(host.Start());
    CHECK(host.Connect());

    now_ns.store(kStartNs + 1, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(1, kStartNs + 1)).outcome,
             CommandOutcome::Accepted);
    now_ns.store(kStartNs + 2, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(2, kStartNs + 2, 0, 0.5, 0, true), SafeGates()).outcome,
             CommandOutcome::Accepted);
    now_ns.store(kStartNs + 20'000'002ULL, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(3, kStartNs + 20'000'002ULL, 0, 0.5, 0, true),
                                SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::Engaged && snapshot.throttle > 0.0;
    }));

    host.SetManualInput(true);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::ManualOverrideLatched &&
               snapshot.steering == 0.0 && snapshot.throttle == 0.0 && snapshot.brake == 0.0;
    }));
    host.SetManualInput(false);
    now_ns.store(kStartNs + 20'000'003ULL, std::memory_order_release);
    CHECK_EQ(host.AcceptCommand(Packet(4, kStartNs + 20'000'003ULL),
                                SafeGates()).outcome, CommandOutcome::Accepted);
    CHECK(WaitForSnapshot(output, [](const ControlSnapshot& snapshot) {
        return snapshot.state == ControlState::Disengaged && snapshot.throttle == 0.0;
    }));

    host.Stop();
}

ETS2AP_TEST(ControlHostStartFailsClosedWhenClockIsUnavailable) {
    std::atomic<std::uint64_t> now_ns{kStartNs};
    CapturedOutput output;
    ControlHostOptions options = OptionsFor(now_ns, output);
    options.now_ns = [] { return std::optional<std::uint64_t>{}; };
    ControlHost host(std::move(options));
    CHECK(!host.Start());
    CHECK(!host.IsRunning());
    const std::lock_guard lock(output.mutex);
    CHECK_EQ(output.neutralizations, std::size_t{1});
}

ETS2AP_TEST(ControlHostRejectsSlowSchedulerAndFailedOutputSink) {
    std::atomic<std::uint64_t> now_ns{kStartNs};
    CapturedOutput slow_output;
    auto slow_options = OptionsFor(now_ns, slow_output);
    slow_options.tick_interval = std::chrono::milliseconds{11};
    ControlHost slow_host(std::move(slow_options));
    CHECK(!slow_host.Start());
    CHECK_EQ(slow_output.neutralizations, std::size_t{1});

    CapturedOutput failed_output;
    auto failed_options = OptionsFor(now_ns, failed_output);
    failed_options.apply_output = [](const ControlSnapshot&, std::uint64_t) { return false; };
    ControlHost failed_host(std::move(failed_options));
    CHECK(!failed_host.Start());
    CHECK(!failed_host.IsRunning());
    CHECK_EQ(failed_output.neutralizations, std::size_t{1});
}
