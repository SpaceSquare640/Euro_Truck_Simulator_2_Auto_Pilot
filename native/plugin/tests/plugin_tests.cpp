// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/plugin/telemetry_plugin.h"
#include "ets2ap/plugin/input_plugin.h"
#include "ets2ap/plugin/control_host.h"
#include "ets2ap/telemetry/seqlock.h"
#include "ets2ap/test/test_framework.h"

#include <eurotrucks2/scssdk_eut2.h>
#include <scssdk_input.h>
#include <eurotrucks2/scssdk_telemetry_eut2.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ets2ap::telemetry;
using ets2ap::plugin::InputPlugin;
using ets2ap::plugin::InputPluginOptions;
using ets2ap::plugin::PluginOptions;
using ets2ap::plugin::TelemetryPlugin;

struct EventRegistration {
  scs_event_t event;
  scs_telemetry_event_callback_t callback;
  scs_context_t context;
};
struct ChannelRegistration {
  std::string name;
  scs_u32_t index;
  scs_value_type_t type;
  scs_u32_t flags;
  scs_telemetry_channel_callback_t callback;
  scs_context_t context;
};

class FakeSdk {
 public:
  explicit FakeSdk(const char* game_id = SCS_GAME_ID_EUT2) {
    active_ = this;
    params_.common.game_name = "Test game";
    params_.common.game_id = game_id;
    params_.common.log = &Log;
    params_.register_for_event = &RegisterEvent;
    params_.register_for_channel = &RegisterChannel;
  }
  ~FakeSdk() { if (active_ == this) active_ = nullptr; }
  FakeSdk(const FakeSdk&) = delete;
  FakeSdk& operator=(const FakeSdk&) = delete;

  const scs_telemetry_init_params_v100_t* params() const { return &params_; }
  void Emit(scs_event_t event) const {
    for (const auto& item : events) {
      if (item.event == event) item.callback(event, nullptr, item.context);
    }
  }
  void Emit(const char* name, const scs_value_t* value) const {
    for (const auto& item : channels) {
      if (item.name == name) {
        item.callback(name, SCS_U32_NIL, value, item.context);
        return;
      }
    }
    throw std::runtime_error("channel was not registered");
  }
  bool HasChannel(const char* name, scs_value_type_t type) const {
    for (const auto& item : channels) if (item.name == name && item.type == type) return true;
    return false;
  }
  std::vector<EventRegistration> events;
  std::vector<ChannelRegistration> channels;
  std::vector<std::string> logs;
  bool throw_on_log = false;
  const char* fail_channel = nullptr;

 private:
  static SCSAPI_VOID Log(scs_log_type_t, scs_string_t message) {
    if (!active_) return;
    if (active_->throw_on_log) throw std::runtime_error("injected log failure");
    active_->logs.emplace_back(message ? message : "");
  }
  static SCSAPI_RESULT RegisterEvent(scs_event_t event, scs_telemetry_event_callback_t callback,
                                    scs_context_t context) {
    active_->events.push_back({event, callback, context});
    return SCS_RESULT_ok;
  }
  static SCSAPI_RESULT RegisterChannel(scs_string_t name, scs_u32_t index,
                                      scs_value_type_t type, scs_u32_t flags,
                                      scs_telemetry_channel_callback_t callback, scs_context_t context) {
    if (active_->fail_channel && std::string(name) == active_->fail_channel) return SCS_RESULT_not_found;
    active_->channels.push_back({name, index, type, flags, callback, context});
    return SCS_RESULT_ok;
  }
  scs_telemetry_init_params_v100_t params_{};
  static inline FakeSdk* active_ = nullptr;
};

struct InputRegistration {
  scs_input_device_t device{};
  std::vector<scs_input_device_input_t> inputs;
};

class FakeInputSdk {
 public:
  FakeInputSdk() {
    active_ = this;
    params_.common.game_name = "Test game";
    params_.common.game_id = SCS_GAME_ID_EUT2;
    params_.register_device = &RegisterDevice;
  }
  ~FakeInputSdk() { if (active_ == this) active_ = nullptr; }
  FakeInputSdk(const FakeInputSdk&) = delete;
  FakeInputSdk& operator=(const FakeInputSdk&) = delete;

