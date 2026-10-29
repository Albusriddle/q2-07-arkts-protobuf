import entry from 'libentry.so';
import { describe, it, expect } from '@ohos/hypium';
export default function entryNapiTest() {
  describe('EntryNapi', () => {
    it('ping_returns_pong', 0, () => { expect(entry.ping()).assertEqual('pong'); });
    it('add_2_3_returns_5', 0, () => { expect(entry.add(2, 3)).assertEqual(5); });
  });
}
