#!/usr/bin/env bash
# 一键构建 HAP（含 native .so）：hvigor 通过 externalNativeOptions 自己调 CMake/Ninja。
# 前置：DevEco Studio + SDK/NDK 已安装（本机 API 24 / clang 15.0.4）。
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVECO="${DEVECO_HOME:-/d/Program Files/Huawei/DevEco Studio}"
export DEVECO_SDK_HOME="${DEVECO_SDK_HOME:-$DEVECO/sdk}"
export HOS_SDK_HOME="${HOS_SDK_HOME:-$DEVECO/sdk}"
export NODE_HOME="${NODE_HOME:-/c/Program Files/nodejs}"
export PATH="$NODE_HOME:$DEVECO/jbr/bin:$DEVECO/tools/hvigor/bin:$PATH"

cd "$ROOT/app"
hvigorw assembleHap --mode module -p product=default -p buildMode=debug --no-daemon
HAP="$ROOT/app/entry/build/default/outputs/default/entry-default-unsigned.hap"
[ -f "$HAP" ] || { echo "HAP not found: $HAP" >&2; exit 1; }
echo "HAP: $HAP ($(stat -c%s "$HAP") bytes, unsigned)"
