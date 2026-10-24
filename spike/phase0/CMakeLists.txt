# Edited by: OpenAI-Codex_Responsible: Source_Code
cmake_minimum_required(VERSION 3.25)
project(ets2ap_phase0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT MSVC)
  message(FATAL_ERROR "Phase 0 requires Windows x64 and MSVC")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(SCS_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/scs_sdk/include")
file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(ets2ap_spike SHARED ${PLUGIN_SOURCES} ets2ap_spike.def)
target_include_directories(ets2ap_spike PRIVATE "${SCS_INCLUDE}")
target_compile_options(ets2ap_spike PRIVATE /W4 /WX /permissive- /utf-8 /EHsc /Zc:__cplusplus)
target_link_libraries(ets2ap_spike PRIVATE Shell32 Ole32)

add_executable(phase0_host_harness tests/host_harness.cpp)
target_include_directories(phase0_host_harness PRIVATE "${SCS_INCLUDE}")
target_compile_options(phase0_host_harness PRIVATE /W4 /WX /permissive- /utf-8 /EHsc /Zc:__cplusplus)

enable_testing()
add_test(NAME phase0_host_harness COMMAND phase0_host_harness "$<TARGET_FILE:ets2ap_spike>")
