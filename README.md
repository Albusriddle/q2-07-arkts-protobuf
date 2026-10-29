# Q2-07 · Native ArkTS High-Performance Protobuf Parsing

> Status: **host vertical slice implemented; V0.1 adds generated field table + NAPI `decodeUser`**; unsigned/self-signed HAP built (not device-run).
> Feasibility: **B- / conditionally viable** under “API-level compatibility + P3-batch first + hot-message P4 + 20 weeks”.
> This repository is a working scaffold, not a claim that 1.5×/700 ms is achieved.

## What this is

A feasibility + implementation workspace for the OpenHarmony Q2-07 challenge:

- decode Protobuf produced by existing ArkTS apps faster than the current `protobuf.js` path;
- keep the **ArkTS-facing API** unchanged at the API/signature level;
- generate C++/ArkTS code from `.proto`, with a reproducible benchmark and evidence chain.

**Implemented today**

- host-side C++17 wire reader, POD decoder for `proto/example.proto`, tape v0 writer/verifier,
  TS materializer and differential harness against **protobufjs 7.6.5**;
- V0.1 NAPI bridge (`ping`/`add`/`decodeUser`) in `app/entry/src/main/cpp/`,
  built by hvigor `externalNativeOptions`; `decodeUser` returns the norm-v1 object shape;
- field numbers/wire types generated from `proto/example.proto` into
  `src/host/decoder/generated/q207_demo_fields.h` (`scripts/codegen.sh --check`);
- `ohosTest` test HAP builds (not executed: no device); `decodeUser` is **compiled,
  not executed on a device** (see `docs/v0.1-decode.md`).

**Not implemented yet (do not claim as done)**

- full `.proto → descriptor/IR → C++/ArkTS` code generation; only field constants + wire
  types are generated today, while `pod.h` / `decode_user.cpp` remain hand-written;
- NAPI `decode`/`decodeMany` (only `ping`/`add` exist);
- protobufjs **8.x**, full P3/P4, real device install/run/AOT/performance.

Device-side performance numbers are **not** available: no board, no UDID, no trusted signing
profile. All host numbers are labelled `HOST-ONLY`.

## Read first

| Document | Why |
|---|---|
| [`docs/feasibility-reestimate.md`](docs/feasibility-reestimate.md) | Resource-available feasibility re-estimate: B-, conditions, hard risks, 20-week route |
| [`docs/w0-execution-summary.md`](docs/w0-execution-summary.md) | What W0 executed; gate statuses; what is resource-gated vs technically hard |
| [`docs/w0-resource-assumption.md`](docs/w0-resource-assumption.md) | Why W0 `BLOCKED` items are reclassified as `RESOURCE-GATED`, not no-go |
| [`docs/host-slice.md`](docs/host-slice.md) | HOST-ONLY reader/tape/materializer/differential results |
| [`docs/v0-slice.md`](docs/v0-slice.md) | V0 NAPI/HAP build and current BLOCKED list |
| [`docs/v0.1-decode.md`](docs/v0.1-decode.md) | V0.1 generated field table + `decodeUser` NAPI, claim levels and limitations |

## Scope and non-goals

**In scope (first phase)**

- host-side vertical slice: `.proto` → descriptor/IR → wire decoder → tape → materializer;
- P3-batch (one NAPI call + N object constructions, no tape) as the first native path;
- differential/golden tests against `protobufjs` 7.6.5 (8.x is a later gate);
- benchmark harness, data cards, statistics, replay scripts;
- API-level compatibility contract and documented value-semantics differences.

**Not claimed**

- exact `Object.keys`/`JSON.stringify`/oneof value semantics of `protobuf.js`;
- 4.29× or 700 ms on all datasets;
- full ANI backend or full P4 row-tape generator before V3/G1 evidence;
- “zero crashes” for malicious input; only bounded behavior + fuzz + stress tests;
- device install/run/performance.

## Repository layout

```
proto/                 sample .proto for host vertical slice
src/host/              host-side C++/TS reference implementation (implemented)
bench/                 deterministic data-card generator + host benchmark (implemented)
scripts/               build / test / inspect / sign helpers (implemented)
docs/                  feasibility, W0 summary, host slice, V0 results
app/                   OpenHarmony ArkTS app + NAPI native module
host/napi/             CMake shim; the canonical NAPI source is app/entry/src/main/cpp/entry.cpp
```

## Build and test (local, no device)

```bash
# V0: host .so + ohpm install + HAP (unsigned)
bash scripts/build.sh

# Local self-signed HAP (verify-only; NOT installable)
#   export Q207_SIGN_STORE_PWD=...   (or create gitignored signing/.local_pwd)
bash scripts/sign_hap.sh

# HOST-ONLY C++/TS slice: build + 433 assertions + bench + differential + evidence
bash scripts/build_host.sh
```

> Field table: `bash scripts/codegen.sh --check` (and `build.sh` / `build_host.sh` run it
> automatically). Native build is integrated through `externalNativeOptions`; `scripts/build.sh`
> builds the standalone `.so` and the HAP. `scripts/build_hap.sh` is the hvigor-only shortcut and now
> runs `ohpm install` first. `scripts/sign_hap.sh` only signs/verifies locally; its output is
> `installable=false` because the CA is not trusted by a device and no UDID profile exists.

## Current next steps

1. implement `.proto → descriptor/IR → C++/ArkTS` codegen (currently hand-written POD/decoder);
2. replace the `ping`/`add` NAPI surface with `decode`/`decodeMany` (P3 batch first);
3. lock API generation/acceptance baseline (current HAP targets API 24 / compileSdk 6.1.1.125);
4. when a board + official signing material/UDID exist: install, run `ohosTest`, record `ping/add`
   and AOT evidence, then measure device performance.

## License

**TBD.** No license is granted yet. Apache-2.0 is the proposed outbound license, pending the school/club IP and open-source authorization record. Do not redistribute until `LICENSE` is added.
