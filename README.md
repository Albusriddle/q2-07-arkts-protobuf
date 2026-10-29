# Q2-07 · Native ArkTS High-Performance Protobuf Parsing

> Status: **W0 host-stage scaffold** (private, pre-implementation)
> Feasibility: **B- / conditionally viable** under “API-level compatibility + P3-batch first + hot-message P4 + 20 weeks”.
> This repository is a working scaffold, not a claim that 1.5×/700 ms is achieved.

## What this is

A feasibility + implementation workspace for the OpenHarmony Q2-07 challenge:

- decode Protobuf produced by existing ArkTS apps faster than the current `protobuf.js` path;
- keep the **ArkTS-facing API** unchanged at the API/signature level;
- generate C++/ArkTS code from `.proto`, with a reproducible benchmark and evidence chain.

The current stage is **W0: platform/schema/measurement/security/legal/product/architecture gates**. Device-side performance numbers are not yet available.

## Read first

| Document | Why |
|---|---|
| [`docs/feasibility-reestimate.md`](docs/feasibility-reestimate.md) | Resource-available feasibility re-estimate: B-, conditions, hard risks, 20-week route |
| [`docs/w0-execution-summary.md`](docs/w0-execution-summary.md) | What W0 executed; gate statuses; what is resource-gated vs technically hard |
| [`docs/w0-resource-assumption.md`](docs/w0-resource-assumption.md) | Why W0 `BLOCKED` items are reclassified as `RESOURCE-GATED`, not no-go |

## Scope and non-goals

**In scope (first phase)**

- host-side vertical slice: `.proto` → descriptor/IR → wire decoder → tape → materializer;
- P3-batch (one NAPI call + N object constructions, no tape) as the first native path;
- differential/golden tests against `protobufjs` 7.6.5 and 8.x;
- benchmark harness, data cards, statistics, replay scripts;
- API-level compatibility contract and documented value-semantics differences.

**Not claimed**

- exact `Object.keys`/`JSON.stringify`/oneof value semantics of `protobuf.js`;
- 4.29× or 700 ms on all datasets;
- full ANI backend or full P4 row-tape generator before V3/G1 evidence;
- “zero crashes” for malicious input; only bounded behavior + fuzz + stress tests.

## Repository layout

```
proto/                 sample .proto for host vertical slice
src/host/              host-side C++/TS reference implementation (planned)
bench/                 benchmark harness, data cards, replay kit (planned)
scripts/               codegen / validation / CI helpers (planned)
docs/                  feasibility, W0 summary, assumptions
```

## Next 7 days (host-only, no board required)

1. lock `protobufjs` 7.6.5 vs 8.x and the target API/ArkTS generation;
2. build the host vertical slice on `proto/example.proto`;
3. differential/golden tests vs protobufjs;
4. 100 MB synthetic data card + benchmark harness;
5. codegen skeleton (C++/ArkTS) and a TS reference materializer;
6. then, when the board arrives, run V0/V1/V3/V4 device gates.

## License

**TBD.** No license is granted yet. Apache-2.0 is the proposed outbound license, pending the school/club IP and open-source authorization record. Do not redistribute until `LICENSE` is added.
