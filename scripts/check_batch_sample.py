#!/usr/bin/env python3
"""Check the V0.2 batch sample against its single source of truth.

Source of truth: evidence/host/corpus/c20_full.wire (52 B, User).
Derived sample: 0x0a + varint(52) + c20_full.wire + 0x0a 0x00 (56 B UserBatch).
ArkTS/ohosTest constants must contain the exact derived hex.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
C20 = ROOT / "evidence" / "host" / "corpus" / "c20_full.wire"
BATCH = ROOT / "evidence" / "host" / "batch" / "user_batch_c20_plus_empty.wire"
MANIFEST = ROOT / "evidence" / "host" / "batch" / "user_batch_c20_plus_empty.json"
ARKTS = [
    ROOT / "app" / "entry" / "src" / "main" / "ets" / "pages" / "Index.ets",
    ROOT / "app" / "entry" / "src" / "ohosTest" / "ets" / "test" / "EntryNapi.test.ets",
]


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def varint(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def derived():
    c20 = C20.read_bytes()
    wire = b"\x0a" + varint(len(c20)) + c20 + b"\x0a\x00"
    return c20, wire


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    if not args.write and not args.check:
        ap.error("use --write or --check")

    c20, wire = derived()
    hex_wire = wire.hex()
    manifest = {
        "scope": "HOST-ONLY",
        "stem": "user_batch_c20_plus_empty",
        "composition": "0a + varint(52) + c20_full.wire + 0a00",
        "wire_bytes": len(wire),
        "wire_hex": hex_wire,
        "wire_sha256": sha(wire),
        "c20_full_sha256": sha(c20),
        "expected": {
            "users": 2,
            "first_id": "9007199254740993",
            "second": "defaults",
        },
    }

    if args.write:
        BATCH.parent.mkdir(parents=True, exist_ok=True)
        BATCH.write_bytes(wire)
        MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(f"wrote {BATCH.relative_to(ROOT)} ({len(wire)} B)")
        print(f"hex: {hex_wire}")

    if not BATCH.exists() or not MANIFEST.exists():
        print("batch sample/manifest missing; run scripts/check_batch_sample.py --write", file=sys.stderr)
        return 1
    current = BATCH.read_bytes()
    if current != wire:
        print("batch sample is stale; run scripts/check_batch_sample.py --write", file=sys.stderr)
        return 1
    m = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if m.get("wire_hex") != hex_wire or m.get("wire_bytes") != len(wire):
        print("batch manifest is stale", file=sys.stderr)
        return 1
    for p in ARKTS:
        text = p.read_text(encoding="utf-8")
        if hex_wire not in text:
            print(f"sample hex missing from {p.relative_to(ROOT)}", file=sys.stderr)
            return 1
    print(f"batch sample OK: {BATCH.relative_to(ROOT)} ({len(wire)} B, {sha(wire)[:12]}…)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
