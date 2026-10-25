# Edited by: OpenAI-Codex_Responsible: Source_Code
add_library(ets2ap_plugin_glue STATIC src/telemetry_plugin.cpp src/input_plugin.cpp src/control_host.cpp
  src/control_pipe_server.cpp src/gameframe_capture.cpp)
target_include_directories(ets2ap_plugin_glue PUBLIC include)
target_include_directories(ets2ap_plugin_glue SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/../third_party/scs_sdk/include)
target_link_libraries(ets2ap_plugin_glue PUBLIC ets2ap_telemetry_core ets2ap_input_core
  advapi32 d3d11 dxgi user32)

add_library(ets2ap_plugin SHARED src/exports.cpp ets2ap_plugin.def)
target_link_libraries(ets2ap_plugin PRIVATE ets2ap_plugin_glue)
set_target_properties(ets2ap_plugin PROPERTIES OUTPUT_NAME ets2ap_plugin)

add_executable(ets2ap_plugin_tests tests/plugin_tests.cpp tests/control_host_tests.cpp src/exports.cpp)
target_link_libraries(ets2ap_plugin_tests PRIVATE ets2ap_plugin_glue ets2ap_test_support)
add_test(NAME plugin_tests COMMAND ets2ap_plugin_tests)
