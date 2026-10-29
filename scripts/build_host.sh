#!/usr/bin/env bash
# Q2-07 host vertical slice -- one-command build + test + bench + differential + evidence.
#
#   bash repo/scripts/build_host.sh            # everything
#   bash repo/scripts/build_host.sh --no-npm   # skip the protobufjs differential
#
# HOST-ONLY: builds with a NATIVE host compiler (g++ / clang++ / MSVC).  This is
# NOT an OpenHarmony NDK cross-build and produces no device numbers.
#
# Exit codes: 0 = all steps passed; non-zero = the first failing step.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

BUILD="$ROOT/build/host"
LOGS="$BUILD/logs"
EVID="$ROOT/evidence/host"
mkdir -p "$BUILD" "$LOGS" "$EVID"

TOTAL=0
FAILED=0
STEP_LOG="$BUILD/steps.txt"
: > "$STEP_LOG"
STEPS=()

say() { printf '\n\033[1m== %s\033[0m\n' "$*"; }

# run_logged <logfile> <command...>  -- no `tee` here: this host shell does not ship it
run_logged() {
  local log="$1"; shift
  "$@" >"$log" 2>&1
  local rc=$?
  cat "$log"
  return $rc
}
now_ms() {
  local t
  t="$(date +%s%3N 2>/dev/null || true)"
  case "$t" in
    ''|*[!0-9]*) python -c 'import time;print(int(time.time()*1000))' ;;
    *) printf '%s\n' "$t" ;;
  esac
}

# run_step <name> <command-description> <function...>
# records name / command description / exit code / duration into the manifest
run_step() {
  local name="$1"; shift
  local desc="$1"; shift
  local t0 t1 rc
  say "$name"
  t0=$(now_ms)
  if "$@"; then rc=0; else rc=$?; fi
  t1=$(now_ms)
  local ms=$((t1 - t0))
  printf '%s\t%s\t%d\t%d\n' "$name" "$desc" "$rc" "$ms" >> "$STEP_LOG"
  if [ "$rc" -ne 0 ]; then
    FAILED=1
    printf '\033[31m[FAILED] %s (exit %d)\033[0m\n' "$name" "$rc"
    exit "$rc"
  fi
  printf '[ok] %s (%d ms)\n' "$name" "$ms"
}

# ---------------------------------------------------------------------------
# 1. compiler detection: a *native host* compiler is mandatory.
# ---------------------------------------------------------------------------
CC_KIND=""
CC_NAME=""
CC_VERSION=""
VC_ENV_READY=0

probe_native() {  # $1 = compiler command
  local tmp
  tmp="$(mktemp -d 2>/dev/null || echo "$BUILD/tmp_probe")"
  mkdir -p "$tmp"
  printf 'int main(){return 0;}\n' > "$tmp/t.cpp"
  "$1" -std=c++17 "$tmp/t.cpp" -o "$tmp/t.exe" >/dev/null 2>&1
  local ok=$?
  rm -rf "$tmp"
  return $ok
}

setup_msvc_env() {
  [ "$VC_ENV_READY" = "1" ] && return 0
  local vcvars="" cand
  for cand in \
    "/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" \
    "/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat" \
    "/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/vars64.bat"; do
    [ -f "$cand" ] && { vcvars="$cand"; break; }
  done
  [ -z "$vcvars" ] && return 1
  local winvc bat out
  winvc="$(printf '%s' "$vcvars" | sed -e 's#^/c/#C:/#')"
  bat="$BUILD/vcenv.bat"
  out="$BUILD/vcenv.txt"
  printf '@echo off\r\ncall "%s" >nul 2>&1\r\nset\r\n' "$winvc" > "$bat"
  cmd //c "$(printf '%s' "$bat" | sed -e 's#^/\([a-zA-Z]\)/#\1:/#' -e 's#/#\\#g')" > "$out" 2>/dev/null
  [ -s "$out" ] || return 1
  local line bindir
  while IFS= read -r line; do
    line="${line%$'\r'}"
    case "$line" in
      INCLUDE=*|LIB=*) export "${line%%=*}=${line#*=}" ;;
      PATH=*) bindir="$(printf '%s' "${line#*=}" | tr ';' '\n' | tr -d '\r' | head -1)" ;;
    esac
  done < "$out"
  bindir="$(printf '%s' "$bindir" | sed -e 's#^[A-Za-z]:#/c#' -e 's#\\#/#g' | tr 'A-Z' 'a-z')"
  [ -x "$bindir/cl.exe" ] || return 1
  export PATH="$bindir:$PATH"
  VC_ENV_READY=1
  return 0
}

