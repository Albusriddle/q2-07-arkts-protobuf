// Q2-07 HOST-ONLY large-tape materializer check.
//
// Reads a tape v0 written by C++ (host_decode_dump --framed), materializes it with
// materializer.ts, and compares the resulting ordinary objects with the C++ norm
// dump of the same messages.  Also reports materializer timing for orientation
// only (HOST-ONLY; not a device number, not an ArkTS number).
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { materialize } from '../materializer/materializer.ts';
import { diffObjects } from './lib_compare.mjs';

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
const tapePath = args.tape;
const normPath = args.norm;
const outPath = args.out || 'materializer_check.json';
if (!tapePath || !normPath) {
  console.error('usage: node materializer_check.mjs --tape <f> --norm <f> [--out <f>]');
  process.exit(2);
}

const sha256 = (b) => createHash('sha256').update(b).digest('hex');
const tape = readFileSync(tapePath);
const normText = readFileSync(normPath, 'utf8');
const norm = JSON.parse(normText);

const t0 = process.hrtime.bigint();
const users = materialize(new Uint8Array(tape));
const t1 = process.hrtime.bigint();
const ms = Number(t1 - t0) / 1e6;

const materializedText = JSON.stringify({ users });
const diffs = diffObjects({ users }, norm);
const report = {
  scope: 'HOST-ONLY',
  tape_file: tapePath,
  tape_bytes: tape.length,
  tape_sha256: sha256(tape),
  norm_file: normPath,
  norm_sha256: sha256(normText),
  materialized_sha256: sha256(materializedText),
  users: users.length,
  roots: users.length,
  materialize_ms: Number(ms.toFixed(3)),
  users_per_s: ms > 0 ? Math.round((users.length / ms) * 1000) : 0,
  json_bytes: materializedText.length,
  exact_text_match: materializedText === normText,
  graph_diffs: diffs.length,
  first_diffs: diffs.slice(0, 10),
  ok: diffs.length === 0,
};

writeFileSync(outPath, JSON.stringify(report, null, 2));
console.log(
  `materializer: ${report.users} users, tape ${report.tape_bytes} B, ` +
    `${report.materialize_ms} ms (${report.users_per_s} users/s, HOST-ONLY)`,
);
console.log(`  graph diffs: ${report.graph_diffs}, exact text match: ${report.exact_text_match}`);
console.log(`  materialized sha256: ${report.materialized_sha256}`);
if (!report.ok) {
  console.error(JSON.stringify(report.first_diffs, null, 2));
  process.exit(1);
}
console.log('materializer: PASS');
