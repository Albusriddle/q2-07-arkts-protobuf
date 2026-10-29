# protobufjs differential harness (HOST-ONLY)

Baseline: **protobufjs 7.6.5**, installed into this directory (`npm install`, pinned in
`package.json`). This is the reference the C++ decoder and the TS materializer are
judged against. It is a *host* harness: no ArkTS, no NAPI, no device.

## Scripts

| Script | Purpose |
|---|---|
| `gen_corpus.mjs <dir>` | builds the 25-case corpus, writes `<stem>.wire` + `<stem>.pbjs.json` + `corpus.json` |
| `differential.mjs --corpus <d> --cpp <d> --out <json>` | the three oracles (see below) |
| `materializer_check.mjs --tape <f> --norm <f> --out <json>` | large-tape materializer check (8,178 users) |
| `lib_compare.mjs` | shared structural diff + presence-driven object reconstruction |

## The three oracles

1. **Object graph** — C++ decode → norm-v1 JSON vs `User.toObject(msg, {defaults:true,
   longs:String})`. Equal means every key, value and omission matches.
2. **Bytes** — rebuild an object from the fields that were **present on the wire**
   (presence bitmap reported by the C++ decoder), re-encode with protobufjs and compare
   with the original bytes. Re-encoding the *defaults-expanded* object would be wrong:
   it materialises defaults the wire never carried.
3. **Materializer** — read the tape v0 written by C++, materialize plain objects with
   `materializer/materializer.ts`, compare with protobufjs's object graph.

## Norm-v1 rules (probed on 7.6.5, not assumed)

- `int64` → decimal string; `int32` → number
- absent `string` → `""`, `repeated` → `[]`, `map` → `{}`
- sub-message: nested object when present (even if empty), `null` when absent
- real `oneof` members and proto3 `optional`: key **omitted** when absent
- duplicate map keys: last value wins, first insertion position kept

## Explained, not-fixed differences

Three corpus cases deliberately differ at the byte level and are reported as
`explained_differences` instead of failures:

| case | why |
|---|---|
| `c22_map_dup_key` | duplicate map key collapses to one entry (JS object semantics), so re-encode writes 1 entry |
| `c23_noncanonical_varint` | `80 00` is accepted but a canonical encoder emits 1 byte |
| `c24_unknown_field` | unknown fields are discarded by proto3 default and are not re-emitted |

Any *other* byte or object-graph difference fails the run (non-zero exit).

## Offline degradation

`scripts/build_host.sh --no-npm` (or a machine where `npm install` fails) switches to
`../tools/py_fallback_diff.py`: an independent Python encoder + expected object graph,
14 cases. The report then says
`"baseline": "python-encoder-fallback (protobufjs unavailable: DEGRADED)"` and lists what
that baseline does **not** cover (protobufjs value semantics, byte round-trip).
