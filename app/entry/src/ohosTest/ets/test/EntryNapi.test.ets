import entry from 'libentry.so';
import { describe, it, expect } from '@ohos/hypium';

const SAMPLE_HEX =
  '0881808080808080101205616c6963651a01611a016222050a015810032a036540783807' +
  '42060a026b31100142060a026b321002';

function sampleBytes(): Uint8Array {
  const out = new Uint8Array(SAMPLE_HEX.length / 2);
  for (let i = 0; i < out.length; i++) {
    out[i] = parseInt(SAMPLE_HEX.substring(i * 2, i * 2 + 2), 16);
  }
  return out;
}

export default function entryNapiTest() {
  describe('EntryNapi', () => {
    it('ping_returns_pong', 0, () => { expect(entry.ping()).assertEqual('pong'); });
    it('add_2_3_returns_5', 0, () => { expect(entry.add(2, 3)).assertEqual(5); });
    // Compile-only in this environment; runs when a device is available.
    it('decodeUser_c20_full', 0, () => {
      const u = entry.decodeUser(sampleBytes());
      expect(u.id).assertEqual('9007199254740993');
      expect(u.name).assertEqual('alice');
      expect(u.profile?.city).assertEqual('X');
      expect(u.scores.k1).assertEqual(1);
    });
  });
}
