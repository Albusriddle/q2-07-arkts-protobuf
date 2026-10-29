# Benchmarks and data cards (HOST-ONLY)

Everything in this directory feeds `evidence/host/host_bench.csv`. The numbers are
measured on the development PC with a native host compiler and are **not device
results** — see `docs/host-slice.md`. Host rows are always labeled `HOST-ONLY`
(and a `scope` column carries it into the CSV), so PC numbers cannot leak into a
device report.

## Data cards

Cards are generated deterministically (seeded LCG, no `random` module), so the same
command reproduces the same sha256 on any machine.

```bash
python bench/gen_cards.py --out bench/data
```

| Card | Class | Shape | Size | Payload |
|---|---|---|---|---|
| `s1_user_1kb.bin` | S1 (10–30 fields, ≈1 KiB) | 1 frame, 8,178 `User` messages, 191,858 fields | 8,397,700 B | 8,397,696 B |
| `s3_100mb.bin` | S3 (100 MB 合成) | 100 frames × ≈1 MiB, 102,100 messages, 2,393,694 fields | 105,005,840 B | 105,005,440 B |

Exact sizes/sha256 for the recorded run are in `evidence/host_slice.json`
(`bench.<dataset>.card_bytes` / `card_sha256`), and every CSV row carries them too.

### Card container (a data-card convention, **not** protobuf)

```
repeated [u32 little-endian payload_len][UserBatch message bytes]
```

Why framing exists: a single protobuf message is capped at **64 MiB** by design
(report v3 §5.5.8), so the 100 MiB card must be split **on message boundaries**, never
mid-message. Each frame is a complete, valid `q207.demo.UserBatch`.

`bench/data/*.bin` is generated and git-ignored (root `.gitignore`); only the CSV,
the evidence JSON and this generator are committed.

## What is measured

For each card, with `--reps 5` (median reported):

| Metric (CSV `metric`) | Meaning |
|---|---|
| `wire_decode_mb_per_s` | payload bytes / 1e6 per second of pure C++ decode into a reused POD batch |
| `wire_decode_msg_per_s`, `wire_decode_ns_per_msg` | per-message costs |
| `wire_decode_median_ms` | median time for one full card decode |
| `tape_write_median_ms` | median time for decode **+** tape write of the full card |
| `tape_write_only_median_ms`, `tape_write_only_ns_per_msg` | derived: tape pass minus decode pass |
| `tape_bytes_total`, `tape_bytes_per_msg` | tape v0 size (80 B/record + 16 B/profile + blob + values) |
| `tape_first_frame_sha256` | proves the tape bytes are reproducible for a fixed card |
| `card_payload_bytes`, `frames`, `messages`, `fields` | dataset identity |

`tape_first_frame_sha256` and the per-card sha256 are the determinism evidence: rerun
the generator and the bench, and both hashes must be unchanged.

## Honest reading

- The tape writer is a **straightforward reference implementation** (per-field
  `std::string` copies into a growing blob, no arena): its numbers are an upper bound,
  not a design target.
- No NAPI, no ArkTS, no GC, no device CPU: `c_call`, `c_con` and end-to-end latency are
  **not** measured here and must not be inferred from these numbers.
- Do not mix these rows into device claims or into a 1.5×/700 ms argument.

## Still missing (deliberately)

- `run_all.sh`, `stats.py` (median/IQR/P95/bootstrap CI) and `report/` from the original
  plan: the slice reports median/min/max in the CSV directly; a statistics layer and a
  device run script belong to the device phase.
- S2 (bulk records) and S4 (wide message) cards, plus the S5 pathological corpus, remain
  for the codegen/device phases.
