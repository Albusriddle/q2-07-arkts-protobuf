#!/usr/bin/env bash
# inspect_so.sh - read-only ELF / NAPI inspection for an OpenHarmony native module.
#
# Uses the DevEco / OpenHarmony SDK LLVM binutils (llvm-readelf, llvm-nm).
# Read-only: this script never builds, strips, patches, copies or signs anything.
#
# Exit codes (every non-zero code means "not acceptable"):
#   0  PASS    the artifact was inspected and all required checks passed
#   1  FAIL    the artifact was inspected and at least one required check failed
#   2  ERROR   the artifact could not be inspected (file or toolchain missing)
#
# Usage:
#   bash scripts/inspect_so.sh [SO]
#   bash scripts/inspect_so.sh --strict [SO]     # promote warnings to failures
#
# Default SO: build/napi/libentry.so
# Env overrides: DEVECO_HOME, OHOS_SDK_ROOT
#
# Design notes
#   * `napi_module_register` is EXPECTED to be an *undefined import* (U): the NAPI
#     runtime exports it and the module constructor calls it. A module that exports
#     a local definition of it instead is reported as WARN, not FAIL.
#   * "Exported symbol count" is not a pass criterion: a NAPI module is located by
#     the module name it registers, not by an exported C symbol. Only _init/_fini
#     being exported is normal.
set -u
set -o pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVECO="${DEVECO_HOME:-/d/Program Files/Huawei/DevEco Studio}"
SDK="${OHOS_SDK_ROOT:-$DEVECO/sdk/default/openharmony}"
NDK="$SDK/native"

STRICT=0
TARGET=""
usage() {
  sed -n '2,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}
for arg in "$@"; do
  case "$arg" in
    --strict) STRICT=1 ;;
    -h|--help) usage; exit 0 ;;
    -*) printf 'inspect_so: unknown option %s\n' "$arg" >&2; exit 2 ;;
    *) TARGET="$arg" ;;
  esac
done
[ -n "$TARGET" ] || TARGET="$ROOT/build/napi/libentry.so"
case "$TARGET" in
  /*|[A-Za-z]:*) : ;;
  *) TARGET="$ROOT/$TARGET" ;;
esac

PASS_N=0; FAIL_N=0; WARN_N=0
FAIL_MSGS=(); WARN_MSGS=()
pass() { PASS_N=$((PASS_N+1)); printf '  [PASS] %s\n' "$*"; }
fail() { FAIL_N=$((FAIL_N+1)); FAIL_MSGS+=("$*"); printf '  [FAIL] %s\n' "$*"; }
warn() { WARN_N=$((WARN_N+1)); WARN_MSGS+=("$*"); printf '  [WARN] %s\n' "$*"; }
info() { printf '  [info] %s\n' "$*"; }

printf '== inspect_so ==\n'

# ---------------------------------------------------------------- toolchain ---
find_tool() {
  local name="$1"; shift
  if command -v "$name" >/dev/null 2>&1; then command -v "$name"; return 0; fi
  local p
  for p in "$@"; do
    if [ -x "$p" ]; then printf '%s\n' "$p"; return 0; fi
  done
  return 1
}
READELF="$(find_tool llvm-readelf "$NDK/llvm/bin/llvm-readelf.exe" "$NDK/llvm/bin/llvm-readelf" || true)"
NM="$(find_tool llvm-nm "$NDK/llvm/bin/llvm-nm.exe" "$NDK/llvm/bin/llvm-nm" || true)"

if [ -z "$READELF" ] || [ -z "$NM" ]; then
  printf 'target : %s\n' "$TARGET"
  printf 'tool   : llvm-readelf=%s llvm-nm=%s\n' "${READELF:-MISSING}" "${NM:-MISSING}"
  printf 'ERROR: SDK LLVM binutils not found under %s\n' "$NDK/llvm/bin"
  printf '       set OHOS_SDK_ROOT or DEVECO_HOME, then re-run.\n'
  printf 'RESULT: ERROR (cannot inspect)\n'
  exit 2
fi

if [ ! -f "$TARGET" ]; then
  printf 'target : %s\n' "$TARGET"
  printf 'ERROR: file not found. Build it first (bash scripts/build.sh) or pass a path.\n'
  printf 'RESULT: ERROR (cannot inspect)\n'
  exit 2
fi

SIZE="$(wc -c < "$TARGET" | tr -d ' ')"
if command -v sha256sum >/dev/null 2>&1; then
  SHA="$(sha256sum "$TARGET" | awk '{print $1}')"
else
  SHA="$(python -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$TARGET" 2>/dev/null || echo unknown)"
fi

printf 'target : %s\n' "$TARGET"
printf 'size   : %s bytes\n' "$SIZE"
printf 'sha256 : %s\n' "$SHA"
printf 'readelf: %s\n' "$READELF"
printf 'nm     : %s\n' "$NM"
printf '\n'

if [ "$SIZE" -eq 0 ] 2>/dev/null; then
  fail "file is empty"
fi

# ------------------------------------------------------------ [1] ELF identity
printf '[1] ELF identity\n'
read_magic() {
  local f="$1"
  if command -v od >/dev/null 2>&1; then
    head -c 4 "$f" 2>/dev/null | od -An -tx1 | tr -d ' \n'
  elif command -v xxd >/dev/null 2>&1; then
    head -c 4 "$f" 2>/dev/null | xxd -p | tr -d '\n'
  else
    python -c "import sys;print(open(sys.argv[1],'rb').read(4).hex())" "$f" 2>/dev/null || true
  fi
}
MAGIC="$(read_magic "$TARGET")"
HDR="$("$READELF" -h "$TARGET" 2>&1)"
hdr_get() { printf '%s\n' "$HDR" | sed -n "s/^ *$1: *//p" | head -1; }
ELF_CLASS="$(hdr_get Class)"
ELF_DATA="$(hdr_get Data)"
ELF_TYPE="$(hdr_get Type)"
ELF_MACHINE="$(hdr_get Machine)"

