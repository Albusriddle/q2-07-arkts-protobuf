# Q2-07 · Native ArkTS High-Performance Protobuf Parsing

> Status: **V0.8 adds a host-only native vs protobufjs benchmark**; generated message/nested/oneof POD+decoder; HAP built (not device-run).
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
- V0.8 host benchmark (native vs protobufjs 7.6.5); V0.7 map hardening + CI/ASan docs; V0.6 generic message codegen; V0.5 fake NAPI host tests; V0.4 generated decoder; NAPI bridge (`ping`/`add`/`decodeUser`/`decodeMany`) unchanged in `app/entry/src/main/cpp/`,
  built by hvigor `externalNativeOptions`; `decodeUser` returns the norm-v1 object shape;
- field numbers, wire types, POD structs, schema IR, decoder body and ArkTS types generated
  from `proto/example.proto` (`scripts/codegen.sh --check`);
- `ohosTest` test HAP builds (not executed: no device); `decodeUser` is **compiled,
  not executed on a device** (see `docs/v0.1-decode.md`).

**Not implemented yet (do not claim as done)**

- full generic `.proto` compiler; message/nested/oneof/map codegen is generic for the supported scalar subset; NAPI bindings remain hand-written;
- `decode` alias / full P3 handle-scope tuning remain;
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
| [`docs/v0.2-decode-many.md`](docs/v0.2-decode-many.md) | V0.2 batch `decodeMany`, limits and NAPI compile-only claim |
| [`docs/v0.3-codegen.md`](docs/v0.3-codegen.md) | V0.3 schema IR + generated POD + ArkTS types, regression and limits |
| [`docs/v0.4-decoder.md`](docs/v0.4-decoder.md) | V0.4 generated decoder body, 468-assertion regression and limits |
| [`docs/v0.5-napi-shim.md`](docs/v0.5-napi-shim.md) | V0.5 fake NAPI host runtime tests, scope/error checks and limits |
| [`docs/v0.6-generic-codegen.md`](docs/v0.6-generic-codegen.md) | V0.6 message-graph POD/decoder/DTS generation and limits |
| [`docs/v0.7-hardening.md`](docs/v0.7-hardening.md) | V0.7 map/CI/docs hardening and remaining limits |
| [`docs/v0.8-benchmark.md`](docs/v0.8-benchmark.md) | V0.8 host-only native vs protobufjs benchmark; no 1.5x claim |
| [`docs/history-rewrite.md`](docs/history-rewrite.md) | Old test secret history rewrite and PR-ref caveat |

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

## Build and test (local, Windows + DevEco)

Prerequisites: DevEco Studio + HarmonyOS SDK/NDK, Node/npm, Python 3, and
`DEVECO_HOME` if DevEco is not at its default path. `PYTHON` can point at a
specific interpreter (auto-detected: working `python`, else `python3`).

```bash
# V0: host .so + ohpm install + HAP (unsigned)
bash scripts/build.sh

# Local self-signed HAP (verify-only; NOT installable)
#   export Q207_SIGN_STORE_PWD=...   (or create gitignored signing/.local_pwd)
bash scripts/sign_hap.sh

# HOST-ONLY C++/TS slice: build + 468 assertions + bench + differential + evidence
bash scripts/build_host.sh   # includes fake-NAPI napi-shim step
```

> Schema/codegen: `bash scripts/codegen.sh --check` (and `build.sh` / `build_host.sh` run it
> automatically). Native build is integrated through `externalNativeOptions`; `scripts/build.sh`
> builds the standalone `.so` and the HAP. `scripts/build_hap.sh` is the hvigor-only shortcut and now
> runs `ohpm install` first. `scripts/sign_hap.sh` only signs/verifies locally; its output is
> `installable=false` because the CA is not trusted by a device and no UDID profile exists.

## Current next steps

1. optimize native decode allocation/arena and compare protoc-gen-napi/Turbo on the same conformance card;
2. finish the P3 path: `decode` alias/batch ergonomics, handle-scope tuning and external buffer/worker policy;
3. lock API generation/acceptance baseline (current HAP targets API 24 / compileSdk 6.1.1.125);
4. when a board + official signing material/UDID exist: install, run `ohosTest`, record `ping/add`
   and AOT evidence, then measure device performance.

## License

This repository is licensed under the **Apache License, Version 2.0**. See [LICENSE](LICENSE).

Third-party dependencies retain their own licenses (for example, `protobufjs` is BSD-3-Clause); their license texts are not relicensed by this repository.
