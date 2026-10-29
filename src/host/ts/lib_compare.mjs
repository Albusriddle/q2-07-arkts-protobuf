// Q2-07 HOST-ONLY shared comparison helpers for the differential harness.

const FIELD_NAME = {
  1: 'id',
  2: 'name',
  3: 'tags',
  4: 'profile',
  5: 'email',
  6: 'phone',
  7: 'level',
  8: 'scores',
};

const PROFILE_NAME = { 1: 'city', 2: 'age' };

/** Object built only from the fields the wire actually carried (presence-driven). */
export function presenceObject(norm, presentFields, profileFields) {
  const out = {};
  for (const f of presentFields) {
    if (f === 4) {
      const p = {};
      for (const pf of profileFields) p[PROFILE_NAME[pf]] = norm.profile[PROFILE_NAME[pf]];
      out.profile = p;
    } else {
      out[FIELD_NAME[f]] = norm[FIELD_NAME[f]];
    }
  }
  return out;
}

/** Structural diff with JSON paths. Returns [] when equal. */
export function diffObjects(a, b, prefix = '$', out = []) {
  if (a === b) return out;
  const ta = typeof a;
  const tb = typeof b;
  if (a === null || b === null || ta !== 'object' || tb !== 'object') {
    out.push({ path: prefix, a, b });
    return out;
  }
  const aArr = Array.isArray(a);
  const bArr = Array.isArray(b);
  if (aArr !== bArr) {
    out.push({ path: prefix, a: aArr ? 'array' : 'object', b: bArr ? 'array' : 'object' });
    return out;
  }
  if (aArr) {
    if (a.length !== b.length) out.push({ path: `${prefix}.length`, a: a.length, b: b.length });
    const n = Math.min(a.length, b.length);
    for (let i = 0; i < n; i += 1) diffObjects(a[i], b[i], `${prefix}[${i}]`, out);
    return out;
  }
  const keys = new Set([...Object.keys(a), ...Object.keys(b)]);
  for (const k of keys) {
    if (!(k in a)) {
      out.push({ path: `${prefix}.${k}`, a: '<missing>', b: b[k] });
    } else if (!(k in b)) {
      out.push({ path: `${prefix}.${k}`, a: a[k], b: '<missing>' });
    } else {
      diffObjects(a[k], b[k], `${prefix}.${k}`, out);
    }
  }
  return out;
}
