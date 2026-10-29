# Host-side vertical slice (planned)

Goal: prove the **logic** without a board.

```
proto/example.proto
  -> descriptor/IR
  -> wire decoder (C++ or TS reference)
  -> tape writer/reader
  -> materializer (TS reference)
  -> differential/golden vs protobufjs 7.6.5 / 8.x
```

Planned files:

- `decoder_reference.ts` — minimal wire reader for the sample schema
- `tape.ts` — row tape layout v0 + verifier
- `materializer.ts` — TS reference materializer (not ArkTS)
- `differential.ts` — compare against protobufjs decode/toObject
- `README.md` — assumptions and known differences

This is **not** a device performance result. It is a correctness/logic harness that will be ported to C++/ArkTS later.