  const scs_input_init_params_v100_t* params() const { return &params_; }
  InputRegistration registration;
  scs_result_t result = SCS_RESULT_ok;

 private:
  static SCSAPI_RESULT RegisterDevice(const scs_input_device_t* device) {
    if (!active_ || !device) return SCS_RESULT_generic_error;
    active_->registration.device = *device;
    active_->registration.inputs.assign(device->inputs, device->inputs + device->input_count);
    active_->registration.device.inputs = active_->registration.inputs.data();
    return active_->result;
  }
  scs_input_init_params_v100_t params_{};
  static inline FakeInputSdk* active_ = nullptr;
};

MappingNames UniqueNames() {
  static std::atomic<unsigned> counter{0};
  const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"_" +
                      std::to_wstring(counter.fetch_add(1));
  return {L"Local\\ETS2AP_S5_plugin_" + suffix,
          L"Local\\ETS2AP_S5_plugin_" + suffix + L".lock"};
}
PluginOptions Options(const MappingNames& names) {
  PluginOptions options{};
  options.mapping_names = names;
  options.now_ns = [] { return std::optional<std::uint64_t>{1234567890ULL}; };
  return options;
}
TelemetryV1 Snapshot(const MappingNames& names) {
  ReadOnlyMapping reader{names};
  if (!reader.OpenReadOnly()) throw std::runtime_error("reader open failed");
  TelemetryV1 snapshot{};
  if (TryReadSnapshot(reader.data(), reader.size(), &snapshot) != ReadStatus::Ok)
    throw std::runtime_error("reader snapshot failed");
  return snapshot;
}
scs_value_t FloatValue(float value) {
  scs_value_t result{};
  result.type = SCS_VALUE_TYPE_float;
  result.value_float.value = value;
  return result;
}
scs_value_t GearValue(std::int32_t value) {
  scs_value_t result{};
  result.type = SCS_VALUE_TYPE_s32;
  result.value_s32.value = value;
  return result;
}
scs_value_t VectorValue(float x, float y, float z) {
  scs_value_t result{};
  result.type = SCS_VALUE_TYPE_fvector;
  result.value_fvector = {x, y, z};
  return result;
}
scs_value_t PlacementValue(double x, double y, double z, float heading, float pitch, float roll) {
  scs_value_t result{};
  result.type = SCS_VALUE_TYPE_dplacement;
  result.value_dplacement.position = {x, y, z};
  result.value_dplacement.orientation = {heading, pitch, roll};
  return result;
}

ETS2AP_TEST(InitAcceptsEts2RejectsAtsAndUnsupportedSdkVersion) {
  const auto names = UniqueNames();
  FakeSdk ets2;
  TelemetryPlugin plugin;
  CHECK_EQ(plugin.Init(ets2.params(), Options(names)), SCS_RESULT_ok);
  CHECK(Snapshot(names).validity_flags == 0);
  plugin.Shutdown();

  FakeSdk ats{"ats"};
  TelemetryPlugin other;
  const auto ats_names = UniqueNames();
  CHECK_EQ(other.Init(ats.params(), Options(ats_names)), SCS_RESULT_unsupported);
  ReadOnlyMapping absent{ats_names};
  CHECK(!absent.OpenReadOnly());
  CHECK_EQ(scs_telemetry_init(0xFFFFFFFFu,
           reinterpret_cast<const scs_telemetry_init_params_t*>(ats.params())), SCS_RESULT_unsupported);
}

