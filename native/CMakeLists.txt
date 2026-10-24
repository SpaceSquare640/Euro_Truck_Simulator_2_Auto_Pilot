# Edited by: OpenAI-Codex_Responsible: Source_Code
cmake_minimum_required(VERSION 3.25)
project(ets2ap_native LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "ETS2 Auto Pilot native modules support Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc /Zc:__cplusplus)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

enable_testing()
add_subdirectory(test_support)
foreach(module telemetry_core plugin input_core)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${module}/CMakeLists.txt")
    add_subdirectory(${module})
  endif()
endforeach()
