#!/usr/bin/env bash
# Host-only ASan/UBSan build of the core test executable.
# Requires clang++ or g++; on Windows use a POSIX toolchain (CI uses Ubuntu).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
mkdir -p build/host
CXX="${CXX:-}"
if [ -z "$CXX" ]; then
  # Prefer the known-good MSYS2 clang64 toolchain on Windows; a bare
  # clang++ from another distribution may lack the ASan runtime.
  if [ -x /c/msys64/clang64/bin/clang++.exe ]; then CXX=/c/msys64/clang64/bin/clang++.exe;
  elif command -v clang++ >/dev/null 2>&1; then CXX=clang++;
  elif command -v g++ >/dev/null 2>&1; then CXX=g++;
  elif [ -x /c/msys64/mingw64/bin/g++.exe ]; then CXX=/c/msys64/mingw64/bin/g++.exe;
  else echo "ASan: clang++/g++ not found" >&2; exit 2; fi
fi
exec "$CXX" -std=c++17 -O1 -g -fno-omit-frame-pointer \
  -fsanitize=address,undefined \
  -DQ207_ASAN -I app/entry/src/main/cpp -I src/host/tests/napi_shim -I src \
  -o build/host/host_tests_asan.exe \
  src/host/common/status.cpp src/host/common/sha256.cpp src/host/decoder/wire_reader.cpp \
  src/host/decoder/conformance.cpp src/host/decoder/decode_user.cpp src/host/decoder/norm.cpp \
  src/host/decoder/map_order.cpp src/host/decoder/object_model.cpp src/host/tape/tape.cpp \
  src/host/tests/test_wire.cpp src/host/tests/test_vectors.cpp src/host/tests/test_tape.cpp \
  src/host/tests/test_decode.cpp src/host/tests/test_object_model.cpp src/host/tests/main.cpp \
  "$@"
