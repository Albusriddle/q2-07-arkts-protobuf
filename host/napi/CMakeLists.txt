cmake_minimum_required(VERSION 3.16)
project(entry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_EXTENSIONS OFF)

# Canonical NAPI + decode sources live in app/; the shared decoder lives in
# src/host.  This monorepo build deliberately compiles both.  An app-only
# checkout is not buildable in V0.1 (see app/entry/src/main/cpp/CMakeLists.txt).
get_filename_component(Q207_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT EXISTS "${Q207_REPO_ROOT}/src/host/decoder/pod.h")
  message(FATAL_ERROR
    "V0.1 requires the monorepo checkout with src/host/**. "
    "Expected Q207_REPO_ROOT=${Q207_REPO_ROOT}")
endif()
include("${Q207_REPO_ROOT}/src/host/decoder/decoder_sources.cmake")

add_library(entry SHARED
  "${Q207_REPO_ROOT}/app/entry/src/main/cpp/entry.cpp"
  "${Q207_REPO_ROOT}/app/entry/src/main/cpp/decode_napi.cpp"
  ${Q207_DECODER_SOURCES}
)
target_include_directories(entry PRIVATE ${Q207_DECODER_INCLUDE_DIRS})
target_link_libraries(entry PUBLIC libace_napi.z.so)
