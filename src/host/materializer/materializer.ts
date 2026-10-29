// Q2-07 HOST-ONLY reference materializer.
//
// Reads the row tape v0 written by src/host/tape/tape.cpp and builds *plain
// JavaScript objects*.  This is NOT ArkTS and NOT the NAPI path: it is the logic
// reference for "tape -> object graph", so the materializer can be judged against
// protobufjs 7.6.5 on the same wire bytes before anything is ported to native.
//
// Every offset constant below mirrors src/host/tape/tape.h; the C++ verifier is
// the authority on structural validity, this reader assumes a verified buffer and
// still bounds-checks every access it performs.
//
// Written with erasable TypeScript only (no enums / namespaces / parameter
// properties) so Node can run it directly with type stripping.

export const TAPE_LAYOUT = {
  HEADER_BYTES: 80,
  USER_RECORD_SIZE: 80,
  PROFILE_RECORD_SIZE: 16,
  TAG_SLOT_BYTES: 8,
  SCORE_SLOT_BYTES: 12,
  ABSENT: 0xffffffff,
  MAGIC: 'Q207TAP0',
} as const;

export interface TapeHeader {
  version: number;
  headerBytes: number;
  userRecordSize: number;
  profileRecordSize: number;
  userCount: number;
  profileCount: number;
  rootsOff: number;
  rootsCount: number;
  tapeOff: number;
  tapeBytes: number;
  blobOff: number;
  blobBytes: number;
  valuesOff: number;
  valuesBytes: number;
  crc32: number;
  flags: number;
  presenceWords: number;
}

export interface MaterializedProfile {
  city: string;
  age: number;
}

export interface MaterializedUser {
  id: string;
  name: string;
  tags: string[];
  profile: MaterializedProfile | null;
  email?: string;
  phone?: string;
  level?: number;
  scores: Record<string, number>;
}

const textDecoder = new TextDecoder('utf-8', { fatal: false });

export function parseHeader(bytes: Uint8Array): TapeHeader {
  if (bytes.byteLength < TAPE_LAYOUT.HEADER_BYTES) {
    throw new RangeError('tape shorter than header');
  }
  const magic = textDecoder.decode(bytes.subarray(0, 8));
  if (magic !== TAPE_LAYOUT.MAGIC) {
    throw new Error(`bad tape magic: ${JSON.stringify(magic)}`);
  }
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return {
    version: dv.getUint32(8, true),
    headerBytes: dv.getUint32(12, true),
    userRecordSize: dv.getUint32(16, true),
    profileRecordSize: dv.getUint32(20, true),
    userCount: dv.getUint32(24, true),
    profileCount: dv.getUint32(28, true),
    rootsOff: dv.getUint32(32, true),
    rootsCount: dv.getUint32(36, true),
    tapeOff: dv.getUint32(40, true),
    tapeBytes: dv.getUint32(44, true),
    blobOff: dv.getUint32(48, true),
    blobBytes: dv.getUint32(52, true),
    valuesOff: dv.getUint32(56, true),
    valuesBytes: dv.getUint32(60, true),
    crc32: dv.getUint32(64, true),
    flags: dv.getUint32(68, true),
    presenceWords: dv.getUint32(72, true),
  };
}

/** Tape reader: same four regions as the C++ writer (roots/tape/blob/values). */
export class TapeReader {
  bytes: Uint8Array;
  dv: DataView;
  header: TapeHeader;

  constructor(bytes: Uint8Array) {
    this.bytes = bytes;
    this.dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    this.header = parseHeader(bytes);
    if (this.header.version !== 1) throw new Error(`unsupported tape version ${this.header.version}`);
    if (this.header.userRecordSize !== TAPE_LAYOUT.USER_RECORD_SIZE) {
      throw new Error(`unexpected user record size ${this.header.userRecordSize}`);
    }
    if (this.header.profileRecordSize !== TAPE_LAYOUT.PROFILE_RECORD_SIZE) {
      throw new Error(`unexpected profile record size ${this.header.profileRecordSize}`);
    }
    // The C++ verifier is authoritative, but this reader is also used directly by
    // the TS harness; reject malformed section order/overlap before any access.
    const h = this.header;
    if (h.headerBytes !== TAPE_LAYOUT.HEADER_BYTES) throw new Error(`bad headerBytes ${h.headerBytes}`);
    if (h.presenceWords !== 1) throw new Error(`bad presenceWords ${h.presenceWords}`);
    if (h.rootsOff !== TAPE_LAYOUT.HEADER_BYTES) throw new Error(`bad rootsOff ${h.rootsOff}`);
    const rootsEnd = h.rootsOff + h.rootsCount * 4;
    const tapeEnd = h.tapeOff + h.tapeBytes;
    const blobEnd = h.blobOff + h.blobBytes;
    const valuesEnd = h.valuesOff + h.valuesBytes;
    if (rootsEnd > h.tapeOff || tapeEnd > h.blobOff || blobEnd > h.valuesOff) {
      throw new Error('tape sections overlap or are out of order');
    }
    if (valuesEnd > bytes.byteLength) throw new Error('tape sections exceed buffer');
  }

