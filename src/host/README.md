# Host-side vertical slice (v0, implemented)

HOST-ONLY. Everything under `src/host/**` is a logic/correctness harness that runs on
the development PC. It produces **no device numbers**: no ArkTS runtime, no board.
V0.1/V0.2 also compile the decoder subset into the NAPI `.so`; the NAPI binding is
compiled but not executed on a device. See `docs/host-slice.md` and `docs/v0.1-decode.md`.

```
proto/example.proto
  -> descriptor/IR (generated: decoder/generated/q207_demo_schema.json)
  -> wire reader          (decoder/wire_reader.{h,cpp})       C++17
  -> POD decode           (decoder/decode_user.cpp + generated decoder.inc) User / UserBatch
  -> norm-v1 object dump  (decoder/norm.{h,cpp})
  -> tape v0 write/verify (tape/tape.{h,cpp})
  -> materializer         (materializer/materializer.ts)       tape -> plain objects
  -> differential         (ts/{gen_corpus,differential,materializer_check}.mjs)
                          vs protobufjs 7.6.5
```

## Files

| Path | What it is |
|---|---|
| `common/status.h`, `common/status.cpp` | `Code` enum + canonical `E_*` names (identical to the W0 Python reference model) and the hard limits (64 MiB/message, depth 100, 16 MiB/string, 1e6 repeated, 256 MiB tape) |
| `common/sha256.{h,cpp}` | dependency-free SHA-256 for artifact/data-card fingerprints |
| `decoder/wire_reader.{h,cpp}` | the reader. Mandated order `tag → wire type → len ≤ remaining → len ≤ cap → allocate`; varint (10-byte rule), zigzag, fixed32/64, length-delimited, packed, group skip, strict UTF-8, depth/field/object budgets, `note_alloc()` accounting hook |
| `decoder/pod.h` | thin include of generated field constants, POD structs and schema metadata |
| `decoder/decode_user.cpp` | thin wrapper; parser body is generated in `generated/q207_demo_decoder.inc`: int64, string, repeated string, nested `Profile`, oneof (keep-all + discriminator), proto3 `optional` tri-state, `map<string,int32>` |
| `decoder/conformance.{h,cpp}` | the W0 reference schema (f1..f5) instantiated on the same reader so the 38 golden vectors can judge the C++ reader directly |
| `decoder/norm.{h,cpp}` | "norm-v1" canonical object dump; rules are exactly protobufjs 7.6.5 `toObject({defaults:true, longs:String})` (probed, not guessed) |
| `tape/tape.{h,cpp}` | tape v0: 80 B user record, 16 B profile record, `roots` = byte offsets, `blob`, `values`, crc32; writer + verifier + reader |
| `tests/` | 464 assertions: wire primitives + validation order, 38 golden vectors, tape round-trip/determinism/verifier negatives, decode edge cases |
| `tools/host_decode_dump.cpp` | CLI: `*.wire` corpus → norm JSON + tape; framed data card → norm + tape |
| `bench/host_bench.cpp` | S1-like / S3-like cards → `evidence/host/host_bench.csv` |
| `tools/collect_evidence.py` | assembles `evidence/host_slice.json` (commands, exit codes, timings, sha256) |
| `tools/py_fallback_diff.py` | degraded differential used only when protobufjs cannot be installed |
| `materializer/materializer.ts` | plain-object materializer for tape v0 (NOT ArkTS, NOT the NAPI path) |
| `ts/` | protobufjs 7.6.5 harness: corpus generator, differential, large-tape materializer check |

## Hard rules the code enforces (and tests assert)

1. **Decision order**: `tag → wire type → len ≤ remaining → len ≤ cap → 分配`.
   `read_length()` checks remaining before cap; nothing is reserved before both pass.
   The test binary overrides global `operator new` and asserts a 4 GiB length prefix
   allocates **0 bytes in 0 calls**.
2. **Limits**: single message ≤ 64 MiB, nesting ≤ 100, single string ≤ 16 MiB,
   repeated ≤ 1,000,000 elements, tape ≤ 256 MiB.
3. **int32/int64 are varints**: a negative `int32` is a 10-byte varint sign-extended to
   64 bits and truncated to 32 on read. It is never read as `fixed32`.
4. **Non-shortest varints are accepted** (protobuf implementations do), never reported
   as errors.
5. **Known-field wire mismatch**: strict fails with `E_WIRE_TYPE_MISMATCH`; the compat arm
   skips the bytes as an unknown field and never reinterprets them.
6. **Unknown fields never allocate** (`skip_field` only advances the cursor).
7. **oneof**: keep-all members (`contact_bits`) + last-wins discriminator (`oneof_case`),
   matching protobufjs 7.6.5 rather than assuming a version.
8. **tape** records are fixed-size and 8-byte aligned; `values` slots are addressed by
   **byte offset** and counted in **elements**; roots are **byte offsets**, not indices.

## Known differences / open points

See the table in `docs/host-slice.md` (D-a … D-g): map duplicate-key folding,
non-canonical varints not re-emitted, unknown fields discarded, dangerous map keys
rejected in strict mode, invalid UTF-8 policy, int64 representation, and the
schema-specific `RECORD_SIZE=80`.

## Toolchain note

`scripts/build_host.sh` prefers `$CXX`, then `g++`, then `clang++`, and only then MSVC.
On the machine used for the recorded evidence no g++/clang++ with a usable Windows
sysroot existed (the DevEco clang 15 is an OHOS-oriented build and MSVC STL 14.44
rejects clang < 19), so the build used **MSVC cl 19.44** — still a *native host*
compiler, not the OpenHarmony NDK cross-toolchain.

## Run

```bash
bash repo/scripts/build_host.sh            # build + tests + cards + bench + differential + evidence
bash repo/scripts/build_host.sh --no-npm   # offline: python golden baseline instead of protobufjs
```