detect_compiler() {
  say "toolchain detection (native host compiler, not the OH NDK)"
  if [ -n "${CXX:-}" ] && probe_native "$CXX"; then
    CC_KIND="generic"; CC_NAME="$CXX"; CC_VERSION="$("$CXX" --version 2>&1 | head -1)"
  elif command -v g++ >/dev/null 2>&1 && probe_native g++; then
    CC_KIND="gcc"; CC_NAME="g++"; CC_VERSION="$(g++ --version 2>&1 | head -1)"
  elif command -v clang++ >/dev/null 2>&1 && probe_native clang++; then
    CC_KIND="clang"; CC_NAME="clang++"; CC_VERSION="$(clang++ --version 2>&1 | head -1)"
  elif setup_msvc_env && probe_native cl; then
    CC_KIND="msvc"; CC_NAME="cl"
    # The banner is localised; extract the version number instead of the text.
    CC_VERSION="MSVC cl $(cl 2>&1 | tr -d '\r' | grep -oE '19\.[0-9]+\.[0-9]+' | head -1)"
  else
    printf '\033[31mNo native host C++ compiler found.\033[0m\n' >&2
    printf 'Tried: $CXX, g++, clang++ (with a working stdlib), MSVC via vcvars64.bat.\n' >&2
    printf 'The OpenHarmony NDK clang is deliberately NOT used: this slice must run\n' >&2
    printf 'natively on the host, not cross-compile to the device.\n' >&2
    exit 90
  fi
  printf 'compiler: %s  (%s)\n  %s\n' "$CC_NAME" "$CC_KIND" "$CC_VERSION"
}

# compile <exe-name> <sources...>
compile_bin() {
  local exe="$1"; shift
  if [ "$CC_KIND" = "msvc" ]; then
    # MSVC needs '/'-prefixed options to be written with '-': MSYS rewrites them.
    # Paths stay relative: cl.exe cannot parse MSYS-style absolute paths (/d/...).
    cl -nologo -D_CRT_SECURE_NO_WARNINGS -std:c++17 -EHsc -O2 -W4 -I src \
      "-Fo:build/host/" "-Fe:build/host/$exe.exe" "$@" >"$LOGS/build_$exe.log" 2>&1
    local rc=$?
    grep -Ei "error|warning" "$LOGS/build_$exe.log" || true
    return $rc
  elif [ "$CC_KIND" = "gcc" ] || [ "$CC_KIND" = "clang" ] || [ "$CC_KIND" = "generic" ]; then
    "$CC_NAME" -std=c++17 -O2 -Wall -Wextra -I src -o "$BUILD/$exe.exe" "$@" \
      >"$LOGS/build_$exe.log" 2>&1
    local rc=$?
    grep -Ei "error|warning" "$LOGS/build_$exe.log" || true
    return $rc
  fi
  return 1
}

# ---------------------------------------------------------------------------
# 2. build
# ---------------------------------------------------------------------------
SHARED="src/host/common/status.cpp src/host/common/sha256.cpp src/host/decoder/wire_reader.cpp \
src/host/decoder/conformance.cpp src/host/decoder/decode_user.cpp src/host/decoder/norm.cpp \
src/host/tape/tape.cpp"

build_all() {
  detect_compiler
  say "build (host_tests / host_decode_dump / host_bench)"
  # shellcheck disable=SC2086
  compile_bin host_tests $SHARED src/host/tests/test_wire.cpp src/host/tests/test_vectors.cpp \
    src/host/tests/test_tape.cpp src/host/tests/test_decode.cpp src/host/tests/main.cpp \
    || return 1
  # shellcheck disable=SC2086
  compile_bin host_decode_dump $SHARED src/host/tools/host_decode_dump.cpp || return 1
  # shellcheck disable=SC2086
  compile_bin host_bench $SHARED src/host/bench/host_bench.cpp || return 1
  ls -l "$BUILD"/host_tests.exe "$BUILD"/host_decode_dump.exe "$BUILD"/host_bench.exe
  return 0
}

