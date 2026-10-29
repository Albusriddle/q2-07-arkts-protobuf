#!/usr/bin/env bash
# Generate/verify the C++ field table from proto/example.proto.
#   bash scripts/codegen.sh --write   # update generated header
#   bash scripts/codegen.sh --check   # fail if checked-in header is stale
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python "$ROOT/scripts/gen_fields.py" "$@"