if [ "$MAGIC" = "7f454c46" ]; then
  pass "ELF magic 7f454c46"
else
  fail "not an ELF file (magic=${MAGIC:-empty})"
fi
if [ "$ELF_CLASS" = "ELF64" ]; then pass "class ELF64"; else fail "class is '${ELF_CLASS:-?}', required ELF64"; fi
if [ "$ELF_MACHINE" = "AArch64" ]; then pass "machine AArch64"; else fail "machine is '${ELF_MACHINE:-?}', required AArch64"; fi
case "$ELF_TYPE" in
  DYN*) pass "type DYN (shared object)" ;;
  *)    fail "type is '${ELF_TYPE:-?}', required DYN (shared object)" ;;
esac
case "$ELF_DATA" in
  *little\ endian*) info "data: little endian (expected for AArch64)" ;;
  *)                warn "data: ${ELF_DATA:-?} (expected little endian)" ;;
esac

# ------------------------------------------------------- [2] NEEDED / SONAME
printf '\n[2] NEEDED / SONAME\n'
DYN_OUT="$("$READELF" -d "$TARGET" 2>/dev/null)"
NEEDED=()
if [ -n "$DYN_OUT" ]; then
  while IFS= read -r line; do NEEDED+=("$line"); done < <(
    printf '%s\n' "$DYN_OUT" | sed -n 's/.*(NEEDED).*\[\(.*\)\].*/\1/p'
  )
fi
SONAME="$(printf '%s\n' "$DYN_OUT" | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p' | head -1)"

if [ "${#NEEDED[@]}" -gt 0 ]; then
  info "NEEDED (${#NEEDED[@]}):"
  for n in "${NEEDED[@]}"; do info "  - $n"; done
else
  fail "no DT_NEEDED entries (no dynamic section, or statically linked)"
fi
if [ -n "$SONAME" ]; then info "SONAME: $SONAME"; else warn "no SONAME"; fi

has_needed() {
  local want="$1" n
  for n in "${NEEDED[@]:-}"; do [ "$n" = "$want" ] && return 0; done
  return 1
}
if has_needed "libace_napi.z.so"; then
  pass "NEEDED contains libace_napi.z.so (NAPI runtime)"
else
  fail "NEEDED is missing libace_napi.z.so - napi_* imports would be unresolvable"
fi
if has_needed "libc++_shared.so"; then
  info "NEEDED contains libc++_shared.so (OHOS_STL=c++_shared): the loader must find it at runtime"
fi
if printf '%s\n' "$DYN_OUT" | grep -q 'TEXTREL'; then
  warn "TEXTREL present (data relocations against a writable text segment)"
fi
if printf '%s\n' "$DYN_OUT" | grep -q 'BIND_NOW'; then
  info "BIND_NOW / FLAGS_1 NOW: full RELRO-style eager binding"
fi

# ------------------------------------------- [3][4][5] symbols via llvm-nm -D
printf '\n[3] napi_module_register\n'
DEFINED_DYN="$("$NM" -D --defined-only "$TARGET" 2>/dev/null | awk 'NF>0{print $NF}')"
UNDEF_DYN="$("$NM" -D -u "$TARGET" 2>/dev/null | awk 'NF>0{print $NF}')"
DEF_CNT="$(printf '%s' "$DEFINED_DYN" | grep -c . || true)"
UND_CNT="$(printf '%s' "$UNDEF_DYN" | grep -c . || true)"

NMR_DEF=0; NMR_UND=0
printf '%s\n' "$DEFINED_DYN" | grep -qx 'napi_module_register' && NMR_DEF=1
printf '%s\n' "$UNDEF_DYN"   | grep -qx 'napi_module_register' && NMR_UND=1

if [ "$NMR_UND" -eq 1 ] && [ "$NMR_DEF" -eq 0 ]; then
  pass "napi_module_register is an undefined import (U) - resolved by libace_napi.z.so"
