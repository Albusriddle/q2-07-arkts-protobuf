#!/usr/bin/env python3
"""HOST-ONLY data-card generator for the Q2-07 host slice.

Cards (report v3 §6.1.1 class names, synthetic, seeded => byte-reproducible):

  s1_user_1kb.bin    S1-like: 10-30 field User messages, ~1 KiB each,
                     one UserBatch frame (~8 MB payload).
  s3_100mb.bin       S3-like: 100 MiB payload split into ~1 MiB UserBatch
                     frames (a single protobuf message is capped at 64 MiB).

Card container (a data-card convention, NOT protobuf):
    repeated [u32 little-endian payload_len][UserBatch bytes]

Every card is deterministic: same seed => same sha256.  Nothing here is a device
measurement; the numbers produced from these cards are HOST-ONLY.

Usage:
    python repo/bench/gen_cards.py --out repo/bench/data
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys


# ---- deterministic PRNG (LCG, so the cards do not depend on Python's random) --
class Lcg:
    def __init__(self, seed: int) -> None:
        self.state = seed & 0xFFFFFFFF

    def next(self) -> int:
        self.state = (1103515245 * self.state + 12345) & 0x7FFFFFFF
        return self.state

    def below(self, n: int) -> int:
        return self.next() % n

    def pick(self, seq):
        return seq[self.below(len(seq))]


# ---- protobuf wire helpers -------------------------------------------------
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


CITIES = ["Hangzhou", "Shenzhen", "Beijing", "Shanghai", "Chengdu", "Xi'an", "Wuhan", "Suzhou"]
DOMAINS = ["example.com", "corp.example", "mail.example", "test.example"]


def make_user(i: int, rng: Lcg, target_len: int) -> bytes:
    """One q207.demo.User message, roughly target_len bytes."""
    score_pairs = 3 + rng.below(3)          # 3..5 map entries
    n_tags = 3 + rng.below(4)               # 3..6 tags
    # choose a name length so that the total lands near target_len
    base = 2 + 4 + 2 + 4 + 2 + 2 + 4 * score_pairs + 2 + 2
    name_len = max(4, target_len - base - n_tags * 3 - 24 - rng.below(64))
    name = (f"user-{i:06d}-" + "x" * max(0, name_len - 12))[:name_len]

    body = bytearray()
    body += vi(1, i + 1)                                  # id  (int64)
    body += ld(2, name.encode())                          # name (string)
    for t in range(n_tags):                               # tags (repeated string)
        body += ld(3, f"t{t}-{rng.pick(['a', 'b', 'c', 'red', 'blue'])}".encode())
    city = rng.pick(CITIES)                               # profile (nested)
    prof = ld(1, city.encode()) + vi(2, rng.below(90))
    body += ld(4, prof)
    if rng.below(2):                                      # oneof contact
        body += ld(5, f"u{i}@{rng.pick(DOMAINS)}".encode())
    else:
        body += ld(6, f"1{rng.next():09d}")
    if rng.below(4) == 0:                                 # optional int32 level
        body += vi(7, rng.below(10))
    for s in range(score_pairs):                          # map<string,int32>
        entry = ld(1, f"k{s}".encode()) + vi(2, rng.below(1000))
        body += ld(8, entry)
    return bytes(body)


def make_batch(users) -> bytes:
    return b"".join(ld(1, u) for u in users)


def build_card(target_payload: int, frame_target: int, seed: int, user_target: int):
    """Returns (card_bytes, stats)."""
    rng = Lcg(seed)
    card = bytearray()
    payload = 0
    messages = 0
    frames = 0
    user_index = 0
    while payload < target_payload:
        users = []
        size = 0
        while size < frame_target:
            u = make_user(user_index, rng, user_target)
            users.append(u)
            size += len(u) + 2  # + tag/len overhead of the batch wrapper
            user_index += 1
        batch = make_batch(users)
        if payload + len(batch) > target_payload and frames > 0:
            # stop on a message boundary; do not split a batch
            remaining = target_payload - payload
            if remaining < 4096:
                break
        card += struct.pack("<I", len(batch)) + batch
        payload += len(batch)
        messages += len(users)
        frames += 1
    return bytes(card), {"payload_bytes": payload, "messages": messages, "frames": frames}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="repo/bench/data")
    ap.add_argument("--s1-payload", type=int, default=8 * 1024 * 1024)
    ap.add_argument("--s3-payload", type=int, default=100 * 1024 * 1024)
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    summary = {}
    # S1: one frame of ~1 KiB messages
    card, stats = build_card(args.s1_payload, args.s1_payload, seed=20261029, user_target=1024)
    path = os.path.join(args.out, "s1_user_1kb.bin")
    with open(path, "wb") as f:
        f.write(card)
    summary["s1_user_1kb"] = dict(
        path=path,
        bytes=len(card),
        sha256=hashlib.sha256(card).hexdigest(),
        **stats,
    )
    # S3: ~1 MiB frames summing to 100 MiB
    card, stats = build_card(args.s3_payload, 1024 * 1024, seed=20261030, user_target=1024)
    path = os.path.join(args.out, "s3_100mb.bin")
    with open(path, "wb") as f:
        f.write(card)
    summary["s3_100mb"] = dict(
        path=path,
        bytes=len(card),
        sha256=hashlib.sha256(card).hexdigest(),
        **stats,
    )

    print(json.dumps({"scope": "HOST-ONLY", "cards": summary}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
