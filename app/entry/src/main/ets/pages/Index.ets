import entry from 'libentry.so';

// c20_full.wire (52 bytes): id=2^53+1, name, tags, profile, email, level=7, scores.
const SAMPLE_HEX =
  '0881808080808080101205616c6963651a01611a016222050a015810032a036540783807' +
  '42060a026b31100142060a026b321002';

@Entry
@Component
struct Index {
  @State pingText: string = 'not called';
  @State sumText: string = 'not called';
  @State decodeText: string = 'not called';

  private sampleBytes(): Uint8Array {
    const out = new Uint8Array(SAMPLE_HEX.length / 2);
    for (let i = 0; i < out.length; i++) {
      out[i] = parseInt(SAMPLE_HEX.substring(i * 2, i * 2 + 2), 16);
    }
    return out;
  }

  aboutToAppear(): void {
    this.pingText = entry.ping();
    this.sumText = `${entry.add(2, 3)}`;
  }

  build() {
    Column({ space: 16 }) {
      Text('Q2-07 V0.1 NAPI decode slice')
        .fontSize(22)
        .fontWeight(FontWeight.Bold)
      Text(`ping(): ${this.pingText}`)
        .fontSize(16)
      Text(`add(2, 3): ${this.sumText}`)
        .fontSize(16)
      Button('Call ping/add again')
        .onClick(() => {
          this.pingText = entry.ping();
          this.sumText = `${entry.add(2, 3)}`;
        })
      Button('Decode c20_full sample')
        .onClick(() => {
          try {
            const decoded = entry.decodeUser(this.sampleBytes());
            this.decodeText = JSON.stringify(decoded);
          } catch (e) {
            this.decodeText = `decode error: ${e}`;
          }
        })
      Text(`decodeUser: ${this.decodeText}`)
        .fontSize(14)
        .maxLines(12)
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .padding(24)
  }
}