elif [ "$NMR_DEF" -eq 1 ] && [ "$NMR_UND" -eq 0 ]; then
  warn "napi_module_register is DEFINED in this module, not imported from the NAPI runtime"
elif [ "$NMR_DEF" -eq 1 ] && [ "$NMR_UND" -eq 1 ]; then
  warn "napi_module_register appears both defined and undefined"
else
  fail "napi_module_register is absent from the dynamic symbol table (not a NAPI module)"
fi

printf '\n[4] exported symbols (dynamic, defined)\n'
if [ "$DEF_CNT" -eq 0 ]; then
  fail "no exported dynamic symbols at all (expected at least _init/_fini)"
else
  info "count: $DEF_CNT"
  printf '%s\n' "$DEFINED_DYN" | sed '/^$/d' | head -40 | while IFS= read -r s; do info "  + $s"; done
  EXTRA="$(printf '%s\n' "$DEFINED_DYN" | sed '/^$/d' | grep -vxE '_init|_fini|_edata|__bss_start|__end__' | head -20 || true)"
  if [ -n "$EXTRA" ]; then
    info "non-boilerplate exports:"
    printf '%s\n' "$EXTRA" | while IFS= read -r s; do info "  * $s"; done
  else
    pass "exports are limited to linker boilerplate (_init/_fini) - normal for a NAPI module"
  fi
fi

printf '\n[5] undefined imports (dynamic)\n'
if [ "$UND_CNT" -eq 0 ]; then
  warn "no undefined dynamic symbols - module cannot call the NAPI runtime"
else
  info "count: $UND_CNT"
  printf '%s\n' "$UNDEF_DYN" | sed '/^$/d' | head -40 | while IFS= read -r s; do info "  - $s"; done
fi
NAPI_UND_CNT="$(printf '%s\n' "$UNDEF_DYN" | grep -c '^napi_' || true)"
if [ "$NAPI_UND_CNT" -gt 0 ]; then
  if has_needed "libace_napi.z.so"; then
    pass "$NAPI_UND_CNT napi_* imports are resolvable through libace_napi.z.so"
  else
    fail "$NAPI_UND_CNT napi_* imports but libace_napi.z.so is not in NEEDED"
  fi
fi
CPP_UND="$(printf '%s\n' "$UNDEF_DYN" | grep -E '^_Z(St|N|dlPv|nwm|TV|TI|TS)' | head -5 || true)"
if [ -n "$CPP_UND" ]; then
  if has_needed "libc++_shared.so"; then
    info "libstdc++-style imports present and libc++_shared.so is in NEEDED"
  else
    warn "C++ runtime imports present but libc++_shared.so is not in NEEDED"
  fi
fi

# --------------------------------------------------- [6] local NAPI callbacks
printf '\n[6] local NAPI callbacks (non-dynamic, informational)\n'
LOCAL_NAPI="$("$NM" -C --defined-only "$TARGET" 2>/dev/null | grep -E 'napi_env|napi_callback_info' || true)"
if [ -n "$LOCAL_NAPI" ]; then
  info "count: $(printf '%s\n' "$LOCAL_NAPI" | grep -c .)"
  printf '%s\n' "$LOCAL_NAPI" | head -10 | while IFS= read -r s; do info "  ~ $s"; done
else
  info "none - local symbols absent (file is stripped); callback names cannot be re-checked here"
fi

# ------------------------------------------------------------- unexpected deps
ALLOWED_NEEDED=" libc.so libm.so libdl.so libpthread.so librt.so libz.so libace_napi.z.so libc++_shared.so libhilog_ndk.z.so libace_ndk.z.so libnative_window.so "
for n in "${NEEDED[@]:-}"; do
  [ -n "$n" ] || continue
  case "$ALLOWED_NEEDED" in
    *" $n "*) : ;;
    *) warn "NEEDED '$n' is outside the known OHOS system set - confirm it ships on the target" ;;
  esac
done

# ------------------------------------------------------------------- verdict
printf '\n'
if [ "$STRICT" -eq 1 ] && [ "$WARN_N" -gt 0 ]; then
  printf 'strict mode: %s warning(s) promoted to failure\n' "$WARN_N"
  FAIL_N=$((FAIL_N + WARN_N))
fi
if [ "$FAIL_N" -gt 0 ]; then
  printf 'failed checks:\n'
  for m in "${FAIL_MSGS[@]:-}"; do [ -n "$m" ] && printf '  - %s\n' "$m"; done
  printf 'RESULT: FAIL (passed=%s failed=%s warnings=%s)\n' "$PASS_N" "$FAIL_N" "$WARN_N"
  exit 1
fi
printf 'RESULT: PASS (passed=%s failed=0 warnings=%s)\n' "$PASS_N" "$WARN_N"
if [ "$WARN_N" -gt 0 ]; then
  printf 'warnings:\n'
  for m in "${WARN_MSGS[@]:-}"; do [ -n "$m" ] && printf '  - %s\n' "$m"; done
fi
exit 0