  private u32(off: number): number {
    return this.dv.getUint32(off, true);
  }

  private i32(off: number): number {
    return this.dv.getInt32(off, true);
  }

  private i64asString(off: number): string {
    return this.dv.getBigInt64(off, true).toString();
  }

  /** UTF-8 bytes from the blob region.  Strict UTF-8 was validated by the decoder. */
  blob(off: number, len: number): string {
    const start = this.header.blobOff + off;
    if (off + len > this.header.blobBytes || start + len > this.bytes.byteLength) {
      throw new RangeError('blob reference out of range');
    }
    return textDecoder.decode(this.bytes.subarray(start, start + len));
  }

  rootAt(i: number): number {
    if (!Number.isInteger(i) || i < 0 || i >= this.header.rootsCount) {
      throw new RangeError('root index out of range');
    }
    return this.u32(this.header.rootsOff + i * 4);
  }

  userAt(recOff: number): MaterializedUser {
    if (recOff < 0 || recOff + TAPE_LAYOUT.USER_RECORD_SIZE > this.header.tapeBytes) {
      throw new RangeError('user record out of range');
    }
    const base = this.header.tapeOff + recOff;
    const presence = this.u32(base + 0);
    const contactBits = this.u32(base + 4);
    // Assignment order is deliberate: it mirrors the C++ norm dump key order and
    // keeps the object shape stable (report v3 §5.5.2 rule 1).
    const u = {} as MaterializedUser;
    u.id = this.i64asString(base + 16);
    u.name = this.blob(this.u32(base + 24), this.u32(base + 28));
    u.tags = [];
    u.profile = null;

    // tags: element slots in the values region (byte offset + element count)
    const tagCount = this.u32(base + 32);
    const tagOff = this.u32(base + 36);
    for (let i = 0; i < tagCount; i += 1) {
      const slot = this.header.valuesOff + tagOff + i * TAPE_LAYOUT.TAG_SLOT_BYTES;
      u.tags.push(this.blob(this.u32(slot), this.u32(slot + 4)));
    }

    // nested message: separate 16-byte record addressed by tape byte offset
    const profOff = this.u32(base + 40);
    if (profOff !== TAPE_LAYOUT.ABSENT) {
      const pr = this.header.tapeOff + profOff;
      u.profile = {
        city: this.blob(this.u32(pr + 0), this.u32(pr + 4)),
        age: this.i32(pr + 8),
      };
    }

    // real oneof members: key present only when the member bit is set
    if ((contactBits & (1 << 5)) !== 0) u.email = this.blob(this.u32(base + 44), this.u32(base + 48));
    if ((contactBits & (1 << 6)) !== 0) u.phone = this.blob(this.u32(base + 52), this.u32(base + 56));

    // proto3 optional: primary presence bit decides "assign or not"
    if ((presence & (1 << 6)) !== 0) u.level = this.i32(base + 60);

    // Null-prototype object: a wire map key such as "__proto__" can never
    // mutate Object.prototype (C++ strict mode rejects those keys, but the
    // materializer must be safe even when handed a compat-mode tape).
    const scores: Record<string, number> = Object.create(null);
    u.scores = scores;

    // map<string,int32>: slots of {u32 key_off, u32 key_len, i32 value}
    const scoreCount = this.u32(base + 64);
    const scoreOff = this.u32(base + 68);
    for (let i = 0; i < scoreCount; i += 1) {
      const slot = this.header.valuesOff + scoreOff + i * TAPE_LAYOUT.SCORE_SLOT_BYTES;
      const key = this.blob(this.u32(slot), this.u32(slot + 4));
      // last value wins, first insertion position kept (JS object semantics)
      scores[key] = this.i32(slot + 8);
    }
    return u;
  }

  /** Materializes every root record.  Roots are byte offsets into `tape`. */
  materialize(): MaterializedUser[] {
    const out: MaterializedUser[] = [];
    for (let i = 0; i < this.header.rootsCount; i += 1) {
      out.push(this.userAt(this.rootAt(i)));
    }
    return out;
  }
}

export function materialize(bytes: Uint8Array): MaterializedUser[] {
  return new TapeReader(bytes).materialize();
}
