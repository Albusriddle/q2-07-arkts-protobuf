cmake_minimum_required(VERSION 3.16)
project(entry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_EXTENSIONS OFF)

# The canonical NAPI source lives in the app module (so an app checkout is
# self-contained).  The host build reuses that exact file; there is no copy.
add_library(entry SHARED ${CMAKE_CURRENT_SOURCE_DIR}/../../app/entry/src/main/cpp/entry.cpp)
target_link_libraries(entry PUBLIC libace_napi.z.so)
