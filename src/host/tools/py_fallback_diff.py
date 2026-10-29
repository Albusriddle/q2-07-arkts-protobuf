#!/usr/bin/env python3
"""HOST-ONLY degraded differential baseline (used only when protobufjs is absent).

Normal path: npm i protobufjs@7.6.5 and src/host/ts/differential.mjs.
Fallback path: this script builds the SAME kind of corpus from an independent
Python encoder (written from the protobuf spec, not shared with the C++ code),
computes the expected object graph directly from the Python objects, then asks the
C++ decoder to decode the bytes and compares.

Why it is weaker than the protobufjs baseline (and says so in the report):
  * no independent *decoder* is involved - only an independent encoder + expected
    object graph, so protobufjs's own value semantics are not exercised;
  * no re-encode/byte round-trip oracle;
  * the 38 W0 golden wire vectors (already in the unit test suite) remain the
    authority on the reader's decision order.

Exit 0 only when every case matches.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys


# ---- protobuf encoding written from the spec -------------------------------
def varint(n: int) -> bytes:
    if n < 0:
        n += 1 << 64
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def tag(field: int, wire: int) -> bytes:
    return varint((field << 3) | wire)


def ld(field: int, payload: bytes) -> bytes:
    return tag(field, 2) + varint(len(payload)) + payload


def vi(field: int, value: int) -> bytes:
    return tag(field, 0) + varint(value)


def enc_user(o: dict) -> bytes:
    out = bytearray()
    if "id" in o:
        out += vi(1, int(o["id"]))
    if "name" in o:
        out += ld(2, o["name"].encode("utf-8"))
    for t in o.get("tags", []):
        out += ld(3, t.encode("utf-8"))
    if "profile" in o:
        p = o["profile"] or {}
        inner = bytearray()
        if "city" in p:
            inner += ld(1, p["city"].encode("utf-8"))
        if "age" in p:
            inner += vi(2, int(p["age"]))
        out += ld(4, bytes(inner))
    if "email" in o:
        out += ld(5, o["email"].encode("utf-8"))
    if "phone" in o:
        out += ld(6, o["phone"].encode("utf-8"))
    if "level" in o:
        out += vi(7, int(o["level"]))
    for k, v in o.get("scores", []):
        out += ld(8, ld(1, k.encode("utf-8")) + vi(2, int(v)))
    return bytes(out)


def norm_user(o: dict) -> dict:
    """norm-v1: protobufjs 7.6.5 toObject({defaults:true, longs:String}) rules."""
    out = {
        "id": str(int(o.get("id", 0))),
        "name": o.get("name", ""),
        "tags": list(o.get("tags", [])),
        "profile": (
            {
                "city": (o["profile"] or {}).get("city", ""),
                "age": int((o["profile"] or {}).get("age", 0)),
            }
            if "profile" in o
            else None
        ),
    }
    if "email" in o:
        out["email"] = o["email"]
    if "phone" in o:
        out["phone"] = o["phone"]
    if "level" in o:
        out["level"] = int(o["level"])
    score_map = {}
    for k, v in o.get("scores", []):
        score_map[k] = int(v)  # last wins, first position kept
    out["scores"] = score_map
    return out


CASES = {
    "f01_empty": {},
    "f02_id": {"id": 1},
    "f03_zeros": {"id": 0, "name": ""},
    "f04_i64_min": {"id": -(2**63)},
    "f05_i64_max": {"id": 2**63 - 1},
    "f06_unicode": {"name": "\u4f60\u597d\U0001f600"},
    "f07_tags": {"tags": ["a", "", "c"]},
    "f08_profile_empty": {"profile": {}},
    "f09_profile": {"profile": {"city": "X", "age": 0}},
    "f10_oneof_both": {"email": "e@x", "phone": "123"},
    "f11_level0": {"level": 0},
    "f12_level_neg": {"level": -5},
    "f13_map": {"scores": [("math", 95), ("art", -1), ("math", 99)]},
    "f14_full": {
        "id": 9007199254740993,
        "name": "alice",
        "tags": ["a", "b"],
        "profile": {"city": "X", "age": 3},
        "email": "e@x",
        "level": 7,
        "scores": [("k1", 1), ("k2", 2)],
    },
}


def deep_diff(a, b, prefix="$", out=None):
    if out is None:
        out = []
    if a == b:
        return out
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a:
                out.append({"path": f"{prefix}.{k}", "cpp": "<missing>", "expect": b[k]})
            elif k not in b:
                out.append({"path": f"{prefix}.{k}", "cpp": a[k], "expect": "<missing>"})
            else:
                deep_diff(a[k], b[k], f"{prefix}.{k}", out)
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append({"path": f"{prefix}.length", "cpp": len(a), "expect": len(b)})
        for i, (x, y) in enumerate(zip(a, b)):
            deep_diff(x, y, f"{prefix}[{i}]", out)
    else:
        out.append({"path": prefix, "cpp": a, "expect": b})
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", required=True)
    ap.add_argument("--cpp-dump", required=True)
    ap.add_argument("--corpus", required=True, help="where the C++ dump output goes")
    ap.add_argument("--report", required=True)
    args = ap.parse_args()

    corpus_dir = os.path.join(args.work, "corpus")
    os.makedirs(corpus_dir, exist_ok=True)
    os.makedirs(args.corpus, exist_ok=True)
    index = []
    for stem, obj in CASES.items():
        wire = enc_user(obj)
        with open(os.path.join(corpus_dir, f"{stem}.wire"), "wb") as f:
            f.write(wire)
        with open(os.path.join(corpus_dir, f"{stem}.pbjs.json"), "w", encoding="utf-8") as f:
            json.dump(norm_user(obj), f)
        index.append({"stem": stem, "wire_hex": wire.hex(), "wire_bytes": len(wire)})
    with open(os.path.join(corpus_dir, "corpus.json"), "w", encoding="utf-8") as f:
        json.dump({"scope": "HOST-ONLY", "baseline": "python-encoder-fallback", "items": index}, f, indent=2)

    dump = subprocess.run(
        [args.cpp_dump, "--corpus", corpus_dir, "--out", args.corpus,
         "--meta", os.path.join(args.corpus, "meta.json")],
        capture_output=True, text=True, check=False,
    )
    if dump.returncode != 0:
        print(dump.stdout + dump.stderr, file=sys.stderr)
        return 1

    report = {
        "scope": "HOST-ONLY",
        "baseline": "python-encoder-fallback (protobufjs unavailable: DEGRADED)",
        "degradation_reason": "npm/protobufjs 7.6.5 could not be installed; using an "
                              "independent Python encoder + expected object graph instead",
        "not_covered_by_this_baseline": [
            "protobufjs's own decode/toObject value semantics",
            "byte round-trip oracle",
        ],
        "cases": len(CASES),
        "object_graph": {"checked": 0, "equal": 0, "mismatches": []},
        "bytes": {"checked": 0, "identical": 0, "explained_differences": [], "not_checked": True},
        "materializer": {"checked": 0, "equal": 0, "mismatches": [], "note": "covered by unit tests"},
    }
    for stem, obj in CASES.items():
        path = os.path.join(args.corpus, f"{stem}.norm.json")
        if not os.path.exists(path):
            report["object_graph"]["mismatches"].append({"stem": stem, "reason": "no C++ norm output"})
            continue
        with open(path, "r", encoding="utf-8") as f:
            got = json.load(f)
        report["object_graph"]["checked"] += 1
        diffs = deep_diff(got, norm_user(obj))
        if diffs:
            report["object_graph"]["mismatches"].append({"stem": stem, "diffs": diffs})
        else:
            report["object_graph"]["equal"] += 1
    report["ok"] = len(report["object_graph"]["mismatches"]) == 0

    with open(args.report, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    print(f"python fallback differential: {report['object_graph']['equal']}/{report['object_graph']['checked']} equal")
    if not report["ok"]:
        print(json.dumps(report["object_graph"]["mismatches"], indent=2), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