ETS2AP_TEST(RegistersExactlyFourEventsAndSeventeenTypedChannels) {
  const auto names = UniqueNames();
  FakeSdk sdk;
  TelemetryPlugin plugin;
  CHECK_EQ(plugin.Init(sdk.params(), Options(names)), SCS_RESULT_ok);
  CHECK_EQ(sdk.events.size(), std::size_t{4});
  const std::array<scs_event_t, 4> expected_events = {
      SCS_TELEMETRY_EVENT_paused, SCS_TELEMETRY_EVENT_started,
      SCS_TELEMETRY_EVENT_frame_end, SCS_TELEMETRY_EVENT_configuration};
  for (auto event : expected_events) {
    std::size_t count = 0;
    for (const auto& item : sdk.events) if (item.event == event) ++count;
    CHECK_EQ(count, std::size_t{1});
  }
  CHECK_EQ(sdk.channels.size(), std::size_t{17});
  const std::array<std::pair<const char*, scs_value_type_t>, 17> expected_channels = {{
      {SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_VALUE_TYPE_float},
      {SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm, SCS_VALUE_TYPE_float},
      {SCS_TELEMETRY_TRUCK_CHANNEL_displayed_gear, SCS_VALUE_TYPE_s32},
      {SCS_TELEMETRY_TRUCK_CHANNEL_engine_gear, SCS_VALUE_TYPE_s32},
      {SCS_TELEMETRY_TRUCK_CHANNEL_input_steering, SCS_VALUE_TYPE_float},
      {SCS_TELEMETRY_TRUCK_CHANNEL_effective_steering, SCS_VALUE_TYPE_float},
      {SCS_TELEMETRY_TRUCK_CHANNEL_input_throttle, SCS_VALUE_TYPE_float},
      {SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle, SCS_VALUE_TYPE_float},
      {SCS_TELEMETRY_TRUCK_CHANNEL_input_brake, SCS_VALUE_TYPE_float},
      {SCS_TELEMETRY_TRUCK_CHANNEL_effective_brake, SCS_VALUE_TYPE_float},
      {SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_VALUE_TYPE_dplacement},
      {SCS_TELEMETRY_TRUCK_CHANNEL_local_linear_velocity, SCS_VALUE_TYPE_fvector},
      {SCS_TELEMETRY_TRUCK_CHANNEL_local_angular_velocity, SCS_VALUE_TYPE_fvector},
      {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_distance, SCS_VALUE_TYPE_float},
      {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_time, SCS_VALUE_TYPE_float},
      {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_speed_limit, SCS_VALUE_TYPE_float},
      {SCS_TELEMETRY_TRUCK_CHANNEL_cruise_control, SCS_VALUE_TYPE_float}}};
  for (const auto& [name, type] : expected_channels) CHECK(sdk.HasChannel(name, type));
  for (const auto& item : sdk.channels) {
    CHECK_EQ(item.index, SCS_U32_NIL);
    CHECK_EQ(item.flags, SCS_TELEMETRY_CHANNEL_FLAG_no_value);
  }
  plugin.Shutdown();
}

