# hvigor 入口：由 externalNativeOptions 调用（见 app/entry/build-profile.json5）
# 说明：NAPI 源文件暂时仍放在 host/napi/entry.cpp（v0 的旁路构建也引用它），
#       本文件只负责把它接进 hvigor，避免"手工拷 .so"。
cmake_minimum_required(VERSION 3.16)
project(entry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NAPI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../host/napi/entry.cpp")
if(NOT EXISTS "${NAPI_SRC}")
  message(FATAL_ERROR "NAPI source not found: ${NAPI_SRC}")
endif()

add_library(entry SHARED ${NAPI_SRC})
target_link_libraries(entry PUBLIC libace_napi.z.so)
