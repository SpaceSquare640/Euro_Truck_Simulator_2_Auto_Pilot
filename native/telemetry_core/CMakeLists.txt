# Edited by: OpenAI-Codex_Responsible: Source_Code
add_library(ets2ap_telemetry_core STATIC
  src/convert.cpp
  src/state.cpp
  src/seqlock.cpp
  src/shared_mapping.cpp
)
target_include_directories(ets2ap_telemetry_core PUBLIC include)
target_link_libraries(ets2ap_telemetry_core PUBLIC Advapi32)

add_executable(telemetry_core_tests tests/core_state_tests.cpp tests/seqlock_tests.cpp)
target_link_libraries(telemetry_core_tests PRIVATE ets2ap_telemetry_core ets2ap_test_support)
add_test(NAME telemetry_core_tests COMMAND telemetry_core_tests)

add_executable(telemetry_mapping_tests tests/mapping_tests.cpp)
target_link_libraries(telemetry_mapping_tests PRIVATE ets2ap_telemetry_core ets2ap_test_support)
add_test(NAME telemetry_mapping_tests COMMAND telemetry_mapping_tests)
