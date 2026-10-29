# Tests (HOST-ONLY)

`main.cpp` runs four suites and prints `TESTS PASSED: <n> FAILED: <m>`; the binary exits
non-zero on any failure. `bash repo/scripts/build_host.sh` runs it as the `unit-tests`
step and records the exit code in `evidence/host_slice.json`.

| Suite | File | Covers |
|---|---|---|
| `wire/*` | `test_wire.cpp` | varint boundaries (0 … 2^64−1), non-shortest encodings, 10th-byte rule, tag validation (field 0, range, wire 6/7, tag > 5 bytes), length decision order, fixed32/64, zigzag, packed varint/fixed32/zigzag, strict UTF-8, depth 100/101, skip-unknown without allocation, **allocation-order proof** |
| `golden/*` | `test_vectors.cpp` | the 38 W0 golden wire vectors + the three R1/D17 additions (non-shortest accepted, negative int32 → −1, 2^64−1 vs 10th-byte overflow) |
| `tape/*` | `test_tape.cpp` | round-trip, byte determinism, header/layout invariants, 13 verifier negatives, empty tape, 2,000-record tape |
| `decode/*` | `test_decode.cpp` | `example.proto` decode: empty defaults, full mixed schema, optional tri-state, unknown fields, strict/compat mismatch, dangerous map keys, map ordering, oneof, UTF-8 policy, limits, `UserBatch`, JSON escaping, int64 boundaries |

## The allocation-order proof

`test_wire.cpp` overrides global `operator new`/`delete` and counts bytes. For
pathological inputs (`12 ff ff ff ff 0f 00` = 4 GiB length prefix, `2^64−1`, …) it asserts:

- the reader reports `E_LENGTH_EXCEEDS_REMAINING` (never a cap/crash path),
- `Reader::alloc_calls() == 0` and `alloc_bytes() == 0`,
- the process-level allocation delta is `< 4096` bytes,
- and the test prints the measured delta.

This proves the *code path* does not allocate before validation. It is **not** an
ASan proof: a sanitizer run on the C++ build (host first, then device) is still open.

## `wire_vectors_data.inc` is generated — do not hand-edit

It is a byte-for-byte transliteration of
`out/w0/security/test_vectors/wire_vectors.json` (name / hex / expected / strict / limits),
copied into the repo so the build does not depend on `out/**` being present.

Regenerate (from the workspace root):

```bash
python - <<'PY'
import json
src = 'out/w0/security/test_vectors/wire_vectors.json'
dst = 'repo/src/host/tests/wire_vectors_data.inc'
d = json.load(open(src, encoding='utf-8'))
head = '''// GENERATED -- do not edit by hand.
// Source: out/w0/security/test_vectors/wire_vectors.json (38 vectors, W0 golden).
// Copied into the repo so the host build does not depend on out/** at build time.
// Regenerate: see repo/src/host/tests/README.md
#pragma once

namespace q207 { namespace test {

struct GoldenVector {
  const char* name;
  const char* hex;
  const char* expected;  // E_* code from the reference model
  int strict;            // -1 = default, 0 = compat arm, 1 = strict arm
  unsigned long long max_string_bytes;  // 0 = default
  unsigned long long max_objects;       // 0 = default
  unsigned long long max_fields;        // 0 = default
};

static const GoldenVector kGoldenVectors[] = {
'''
lines = [head]
for v in d['vectors']:
    strict = -1 if v['strict'] is None else (1 if v['strict'] else 0)
    lim = v.get('limits') or {}
    lines.append('    {"%s", "%s", "%s", %d, %dull, %dull, %dull},' % (
        v['name'], v['hex'], v['expected'], strict,
        lim.get('max_string_bytes', 0), lim.get('max_objects', 0), lim.get('max_fields', 0)))
lines.append('};\n\nstatic const unsigned kGoldenVectorCount = sizeof(kGoldenVectors)/sizeof(kGoldenVectors[0]);\n\n}}  // namespace q207::test\n')
open(dst, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
print('wrote', len(d['vectors']), 'vectors')
PY
```

If the golden file changes, the diff must be reviewed as a spec change — never edited
to make a test pass.