ETS2AP_TEST(RoundTripAllChannelsAndUnitConversions) {
  const auto names = UniqueNames();
  FakeSdk sdk;
  TelemetryPlugin plugin;
  CHECK_EQ(plugin.Init(sdk.params(), Options(names)), SCS_RESULT_ok);
  const std::array<std::pair<const char*, scs_value_t>, 17> values = {{
      {SCS_TELEMETRY_TRUCK_CHANNEL_speed, FloatValue(-1.25f)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm, FloatValue(3000.0f)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_displayed_gear, GearValue(6)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_engine_gear, GearValue(5)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_input_steering, FloatValue(0.5f)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_effective_steering, FloatValue(0.25f)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_input_throttle, FloatValue(0.75f)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle, FloatValue(0.5f)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_input_brake, FloatValue(0.125f)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_effective_brake, FloatValue(0.25f)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, PlacementValue(10, 20, 30, 0.25f, 0.125f, -0.25f)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_local_linear_velocity, VectorValue(1, 2, 3)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_local_angular_velocity, VectorValue(0.5f, 0.25f, -0.25f)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_distance, FloatValue(500)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_time, FloatValue(30)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_speed_limit, FloatValue(25)},
      {SCS_TELEMETRY_TRUCK_CHANNEL_cruise_control, FloatValue(20)}}};
  for (const auto& [name, value] : values) sdk.Emit(name, &value);
  sdk.Emit(SCS_TELEMETRY_EVENT_frame_end);
  const auto snapshot = Snapshot(names);
  CHECK_EQ(snapshot.validity_flags, kKnownValidityMask & ~std::uint32_t{1});
  CHECK_EQ(snapshot.timestamp_ns, 1234567890ULL);
  CHECK_EQ(snapshot.moving_backward, std::uint8_t{1});
  CHECK_NEAR(snapshot.speed_mps, 1.25, 1e-10);
  CHECK_NEAR(snapshot.engine_speed_rad_s, 100.0 * 3.14159265358979323846, 1e-8);
  CHECK_EQ(snapshot.gear_displayed, 6);
  CHECK_EQ(snapshot.gear_engine, 5);
  CHECK_NEAR(snapshot.steering_input, 0.5, 1e-10);
  CHECK_NEAR(snapshot.steering_effective, 0.25, 1e-10);
  CHECK_NEAR(snapshot.throttle_input, 0.75, 1e-10);
  CHECK_NEAR(snapshot.throttle_effective, 0.5, 1e-10);
  CHECK_NEAR(snapshot.brake_input, 0.125, 1e-10);
  CHECK_NEAR(snapshot.brake_effective, 0.25, 1e-10);
  CHECK_NEAR(snapshot.world_x_m, 10, 1e-10);
  CHECK_NEAR(snapshot.world_y_m, 20, 1e-10);
  CHECK_NEAR(snapshot.world_z_m, 30, 1e-10);
  CHECK_NEAR(snapshot.heading_rad, 3.14159265358979323846 / 2, 1e-8);
  CHECK_NEAR(snapshot.pitch_rad, 3.14159265358979323846 / 4, 1e-8);
  CHECK_NEAR(snapshot.roll_rad, -3.14159265358979323846 / 2, 1e-8);
  CHECK_NEAR(snapshot.linear_velocity_local_x_mps, 1, 1e-10);
  CHECK_NEAR(snapshot.linear_velocity_local_y_mps, 2, 1e-10);
  CHECK_NEAR(snapshot.linear_velocity_local_z_mps, 3, 1e-10);
  CHECK_NEAR(snapshot.angular_velocity_local_x_rad_s, 3.14159265358979323846, 1e-8);
  CHECK_NEAR(snapshot.angular_velocity_local_y_rad_s, 3.14159265358979323846 / 2, 1e-8);
  CHECK_NEAR(snapshot.angular_velocity_local_z_rad_s, -3.14159265358979323846 / 2, 1e-8);
  CHECK_NEAR(snapshot.navigation_distance_m, 500, 1e-10);
  CHECK_NEAR(snapshot.navigation_time_s, 30, 1e-10);
  CHECK_NEAR(snapshot.speed_limit_mps, 25, 1e-10);
  CHECK_NEAR(snapshot.cruise_speed_mps, 20, 1e-10);
  plugin.Shutdown();
}

ETS2AP_TEST(NullAndWrongTypeClearGroupUntilRecovery) {
  const auto names = UniqueNames();
  FakeSdk sdk;
  TelemetryPlugin plugin;
  CHECK_EQ(plugin.Init(sdk.params(), Options(names)), SCS_RESULT_ok);
  const auto steering = FloatValue(0.5f);
  const auto effective = FloatValue(0.25f);
  sdk.Emit(SCS_TELEMETRY_TRUCK_CHANNEL_input_steering, &steering);
  sdk.Emit(SCS_TELEMETRY_TRUCK_CHANNEL_effective_steering, &effective);
  sdk.Emit(SCS_TELEMETRY_EVENT_frame_end);
  CHECK((Snapshot(names).validity_flags & (1u << 4)) != 0);
  sdk.Emit(SCS_TELEMETRY_TRUCK_CHANNEL_input_steering, nullptr);
  sdk.Emit(SCS_TELEMETRY_EVENT_frame_end);
  const auto absent = Snapshot(names);
  CHECK((absent.validity_flags & (1u << 4)) == 0);
  CHECK_EQ(absent.steering_input, 0.0);
  CHECK_EQ(absent.steering_effective, 0.0);
  sdk.Emit(SCS_TELEMETRY_TRUCK_CHANNEL_input_steering, &steering);
  sdk.Emit(SCS_TELEMETRY_EVENT_frame_end);
  CHECK((Snapshot(names).validity_flags & (1u << 4)) != 0);
  const auto wrong_type = GearValue(2);
  sdk.Emit(SCS_TELEMETRY_TRUCK_CHANNEL_input_steering, &wrong_type);
  sdk.Emit(SCS_TELEMETRY_EVENT_frame_end);
  CHECK((Snapshot(names).validity_flags & (1u << 4)) == 0);
  plugin.Shutdown();
}

