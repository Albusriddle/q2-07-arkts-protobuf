#!/usr/bin/env python3
"""Compare HOST-ONLY native and protobufjs benchmark CSVs."""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def index(rows: list[dict[str, str]]) -> dict[tuple[str, str], dict[str, str]]:
    return {(r["dataset"], r["metric"]): r for r in rows}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--native", required=True)
    ap.add_argument("--pbjs", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    native = index(read_csv(Path(a.native)))
    pbjs = index(read_csv(Path(a.pbjs)))
    datasets = sorted({d for d, _ in native if d in {x for x, _ in pbjs}})
    out = {
        "scope": "HOST-ONLY",
        "not_claimed": ["device performance", "1.5x", "700ms"],
        "note": "native vs protobufjs is host-only; different runtimes and object models",
        "datasets": {},
    }
    for ds in datasets:
        def get(table, key):
            row = table.get((ds, key))
            return float(row["value"]) if row else None

        n_ms = get(native, "wire_decode_median_ms")
        p_ms = get(pbjs, "wire_decode_median_ms")
        p_obj_ms = get(pbjs, "decode_plus_toObject_median_ms")
        n_mb = get(native, "wire_decode_mb_per_s")
        p_mb = get(pbjs, "wire_decode_mb_per_s")
        n_msg = get(native, "wire_decode_msg_per_s")
        p_msg = get(pbjs, "wire_decode_msg_per_s")
        n_json = get(native, "decode_plus_json_median_ms")
        p_obj = get(pbjs, "decode_plus_toObject_median_ms")
        n_json_mb = get(native, "decode_plus_json_mb_per_s")
        p_obj_mb = get(pbjs, "decode_plus_toObject_mb_per_s")
        item = {
            "native": {"decode_median_ms": n_ms, "decode_mb_per_s": n_mb, "decode_msg_per_s": n_msg},
            "protobufjs": {"decode_median_ms": p_ms, "decode_mb_per_s": p_mb, "decode_msg_per_s": p_msg,
                           "decode_plus_toObject_median_ms": p_obj_ms},
            "native_decode_plus_json": {"median_ms": n_json, "mb_per_s": n_json_mb},
            "ratios": {
                "native_speedup_by_median": (p_ms / n_ms) if p_ms and n_ms else None,
                "native_mb_per_s_over_pbjs": (n_mb / p_mb) if n_mb and p_mb else None,
                "native_json_vs_pbjs_decode_toObject": (p_obj / n_json) if p_obj and n_json else None,
            },
            "messages": int(get(native, "messages") or 0),
        }
        out["datasets"][ds] = item
    Path(a.out).write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for ds, item in out["datasets"].items():
        ratio = item["ratios"]["native_speedup_by_median"]
        print(f"{ds}: native/pbjs decode median ratio={ratio:.2f}x" if ratio else f"{ds}: incomplete")
    print(f"wrote {a.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
