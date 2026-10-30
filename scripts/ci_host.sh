#!/usr/bin/env bash
# Host-only CI gate: no DevEco, no device.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

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

"$PYTHON" scripts/gen_fields.py --check
"$PYTHON" scripts/test_gen_fields.py
"$PYTHON" scripts/check_batch_sample.py --check
bash scripts/build_host.sh
"$PYTHON" scripts/check_generated_map_compile.py