ETS2AP_TEST(PausedAndStartedPublishWithoutFrameEnd) {
  const auto names = UniqueNames();
  FakeSdk sdk;
  TelemetryPlugin plugin;
  CHECK_EQ(plugin.Init(sdk.params(), Options(names)), SCS_RESULT_ok);
  sdk.Emit(SCS_TELEMETRY_EVENT_paused);
  const auto paused = Snapshot(names);
  CHECK((paused.validity_flags & 1u) != 0);
  CHECK_EQ(paused.paused, std::uint8_t{1});
  sdk.Emit(SCS_TELEMETRY_EVENT_started);
  const auto started = Snapshot(names);
  CHECK((started.validity_flags & 1u) != 0);
  CHECK_EQ(started.paused, std::uint8_t{0});
  CHECK(started.sequence > paused.sequence);
  plugin.Shutdown();
}

ETS2AP_TEST(ControlSnapshotRequiresFreshUnpausedTelemetryAndSpeedLimit) {
  const auto names = UniqueNames();
  FakeSdk sdk;
  TelemetryPlugin plugin;
  CHECK_EQ(plugin.Init(sdk.params(), Options(names)), SCS_RESULT_ok);
  sdk.Emit(SCS_TELEMETRY_EVENT_paused);
  const auto speed_limit = FloatValue(20.0f);
  sdk.Emit(SCS_TELEMETRY_TRUCK_CHANNEL_navigation_speed_limit, &speed_limit);
  sdk.Emit(SCS_TELEMETRY_EVENT_frame_end);

  auto safety = plugin.ControlSnapshot(1234567890ULL);
  CHECK(!safety.fresh);
  CHECK(safety.paused);
  CHECK(safety.speed_limit_available);

  sdk.Emit(SCS_TELEMETRY_EVENT_started);
  safety = plugin.ControlSnapshot(1234567890ULL);
  CHECK(safety.fresh);
  CHECK(!safety.paused);
  CHECK(safety.speed_limit_available);
  safety = plugin.ControlSnapshot(1484567891ULL);
  CHECK(!safety.fresh);
  plugin.Shutdown();
}

ETS2AP_TEST(ConfigurationEventClearsAllValidity) {
  const auto names = UniqueNames();
  FakeSdk sdk;
  TelemetryPlugin plugin;
  CHECK_EQ(plugin.Init(sdk.params(), Options(names)), SCS_RESULT_ok);
  sdk.Emit(SCS_TELEMETRY_EVENT_paused);
  auto speed = FloatValue(4);
  sdk.Emit(SCS_TELEMETRY_TRUCK_CHANNEL_speed, &speed);
  sdk.Emit(SCS_TELEMETRY_EVENT_frame_end);
  CHECK(Snapshot(names).validity_flags != 0);
  sdk.Emit(SCS_TELEMETRY_EVENT_configuration);
  const auto after = Snapshot(names);
  CHECK_EQ(after.validity_flags, 0u);
  CHECK_EQ(after.speed_mps, 0.0);
  CHECK_EQ(after.paused, std::uint8_t{0});
  plugin.Shutdown();
}

