# hvigor 入口：由 externalNativeOptions 调用（见 app/entry/build-profile.json5）
#
# V0.1 KNOWN LIMITATION: this target compiles the shared decoder from
# <repo>/src/host/**.  The app module alone is no longer buildable; build from the
# monorepo checkout.  CMake fails early with a readable error otherwise.
cmake_minimum_required(VERSION 3.16)
project(entry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(Q207_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
if(NOT EXISTS "${Q207_REPO_ROOT}/src/host/decoder/pod.h")
  message(FATAL_ERROR
    "V0.1 requires the monorepo checkout with src/host/**. "
    "Expected Q207_REPO_ROOT=${Q207_REPO_ROOT}; app-only checkout is not supported.")
endif()
include("${Q207_REPO_ROOT}/src/host/decoder/decoder_sources.cmake")

set(NAPI_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/entry.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/decode_napi.cpp"
)
add_library(entry SHARED ${NAPI_SOURCES} ${Q207_DECODER_SOURCES})
target_include_directories(entry PRIVATE ${Q207_DECODER_INCLUDE_DIRS})
target_link_libraries(entry PUBLIC libace_napi.z.so)
