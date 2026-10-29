// Q2-07 HOST-ONLY differential corpus generator (protobufjs 7.6.5 baseline).
//
// For every corpus case this writes:
//   <out>/<stem>.wire       wire bytes (produced by protobufjs encode, except the
//                           hand-built non-canonical / unknown-field cases)
//   <out>/<stem>.pbjs.json  protobufjs toObject({defaults:true, longs:String})
//   <out>/corpus.json       index: stem, sha256(wire), note, byteRoundtrip flag
//
// The C++ side (host_decode_dump --corpus) then produces <stem>.norm.json and
// <stem>.tape.bin for the same wire bytes; differential.mjs compares the three
// producers.  Nothing here is a device measurement.
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import protobuf from 'protobufjs';

const require = createRequire(import.meta.url);
const protobufVersion = require('protobufjs/package.json').version;
const here = path.dirname(fileURLToPath(import.meta.url));
const protoPath = path.resolve(here, '../../../proto/example.proto');
const outDir = process.argv[2] || path.resolve(here, '../../../evidence/host/corpus');

const sha256 = (buf) => createHash('sha256').update(buf).digest('hex');
const hex = (buf) => buf.toString('hex');

const root = await protobuf.load(protoPath);
const User = root.lookupType('q207.demo.User');

// protobufjs 7.6.5 defaults:true / longs:String is exactly the norm-v1 shape.
const normOf = (msg) => User.toObject(msg, { defaults: true, longs: String });

function encode(obj) {
  // fromObject() coerces int64 strings -> Long; verify() then checks the real types.
  const msg = User.fromObject(obj);
  const err = User.verify(msg);
  if (err) throw new Error(`corpus object invalid: ${err}`);
  return Buffer.from(User.encode(msg).finish());
}

const cases = [];
function add(stem, opts) {
  cases.push({ stem, ...opts });
}

/** Encoded by protobufjs from an object. */
add('c01_empty', { obj: {} });
add('c02_id_only', { obj: { id: '1' } });
add('c03_explicit_zeros', { obj: { id: 0, name: '' } });
add('c04_int64_min', { obj: { id: '-9223372036854775808' } });
add('c05_int64_max', { obj: { id: '9223372036854775807' } });
add('c06_int64_negative', { obj: { id: '-1' } });
add('c07_unicode_name', { obj: { name: '\u4f60\u597d\ud83d\ude00' } });
add('c08_escaped_name', { obj: { name: 'a"b\\c\n\t\u0001' } });
add('c09_tags', { obj: { tags: ['a', '', 'c'] } });
add('c10_tags_many', { obj: { tags: Array.from({ length: 16 }, (_, i) => `tag-${i}`) } });
add('c11_profile_empty', { obj: { profile: {} } });
add('c12_profile_city', { obj: { profile: { city: 'Hangzhou' } } });
add('c13_profile_age0', { obj: { profile: { city: 'X', age: 0 } } });
add('c14_oneof_email', { obj: { email: 'e@x' } });
add('c15_oneof_phone', { obj: { phone: '123' } });
add('c16_level_zero', { obj: { level: 0 } });
add('c17_level_negative', { obj: { level: -5 } });
add('c18_level_max', { obj: { level: 2147483647 } });
add('c19_map_multi', { obj: { scores: { math: 95, art: -1, zero: 0 } } });
add('c20_full', {
  obj: {
    id: '9007199254740993',
    name: 'alice',
    tags: ['a', 'b'],
    profile: { city: 'X', age: 3 },
    email: 'e@x',
    level: 7,
    scores: { k1: 1, k2: 2 },
  },
});

// Concatenated encodes: protobuf merge semantics (7.6.5 keeps both oneof members,
// map duplicate keys resolve last-wins).
add('c21_oneof_both', {
  concat: [{ email: 'e@x' }, { phone: '123' }],
  note: 'two encodes concatenated: protobufjs 7.6.5 keeps BOTH oneof members',
});
add('c22_map_dup_key', {
  concat: [{ scores: { k: 1 } }, { scores: { k: 2 } }],
  note: 'duplicate map key: last value wins',
  byteRoundtrip: false,
  byteDiffReason: 'duplicate map key collapses to one entry; canonical re-encode writes 1 entry',
});

// Hand-built wire bytes (documented so byte round-trip is not expected).
add('c23_noncanonical_varint', {
  wireHex: '088000',
  note: 'id=0 encoded as a non-shortest 2-byte varint (must be accepted)',
  byteRoundtrip: false,
  byteDiffReason: 'non-shortest varint: a canonical encoder emits 1 byte instead of 2',
});
add('c24_unknown_field', {
  concat: [{ id: '5' }],
  extraHex: '4807',
  note: 'one unknown varint field appended; unknown fields are skipped',
  byteRoundtrip: false,
  byteDiffReason: 'unknown field is discarded by design (proto3 default), so it is not re-emitted',
});
add('c25_neg_int32_10byte', {
  wireHex: '38ffffffffffffffffff01',
  note: 'negative int32 (level) as a 10-byte varint',
});

mkdirSync(outDir, { recursive: true });

const index = [];
for (const c of cases) {
  let wire;
  if (c.wireHex) {
    wire = Buffer.from(c.wireHex, 'hex');
  } else if (c.concat) {
    wire = Buffer.concat(c.concat.map(encode));
    if (c.extraHex) wire = Buffer.concat([wire, Buffer.from(c.extraHex, 'hex')]);
  } else {
    wire = encode(c.obj);
  }
  const msg = User.decode(wire);
  const pbjsNorm = normOf(msg);
  writeFileSync(path.join(outDir, `${c.stem}.wire`), wire);
  writeFileSync(path.join(outDir, `${c.stem}.pbjs.json`), JSON.stringify(pbjsNorm));
  index.push({
    stem: c.stem,
    wire_bytes: wire.length,
    wire_hex: hex(wire),
    wire_sha256: sha256(wire),
    pbjs_sha256: sha256(JSON.stringify(pbjsNorm)),
    own_keys: Object.keys(msg),
    bytes_expected_identical: c.byteRoundtrip === false ? false : true,
    byte_diff_reason: c.byteDiffReason || '',
    note: c.note || '',
  });
}

writeFileSync(
  path.join(outDir, 'corpus.json'),
  JSON.stringify(
    {
      scope: 'HOST-ONLY',
      schema: 'proto/example.proto (q207.demo.User)',
      baseline: `protobufjs ${protobufVersion}`,
      toObject_options: { defaults: true, longs: 'String' },
      items: index,
    },
    null,
    2,
  ),
);
console.log(`corpus: ${index.length} cases -> ${outDir}`);
for (const it of index) console.log(`  ${it.stem.padEnd(24)} ${String(it.wire_bytes).padStart(4)} B  ${it.wire_hex.slice(0, 48)}`);