ETS2AP_TEST(ShutdownPublishesInvalidSnapshotAndAllowsReinit) {
  const auto names = UniqueNames();
  FakeSdk sdk;
  TelemetryPlugin plugin;
  CHECK_EQ(plugin.Init(sdk.params(), Options(names)), SCS_RESULT_ok);
  ReadOnlyMapping reader{names};
  CHECK(static_cast<bool>(reader.OpenReadOnly()));
  sdk.Emit(SCS_TELEMETRY_EVENT_paused);
  plugin.Shutdown();
  TelemetryV1 after{};
  CHECK_EQ(TryReadSnapshot(reader.data(), reader.size(), &after), ReadStatus::Ok);
  CHECK_EQ(after.validity_flags, 0u);
  reader.Close();
  CHECK_EQ(plugin.Init(sdk.params(), Options(names)), SCS_RESULT_ok);
  plugin.Shutdown();
}

ETS2AP_TEST(SecondPluginWriterIsRejected) {
  const auto names = UniqueNames();
  FakeSdk sdk;
  TelemetryPlugin first;
  TelemetryPlugin second;
  CHECK_EQ(first.Init(sdk.params(), Options(names)), SCS_RESULT_ok);
  CHECK_EQ(second.Init(sdk.params(), Options(names)), SCS_RESULT_generic_error);
  first.Shutdown();
  second.Shutdown();
}

ETS2AP_TEST(ExportCatchesLogExceptionWithoutTouchingMapping) {
  FakeSdk sdk{"ats"};
  sdk.throw_on_log = true;
  CHECK_EQ(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_01,
           reinterpret_cast<const scs_telemetry_init_params_t*>(sdk.params())), SCS_RESULT_generic_error);
  scs_telemetry_shutdown();
}

ETS2AP_TEST(EventClockExceptionReleasesWriterOwnership) {
  const auto names = UniqueNames();
  FakeSdk sdk;
  TelemetryPlugin first;
  auto options = Options(names);
  options.now_ns = []() -> std::optional<std::uint64_t> {
    throw std::runtime_error("injected clock failure");
  };
  CHECK_EQ(first.Init(sdk.params(), options), SCS_RESULT_ok);
  sdk.Emit(SCS_TELEMETRY_EVENT_frame_end);
  TelemetryPlugin second;
  CHECK_EQ(second.Init(sdk.params(), Options(names)), SCS_RESULT_ok);
  second.Shutdown();
  first.Shutdown();
}

ETS2AP_TEST(InputPluginRegistersSemanticMixesWithoutPlayerBinding) {
  FakeInputSdk sdk;
  InputPlugin plugin;
  std::uint64_t now_ns = 1'000'000'000ULL;
  InputPluginOptions options{};
  options.now_ns = [&] { return std::optional<std::uint64_t>{now_ns}; };

  CHECK_EQ(plugin.Init(SCS_INPUT_VERSION_1_00, sdk.params(), options), SCS_RESULT_ok);
  CHECK_EQ(sdk.registration.device.type, SCS_INPUT_DEVICE_TYPE_semantical);
  CHECK_EQ(std::string(sdk.registration.device.name), std::string("ets2ap_autopilot"));
  CHECK_EQ(sdk.registration.inputs.size(), std::size_t{3});
  CHECK_EQ(std::string(sdk.registration.inputs[0].name), std::string("steering"));
  CHECK_EQ(std::string(sdk.registration.inputs[1].name), std::string("aforward"));
  CHECK_EQ(std::string(sdk.registration.inputs[2].name), std::string("abackward"));
  for (const auto& input : sdk.registration.inputs) CHECK_EQ(input.value_type, SCS_VALUE_TYPE_float);

  const auto emit_float = [&](std::uint32_t flags, std::uint32_t index, float expected) {
    scs_input_event_t event{};
    CHECK_EQ(sdk.registration.device.input_event_callback(&event, flags,
        sdk.registration.device.callback_context), SCS_RESULT_ok);
    CHECK_EQ(event.input_index, index);
    CHECK_NEAR(event.value_float.value, expected, 0.0001);
  };

  emit_float(SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame, 0, 0.0f);
  emit_float(0, 1, 0.0f);
  emit_float(0, 2, 0.0f);
  scs_input_event_t exhausted{};
  CHECK_EQ(sdk.registration.device.input_event_callback(&exhausted, 0,
      sdk.registration.device.callback_context), SCS_RESULT_not_found);

  CHECK(plugin.SetCommand({0.5f, 0.4f, 0.2f, true, now_ns}));
  emit_float(SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame, 0, 0.5f);
  emit_float(0, 1, 0.0f);
  emit_float(0, 2, 0.2f);

  now_ns += 100'000'000ULL;
  emit_float(SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame, 0, 0.5f);
  emit_float(0, 1, 0.0f);
  emit_float(0, 2, 0.2f);
  now_ns += 1ULL;
  emit_float(SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame, 0, 0.0f);
  emit_float(0, 1, 0.0f);
  emit_float(0, 2, 0.0f);
  plugin.Shutdown();
}

