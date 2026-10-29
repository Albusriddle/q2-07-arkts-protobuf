#!/usr/bin/env bash
# Build the V0 minimal slice: arm64-v8a libentry.so, then try an empty HAP.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVIDENCE="$ROOT/evidence"
BUILD_DIR="$ROOT/build/napi"
mkdir -p "$EVIDENCE" "$BUILD_DIR"

DEVECO="${DEVECO_HOME:-/d/Program Files/Huawei/DevEco Studio}"
SDK="$DEVECO/sdk/default/openharmony"
NDK="$SDK/native"
CMAKE="$NDK/build-tools/cmake/bin/cmake.exe"
NINJA="$NDK/build-tools/cmake/bin/ninja.exe"
HDC="$SDK/toolchains/hdc.exe"
HVIGORW="$DEVECO/tools/hvigor/bin/hvigorw"
NODE_BIN="$DEVECO/tools/node/bin"
NODE_HOME_DEFAULT="/c/Program Files/nodejs"
export NODE_HOME="${NODE_HOME:-$NODE_HOME_DEFAULT}"
export PATH="$NODE_HOME:$DEVECO/jbr/bin:$PATH"
export DEVECO_SDK_HOME="$DEVECO/sdk"
export HOS_SDK_HOME="$DEVECO/sdk"

export PATH="$NDK/build-tools/cmake/bin:$NDK/llvm/bin:$NODE_BIN:$PATH"

CMD_RECORDS=()
BLOCKED=()

record_cmd() {
  local name="$1"; local log="$2"; shift 2
  "$@" >"$log" 2>&1
  local ec=$?
  CMD_RECORDS+=("$name|$ec|$(basename "$log")")
  return $ec
}

CHECK_LOG="$EVIDENCE/check_env.log"
"$ROOT/scripts/check_env.sh" >"$CHECK_LOG" 2>&1
CHECK_EXIT=$?
CMD_RECORDS+=("check_env|$CHECK_EXIT|check_env.log")

if [ ! -x "$CMAKE" ] || [ ! -x "$NINJA" ]; then
  BLOCKED+=("cmake/ninja missing; cannot build .so")
  python "$ROOT/scripts/record_build.py" --status FAIL --hap-status "blocked: cmake/ninja missing" --hdc-status "unknown" --command "${CMD_RECORDS[@]}" --blocked "${BLOCKED[@]}" || true
  exit 1
fi

CONFIG_LOG="$EVIDENCE/cmake_configure.log"
record_cmd "cmake-configure" "$CONFIG_LOG" \
  "$CMAKE" -S "$ROOT/host/napi" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/ohos.toolchain.cmake" \
  -DOHOS_ARCH=arm64-v8a -DOHOS_PLATFORM=OHOS -DOHOS_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release
CONFIG_EXIT=$?

BUILD_LOG="$EVIDENCE/cmake_build.log"
if [ "$CONFIG_EXIT" -eq 0 ]; then
  record_cmd "cmake-build" "$BUILD_LOG" "$CMAKE" --build "$BUILD_DIR" --target entry -j 4
  BUILD_EXIT=$?
else
  BUILD_EXIT=$CONFIG_EXIT
  : >"$BUILD_LOG"
fi

SO_PATH=""
if [ -f "$BUILD_DIR/libentry.so" ]; then
  SO_PATH="$BUILD_DIR/libentry.so"
  mkdir -p "$ROOT/app/entry/libs/arm64-v8a"
  cp "$SO_PATH" "$ROOT/app/entry/libs/arm64-v8a/libentry.so"
fi

HAP_FILE=""
HAP_STATUS="blocked: hvigorw not found"
if [ -x "$HVIGORW" ] && [ -f "$ROOT/app/build-profile.json5" ]; then
  HAP_LOG="$EVIDENCE/hap_build.log"
  (
    cd "$ROOT/app" || exit 1
    "$HVIGORW" assembleHap --mode module -p product=default -p buildMode=debug --no-daemon
  ) >"$HAP_LOG" 2>&1
  HAP_EXIT=$?
  CMD_RECORDS+=("hvigor-assembleHap|$HAP_EXIT|hap_build.log")
  HAP_FILE="$(find "$ROOT/app/entry/build" -name '*.hap' -type f 2>/dev/null | head -1 || true)"
  if [ "$HAP_EXIT" -eq 0 ] && [ -n "$HAP_FILE" ]; then
    HAP_STATUS="built unsigned: $HAP_FILE (no signingConfig; not installed)"
  elif [ "$HAP_EXIT" -eq 0 ]; then
    HAP_STATUS="hvigor exited 0 but no .hap found"
  else
    HAP_STATUS="failed: hvigor exit $HAP_EXIT (see evidence/hap_build.log)"
  fi
elif [ ! -x "$HVIGORW" ]; then
  HAP_FILE=""
HAP_STATUS="blocked: hvigorw not found"
else
  HAP_STATUS="blocked: app/build-profile.json5 not found"
fi

HDC_STATUS="blocked: hdc not found"
if [ -x "$HDC" ]; then
  HDC_OUT="$("$HDC" list targets 2>&1 || true)"
  printf '%s\n' "$HDC_OUT" >"$EVIDENCE/hdc_targets.log"
  if printf '%s' "$HDC_OUT" | grep -qiE 'empty|not found|no target|error'; then
    HDC_STATUS="blocked: hdc present but no target"
  elif [ -z "$HDC_OUT" ]; then
    HDC_STATUS="blocked: hdc returned empty target list"
  else
    HDC_STATUS="target(s) listed; not installed by this script"
  fi
fi

if [ -n "$SO_PATH" ]; then
  [ "$HAP_STATUS" = "built" ] && STATUS="PASS" || STATUS="PARTIAL"
else
  STATUS="FAIL"
fi

ARGS=(--status "$STATUS" --hap-status "$HAP_STATUS" --hdc-status "$HDC_STATUS")
[ -n "$SO_PATH" ] && ARGS+=(--so "$SO_PATH")
[ -f "$ROOT/app/entry/libs/arm64-v8a/libentry.so" ] && ARGS+=(--so "$ROOT/app/entry/libs/arm64-v8a/libentry.so")
[ -n "$HAP_FILE" ] && ARGS+=(--hap "$HAP_FILE")
for c in "${CMD_RECORDS[@]}"; do ARGS+=(--command "$c"); done
for b in "${BLOCKED[@]}"; do ARGS+=(--blocked "$b"); done

python "$ROOT/scripts/record_build.py" "${ARGS[@]}"

echo "status=$STATUS"
echo "so=${SO_PATH:-MISSING}"
echo "hap_status=$HAP_STATUS"
echo "hdc_status=$HDC_STATUS"
[ -n "$SO_PATH" ] && exit 0 || exit 1
