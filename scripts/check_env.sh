#!/usr/bin/env bash
# V0 environment probe. Missing protoc/buf/board/phone are recorded, not fatal.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON="${PYTHON:-}"
if [ -z "$PYTHON" ]; then
  if command -v python >/dev/null 2>&1 && python -c 'import sys' >/dev/null 2>&1; then
    PYTHON=python
  elif command -v python3 >/dev/null 2>&1 && python3 -c 'import sys' >/dev/null 2>&1; then
    PYTHON=python3
  else
    PYTHON=python
  fi
fi
DEVECO="${DEVECO_HOME:-/d/Program Files/Huawei/DevEco Studio}"
SDK="$DEVECO/sdk/default/openharmony"
NDK="$SDK/native"
EVIDENCE="$ROOT/evidence"
mkdir -p "$EVIDENCE" "$ROOT/build"

find_tool() {
  local name="$1"; shift
  if command -v "$name" >/dev/null 2>&1; then
    command -v "$name"
    return 0
  fi
  local p
  for p in "$@"; do
    if [ -x "$p" ] || [ -f "$p" ]; then
      printf '%s\n' "$p"
      return 0
    fi
  done
  return 1
}

CMAKE="$(find_tool cmake "$NDK/build-tools/cmake/bin/cmake.exe" || true)"
NINJA="$(find_tool ninja "$NDK/build-tools/cmake/bin/ninja.exe" || true)"
HDC="$(find_tool hdc "$SDK/toolchains/hdc.exe" || true)"
HVIGORW="$(find_tool hvigorw "$DEVECO/tools/hvigor/bin/hvigorw" "$DEVECO/tools/hvigor/bin/hvigorw.bat" || true)"
PROTOC="$(find_tool protoc || true)"
BUF="$(find_tool buf || true)"
CLANG="$NDK/llvm/bin/clang++.exe"

API_VERSION="unknown"
if [ -f "$SDK/ets/oh-uni-package.json" ]; then
  API_VERSION="$(grep -o '"apiVersion"[[:space:]]*:[[:space:]]*"[^"]*"' "$SDK/ets/oh-uni-package.json" | head -1 | sed 's/.*"\([^"]*\)"$/\1/')"
fi

CLANG_VERSION="missing"
if [ -x "$CLANG" ]; then
  CLANG_VERSION="$("$CLANG" --version 2>/dev/null | head -1 || true)"
fi

BOARD_STATUS="blocked: hdc not found"
BOARD_TARGETS=""
if [ -n "$HDC" ]; then
  BOARD_TARGETS="$("$HDC" list targets 2>&1 || true)"
  if [ -z "$BOARD_TARGETS" ] || printf '%s' "$BOARD_TARGETS" | grep -qiE 'empty|not found|no target|error'; then
    BOARD_STATUS="no target"
  else
    BOARD_STATUS="target(s) listed"
  fi
fi

REQUIRED_MISSING=()
[ -d "$DEVECO" ] || REQUIRED_MISSING+=("DevEco Studio")
[ -d "$SDK" ] || REQUIRED_MISSING+=("OpenHarmony SDK")
[ -x "$CLANG" ] || REQUIRED_MISSING+=("NDK clang++")
[ -n "$CMAKE" ] || REQUIRED_MISSING+=("cmake")
[ -n "$NINJA" ] || REQUIRED_MISSING+=("ninja")

OPTIONAL_MISSING=()
[ -n "$HDC" ] || OPTIONAL_MISSING+=("hdc")
[ -n "$HVIGORW" ] || OPTIONAL_MISSING+=("hvigorw")
[ -n "$PROTOC" ] || OPTIONAL_MISSING+=("protoc")
[ -n "$BUF" ] || OPTIONAL_MISSING+=("buf")
[ "$BOARD_STATUS" = "target(s) listed" ] || OPTIONAL_MISSING+=("target board")

printf 'V0 environment probe\n'
printf '  DevEco: %s\n' "$DEVECO"
printf '  SDK: %s (API %s)\n' "$SDK" "$API_VERSION"
printf '  NDK clang: %s\n' "$CLANG_VERSION"
printf '  cmake: %s\n' "${CMAKE:-MISSING}"
printf '  ninja: %s\n' "${NINJA:-MISSING}"
printf '  hdc: %s (%s)\n' "${HDC:-MISSING}" "$BOARD_STATUS"
printf '  hvigorw: %s\n' "${HVIGORW:-MISSING}"
printf '  protoc: %s\n' "${PROTOC:-MISSING}"
printf '  buf: %s\n' "${BUF:-MISSING}"
if [ "${#REQUIRED_MISSING[@]}" -gt 0 ]; then
  printf '  required missing: %s\n' "${REQUIRED_MISSING[*]}"
else
  printf '  required missing: none\n'
fi
if [ "${#OPTIONAL_MISSING[@]}" -gt 0 ]; then
  printf '  optional missing: %s\n' "${OPTIONAL_MISSING[*]}"
else
  printf '  optional missing: none\n'
fi

export V0_ROOT="$ROOT"
export V0_DEVECO="$DEVECO" V0_SDK="$SDK" V0_NDK="$NDK" V0_API="$API_VERSION"
export V0_CLANG="$CLANG_VERSION" V0_CMAKE="${CMAKE:-}" V0_NINJA="${NINJA:-}"
export V0_HDC="${HDC:-}" V0_HVIGORW="${HVIGORW:-}" V0_PROTOC="${PROTOC:-}" V0_BUF="${BUF:-}"
export V0_BOARD_STATUS="$BOARD_STATUS" V0_BOARD_TARGETS="$BOARD_TARGETS"
export V0_REQ_MISSING="${REQUIRED_MISSING[*]:-}"
export V0_OPT_MISSING="${OPTIONAL_MISSING[*]:-}"
"$PYTHON" - <<'PY'
import json, os, datetime
def split(k):
    v=os.environ.get(k,'').strip()
    return v.split() if v else []
out={
  'timestamp': datetime.datetime.now(datetime.timezone.utc).isoformat(),
  'deveco': os.environ.get('V0_DEVECO'),
  'sdk': os.environ.get('V0_SDK'),
  'api_version': os.environ.get('V0_API'),
  'clang': os.environ.get('V0_CLANG'),
  'tools': {
    'cmake': os.environ.get('V0_CMAKE') or None,
    'ninja': os.environ.get('V0_NINJA') or None,
    'hdc': os.environ.get('V0_HDC') or None,
    'hvigorw': os.environ.get('V0_HVIGORW') or None,
    'protoc': os.environ.get('V0_PROTOC') or None,
    'buf': os.environ.get('V0_BUF') or None,
  },
  'board': {'status': os.environ.get('V0_BOARD_STATUS'), 'targets': os.environ.get('V0_BOARD_TARGETS')},
  'missing_required': split('V0_REQ_MISSING'),
  'missing_optional': split('V0_OPT_MISSING'),
}
out['status']='PASS' if not out['missing_required'] else 'PARTIAL'
path=os.path.join(os.environ.get('V0_ROOT', os.getcwd()),'evidence','env_check.json')
with open(path,'w',encoding='utf-8') as f: json.dump(out,f,ensure_ascii=False,indent=2)
print('wrote',path)
PY

if [ "${#REQUIRED_MISSING[@]}" -gt 0 ]; then
  exit 2
fi
exit 0