# ---------------------------------------------------------------------------
# 3. tests
# ---------------------------------------------------------------------------
run_tests() {
  local out="$LOGS/tests.log"
  "$BUILD/host_tests.exe" >"$out" 2>&1
  local rc=$?
  tail -3 "$out"
  grep -E "38-vector golden|TESTS PASSED" "$out" || true
  return $rc
}

# ---------------------------------------------------------------------------
# 4. data cards
# ---------------------------------------------------------------------------
gen_cards() {
  python "$ROOT/bench/gen_cards.py" --out "$ROOT/bench/data" >"$LOGS/cards.log" 2>&1
  local rc=$?
  tail -20 "$LOGS/cards.log"
  return $rc
}

# ---------------------------------------------------------------------------
# 5. bench -> evidence/host/host_bench.csv
# ---------------------------------------------------------------------------
BENCH_CSV="$EVID/host_bench.csv"
run_bench() {
  : > "$BENCH_CSV"
  local rc=0
  "$BUILD/host_bench.exe" --card bench/data/s1_user_1kb.bin --dataset s1_user_1kb \
    --reps 5 --csv "$BENCH_CSV" --csv-header || rc=$?
  [ $rc -ne 0 ] && return $rc
  "$BUILD/host_bench.exe" --card bench/data/s3_100mb.bin --dataset s3_100mb \
    --reps 5 --csv "$BENCH_CSV" || rc=$?
  [ $rc -ne 0 ] && return $rc
  wc -l "$BENCH_CSV"
  return 0
}

# ---------------------------------------------------------------------------
# 6. differential vs protobufjs 7.6.5 (or the documented Python fallback)
# ---------------------------------------------------------------------------
DIFF_MODE=""
run_differential() {
  local ts_dir="$ROOT/src/host/ts"
  local corpus="$EVID/corpus"
  local cpp="$EVID/corpus_cpp"
  rm -rf "$cpp"
  mkdir -p "$cpp"

  # Degraded baseline: an independent Python encoder + expected object graph.
  # It always overwrites evidence/host/differential.json so a stale report from a
  # previous run can never be mistaken for this run's result.
  python_fallback() {
    DIFF_MODE="python-fallback"
    rm -f "$EVID/differential.json"
    say "differential: DEGRADED to the Python golden baseline (protobufjs unavailable/disabled)"
    python "$ROOT/src/host/tools/py_fallback_diff.py" --work "$BUILD/pyfallback" \
      --cpp-dump "$BUILD/host_decode_dump.exe" --corpus "$cpp" \
      --report "$EVID/differential.json" >"$LOGS/py_fallback.log" 2>&1
    local rc=$?
    cat "$LOGS/py_fallback.log"
    return $rc
  }

  if [ "${NO_NPM:-0}" = "1" ]; then
    printf 'protobufjs differential disabled by --no-npm\n' >&2
    python_fallback
    return $?
  fi

  if [ ! -d "$ts_dir/node_modules/protobufjs" ]; then
    say "installing protobufjs 7.6.5 (npm)"
    ( cd "$ts_dir" && npm install --no-audit --no-fund ) >"$LOGS/npm_install.log" 2>&1
    if [ ! -d "$ts_dir/node_modules/protobufjs" ]; then
      python_fallback
      return $?
    fi
  fi

  DIFF_MODE="protobufjs-7.6.5"
  say "differential corpus (protobufjs 7.6.5)"
  ( cd "$ts_dir" && node gen_corpus.mjs "$corpus" ) >"$LOGS/gen_corpus.log" 2>&1
  local rc=$?
  cat "$LOGS/gen_corpus.log"
  [ $rc -ne 0 ] && return $rc

  say "decode corpus with C++"
  "$BUILD/host_decode_dump.exe" --corpus "$corpus" --out "$cpp" --meta "$cpp/meta.json" \
    >"$LOGS/decode_corpus.log" 2>&1
  rc=$?
  cat "$LOGS/decode_corpus.log"
  [ $rc -ne 0 ] && return $rc

  say "differential: object graph / bytes / materializer"
  ( cd "$ts_dir" && node differential.mjs --corpus "$corpus" --cpp "$cpp" \
      --out "$EVID/differential.json" ) >"$LOGS/differential.log" 2>&1
  rc=$?
  cat "$LOGS/differential.log"
  [ $rc -ne 0 ] && return $rc

  say "materializer: big tape (S1 card frame 0)"
  rm -rf "$BUILD/s1dump"
  mkdir -p "$BUILD/s1dump"
  "$BUILD/host_decode_dump.exe" --framed bench/data/s1_user_1kb.bin --out "$BUILD/s1dump" \
    --stem s1_frame0 >"$LOGS/s1dump.log" 2>&1
  rc=$?
  [ $rc -ne 0 ] && { cat "$LOGS/s1dump.log"; return $rc; }
  ( cd "$ts_dir" && node materializer_check.mjs --tape "$BUILD/s1dump/s1_frame0.tape.bin" \
      --norm "$BUILD/s1dump/s1_frame0.norm.json" --out "$EVID/materializer_check.json" ) \
    >"$LOGS/materializer_check.log" 2>&1
  rc=$?
  cat "$LOGS/materializer_check.log"
  [ $rc -ne 0 ] && return $rc
  return 0
}

