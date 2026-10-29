// Q2-07 HOST-ONLY differential harness: C++ decoder + TS materializer vs protobufjs 7.6.5.
//
// Three oracles over the same wire bytes:
//   A. object graph: C++ decode -> norm-v1 JSON  vs  protobufjs toObject({defaults:true,longs:String})
//   B. bytes:        protobufjs fromObject(C++ norm) -> encode  vs  original wire bytes
//   C. materializer: TS materializer(tape v0 written by C++)  vs  protobufjs object graph
//
// A mismatch is a defect unless it is listed in an already-signed difference list;
// this slice has none, so any mismatch fails the run (non-zero exit).
import { createRequire } from 'node:module';
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import protobuf from 'protobufjs';

import { materialize } from '../materializer/materializer.ts';
import { diffObjects, presenceObject } from './lib_compare.mjs';

const require = createRequire(import.meta.url);
const here = path.dirname(fileURLToPath(import.meta.url));

function parseArgs(argv) {
  const out = {};
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i].startsWith('--')) {
      out[argv[i].slice(2)] = argv[i + 1];
      i += 1;
    }
  }
  return out;
}
const args = parseArgs(process.argv.slice(2));
const corpusDir = args.corpus || path.resolve(here, '../../../evidence/host/corpus');
const cppDir = args.cpp || path.resolve(here, '../../../evidence/host/corpus_cpp');
const outPath = args.out || path.resolve(cppDir, 'differential.json');

const protoPath = path.resolve(here, '../../../proto/example.proto');
const root = await protobuf.load(protoPath);
const User = root.lookupType('q207.demo.User');

const corpus = JSON.parse(readFileSync(path.join(corpusDir, 'corpus.json'), 'utf8'));
const meta = JSON.parse(readFileSync(path.join(cppDir, 'meta.json'), 'utf8'));
const metaByStem = new Map(meta.items.map((it) => [it.stem, it]));

const report = {
  scope: 'HOST-ONLY',
  baseline: `protobufjs ${require('protobufjs/package.json').version}`,
  schema: 'proto/example.proto (q207.demo.User)',
  cases: 0,
  object_graph: { checked: 0, equal: 0, mismatches: [] },
  bytes: { checked: 0, identical: 0, explained_differences: [], mismatches: [] },
  materializer: { checked: 0, equal: 0, mismatches: [] },
};

for (const item of corpus.items) {
  const stem = item.stem;
  const cppMeta = metaByStem.get(stem);
  if (!cppMeta) {
    report.object_graph.mismatches.push({ stem, reason: 'C++ decoder produced no metadata' });
    continue;
  }
  report.cases += 1;

  const wire = readFileSync(path.join(corpusDir, `${stem}.wire`));
  const normPbjs = JSON.parse(readFileSync(path.join(corpusDir, `${stem}.pbjs.json`), 'utf8'));
  const normCpp = JSON.parse(readFileSync(path.join(cppDir, `${stem}.norm.json`), 'utf8'));

  // A. object graph
  report.object_graph.checked += 1;
  const diffs = diffObjects(normCpp, normPbjs);
  if (diffs.length === 0) {
    report.object_graph.equal += 1;
  } else {
    report.object_graph.mismatches.push({ stem, diffs });
  }

  // B. bytes: rebuild an object from the fields that were PRESENT on the wire
  //    (presence bitmap from the C++ decode), re-encode it with protobufjs and
  //    compare with the original bytes.  Re-encoding the defaults-expanded norm
  //    would be wrong: it would materialise defaults the wire never carried.
  if (item.bytes_expected_identical === false) {
    report.bytes.explained_differences.push({ stem, reason: item.byte_diff_reason });
  } else {
    report.bytes.checked += 1;
    const presentObj = presenceObject(normCpp, cppMeta.present_fields, cppMeta.profile_fields);
    const reencoded = Buffer.from(User.encode(User.fromObject(presentObj)).finish());
    if (reencoded.equals(wire)) {
      report.bytes.identical += 1;
    } else {
      report.bytes.mismatches.push({
        stem,
        present_fields: cppMeta.present_fields,
        original_hex: wire.toString('hex'),
        reencoded_hex: reencoded.toString('hex'),
      });
    }
  }

  // C. materializer: tape (written by C++) -> plain objects via materializer.ts
  const tape = readFileSync(path.join(cppDir, `${stem}.tape.bin`));
  report.materializer.checked += 1;
  const users = materialize(new Uint8Array(tape));
  if (users.length !== 1) {
    report.materializer.mismatches.push({ stem, reason: `expected 1 root, got ${users.length}` });
  } else {
    const mdiffs = diffObjects(users[0], normPbjs);
    if (mdiffs.length === 0) {
      report.materializer.equal += 1;
    } else {
      report.materializer.mismatches.push({ stem, diffs: mdiffs });
    }
  }
}

report.cases_in_corpus = corpus.items.length;
report.tape_verified = meta.items.filter((it) => it.tape_verified).length;
report.ok =
  report.object_graph.mismatches.length === 0 &&
  report.bytes.mismatches.length === 0 &&
  report.materializer.mismatches.length === 0;

writeFileSync(outPath, JSON.stringify(report, null, 2));
console.log(`differential: ${report.cases} cases`);
console.log(
  `  A object graph : ${report.object_graph.equal}/${report.object_graph.checked} equal`,
);
console.log(
  `  B bytes        : ${report.bytes.identical}/${report.bytes.checked} byte-identical ` +
    `(${report.bytes.explained_differences.length} explained differences)`,
);
console.log(
  `  C materializer : ${report.materializer.equal}/${report.materializer.checked} equal`,
);
if (!report.ok) {
  console.error('MISMATCHES:');
  console.error(JSON.stringify(report.object_graph.mismatches, null, 2));
  console.error(JSON.stringify(report.bytes.mismatches, null, 2));
  console.error(JSON.stringify(report.materializer.mismatches, null, 2));
  process.exit(1);
}
console.log('differential: PASS (no undocumented difference)');
