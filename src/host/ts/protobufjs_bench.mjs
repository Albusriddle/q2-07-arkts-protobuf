// HOST-ONLY protobufjs 7.6.5 baseline benchmark for the Q2-07 data cards.
// This is NOT a device measurement and must not be quoted as 1.5x/700 ms.
import { readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import protobuf from 'protobufjs';

const require = createRequire(import.meta.url);
const here = path.dirname(fileURLToPath(import.meta.url));
const protoPath = path.resolve(here, '../../../proto/example.proto');
const pbjsVersion = require('protobufjs/package.json').version;

const args = process.argv.slice(2);
function arg(name, fallback) {
  const i = args.indexOf(name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : fallback;
}
const cardPath = arg('--card');
const dataset = arg('--dataset', 'unknown');
const csvPath = arg('--csv');
const reps = Math.max(1, Number(arg('--reps', '3')));
if (!cardPath || !csvPath) {
  console.error('usage: protobufjs_bench.mjs --card <path> --dataset <name> --reps <n> --csv <file> [--header]');
  process.exit(2);
}

const card = readFileSync(cardPath);
const frames = [];
let payload = 0;
for (let pos = 0; pos + 4 <= card.length;) {
  const len = card.readUInt32LE(pos);
  pos += 4;
  if (pos + len > card.length) throw new Error(`frame overruns card at ${pos}`);
  frames.push(card.subarray(pos, pos + len));
  payload += len;
  pos += len;
}

const root = await protobuf.load(protoPath);
const Batch = root.lookupType('q207.demo.UserBatch');

function decodePass(toObject) {
  let messages = 0;
  for (const frame of frames) {
    const msg = Batch.decode(frame);
    if (toObject) {
      Batch.toObject(msg, { defaults: true, longs: String });
    }
    messages += msg.users ? msg.users.length : 0;
  }
  return messages;
}

// Warm the JIT and protobufjs code paths once.
let messages = decodePass(false);
decodePass(true);
const samples = [];
const samplesObj = [];
for (let r = 0; r < reps; r += 1) {
  const t0 = process.hrtime.bigint();
  decodePass(false);
  const t1 = process.hrtime.bigint();
  decodePass(true);
  const t2 = process.hrtime.bigint();
  samples.push(Number(t1 - t0) / 1e6);
  samplesObj.push(Number(t2 - t1) / 1e6);
}

function median(xs) {
  const a = [...xs].sort((x, y) => x - y);
  const n = a.length;
  return n % 2 ? a[(n - 1) / 2] : 0.5 * (a[n / 2 - 1] + a[n / 2]);
}

function rowsFor(prefix, times) {
  const med = median(times);
  const mbps = payload / 1e6 / (med / 1000);
  const mps = messages / (med / 1000);
  const ns = (med * 1e6) / messages;
  return [
    [prefix + '_mb_per_s', mbps.toFixed(6), 'MB/s (payload bytes/1e6 per s)'],
    [prefix + '_msg_per_s', mps.toFixed(3), 'messages/s'],
    [prefix + '_ns_per_msg', ns.toFixed(6), 'ns/message'],
    [prefix + '_median_ms', med.toFixed(6), 'ms/full card'],
  ];
}

const header = [
  'scope', 'dataset', 'card_file', 'card_bytes', 'payload_bytes', 'frames', 'messages', 'reps',
  'metric', 'value', 'unit', 'protobufjs_version', 'node_version', 'host_os',
];
const rows = [];
for (const [metric, value, unit] of rowsFor('wire_decode', samples)) {
  rows.push(['HOST-ONLY', dataset, cardPath, card.length, payload, frames.length, messages, reps,
    metric, value, unit, pbjsVersion, process.version, process.platform]);
}
for (const [metric, value, unit] of rowsFor('decode_plus_toObject', samplesObj)) {
  rows.push(['HOST-ONLY', dataset, cardPath, card.length, payload, frames.length, messages, reps,
    metric, value, unit, pbjsVersion, process.version, process.platform]);
}
const headerWanted = args.includes('--header');
const append = args.includes('--append');
const lines = [];
if (append) {
  try {
    lines.push(...readFileSync(csvPath, 'utf8').trimEnd().split('\n'));
  } catch {
    /* first write */
  }
} else if (headerWanted) {
  lines.push(header.join(','));
}
for (const r of rows) lines.push(r.map((v) => String(v)).join(','));
writeFileSync(csvPath, lines.join('\n') + '\n');
console.log(`protobufjs bench: ${dataset} ${frames.length} frames, ${messages} messages, reps=${reps}`);
console.log(`  decode median ${median(samples).toFixed(3)} ms, decode+toObject median ${median(samplesObj).toFixed(3)} ms`);
