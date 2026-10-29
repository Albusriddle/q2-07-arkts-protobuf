cmake_minimum_required(VERSION 3.16)
project(entry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(entry SHARED entry.cpp)
target_link_libraries(entry PUBLIC libace_napi.z.so)