# ---------------------------------------------------------------------------
# 7. evidence
# ---------------------------------------------------------------------------
collect_evidence() {
  local t0 t1
  t0=$(now_ms)
  python "$ROOT/src/host/tools/collect_evidence.py" \
    --repo "$ROOT" \
    --steps "$STEP_LOG" \
    --bench-csv "$BENCH_CSV" \
    --diff-report "$EVID/differential.json" \
    --materializer-report "$EVID/materializer_check.json" \
    --diff-mode "${DIFF_MODE:-none}" \
    --compiler-kind "${CC_KIND:-unknown}" \
    --compiler-name "${CC_NAME:-unknown}" \
    --compiler-version "${CC_VERSION:-unknown}" \
    --out "$ROOT/evidence/host_slice.json" >"$LOGS/evidence.log" 2>&1
  local rc=$?
  cat "$LOGS/evidence.log"
  [ $rc -ne 0 ] && return $rc
  t1=$(now_ms)
  printf 'evidence written: evidence/host_slice.json (%d ms)\n' $((t1 - t0))
  return 0
}

# ---------------------------------------------------------------------------
NO_NPM=0
for arg in "$@"; do
  case "$arg" in
    --no-npm) NO_NPM=1 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) printf 'unknown argument: %s\n' "$arg" >&2; exit 2 ;;
  esac
done

run_step "build" "native host compiler: cl/g++/clang++ over src/host/**, -O2, 3 binaries" build_all
run_step "unit-tests" "build/host/host_tests.exe (wire + 38 golden vectors + tape + decode)" run_tests
run_step "data-cards" "python bench/gen_cards.py --out bench/data" gen_cards
run_step "bench" "host_bench.exe --card bench/data/{s1_user_1kb,s3_100mb}.bin --reps 5 -> evidence/host/host_bench.csv" run_bench
run_step "differential" "differential vs protobufjs 7.6.5 (gen_corpus + differential + materializer_check), Python golden fallback when unavailable" run_differential
run_step "evidence" "python src/host/tools/collect_evidence.py -> evidence/host_slice.json" collect_evidence

say "DONE (HOST-ONLY)"
printf 'tests     : %s\n' "$(grep -h 'TESTS PASSED' "$LOGS/tests.log" | tail -1)"
printf 'golden    : %s\n' "$(grep -h '38-vector golden' "$LOGS/tests.log" | tail -1 | tr -s ' ')"
printf 'bench     : evidence/host/host_bench.csv (%s rows)\n' "$(wc -l < "$BENCH_CSV")"
printf 'diff mode : %s\n' "${DIFF_MODE:-none}"
printf 'evidence  : evidence/host_slice.json\n'
printf 'NOTE: every number above is HOST-ONLY. No device performance is claimed.\n'
exit 0
