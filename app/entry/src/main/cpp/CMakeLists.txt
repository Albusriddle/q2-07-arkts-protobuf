# hvigor 入口：由 externalNativeOptions 调用（见 app/entry/build-profile.json5）
# 说明：entry.cpp 是 canonical NAPI 源；host/napi/CMakeLists.txt 反向引用它，
#       所以 app 单独构建时不再依赖 host/ 目录。
cmake_minimum_required(VERSION 3.16)
project(entry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NAPI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/entry.cpp")
if(NOT EXISTS "${NAPI_SRC}")
  message(FATAL_ERROR "NAPI source not found: ${NAPI_SRC}")
endif()

add_library(entry SHARED ${NAPI_SRC})
target_link_libraries(entry PUBLIC libace_napi.z.so)