ETS2AP_TEST(InputPluginRejectsUnsafeCommandAndStaysNeutral) {
  FakeInputSdk sdk;
  InputPlugin plugin;
  std::uint64_t now_ns = 5'000'000'000ULL;
  InputPluginOptions options{};
  options.now_ns = [&] { return std::optional<std::uint64_t>{now_ns}; };
  CHECK_EQ(plugin.Init(SCS_INPUT_VERSION_1_00, sdk.params(), options), SCS_RESULT_ok);

  CHECK(!plugin.SetCommand({0.1f, 1.1f, 0.0f, true, now_ns}));
  scs_input_event_t event{};
  CHECK_EQ(sdk.registration.device.input_event_callback(&event,
      SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame, sdk.registration.device.callback_context),
      SCS_RESULT_ok);
  CHECK_EQ(event.input_index, 0u);
  CHECK_NEAR(event.value_float.value, 0.0, 0.0001);

  CHECK(!plugin.SetCommand({0.1f, 0.0f, 0.0f, true, now_ns + 1}));
  CHECK(!plugin.SetCommand({0.1f, 0.0f, 0.0f, true, now_ns - 100'000'001ULL}));
  CHECK(!plugin.SetCommand({0.1f, 0.0f, 0.0f, false, now_ns}));
  CHECK(plugin.SetCommand({0.0f, 0.0f, 0.0f, false, now_ns}));
  CHECK_EQ(sdk.registration.device.input_event_callback(&event, 0,
      sdk.registration.device.callback_context), SCS_RESULT_ok);
  CHECK_NEAR(event.value_float.value, 0.0, 0.0001);
  CHECK_EQ(sdk.registration.device.input_event_callback(&event, 0,
      sdk.registration.device.callback_context), SCS_RESULT_ok);
  CHECK_NEAR(event.value_float.value, 0.0, 0.0001);
  ++now_ns;
  CHECK(plugin.SetCommand({0.25f, 0.0f, 0.0f, true, now_ns}));
  CHECK(!plugin.SetCommand({0.5f, 0.0f, 0.0f, true, now_ns}));
  CHECK(!plugin.SetCommand({0.5f, 0.0f, 0.0f, true, now_ns - 1}));
  CHECK_EQ(sdk.registration.device.input_event_callback(&event,
      SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame, sdk.registration.device.callback_context),
      SCS_RESULT_ok);
  CHECK_NEAR(event.value_float.value, 0.0, 0.0001);
  plugin.Shutdown();
}

ETS2AP_TEST(InputExportsStartAndStopTheControlHost) {
  FakeInputSdk sdk;
  CHECK(!ets2ap::plugin::GlobalControlHost().IsRunning());
  CHECK_EQ(scs_input_init(SCS_INPUT_VERSION_1_00, sdk.params()), SCS_RESULT_ok);
  CHECK(ets2ap::plugin::GlobalControlHost().IsRunning());
  scs_input_shutdown();
  CHECK(!ets2ap::plugin::GlobalControlHost().IsRunning());
}
}  // namespace

ETS2AP_TEST_MAIN()
