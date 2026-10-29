import entry from 'libentry.so';

@Entry
@Component
struct Index {
  @State pingText: string = 'not called';
  @State sumText: string = 'not called';

  aboutToAppear(): void {
    this.pingText = entry.ping();
    this.sumText = `${entry.add(2, 3)}`;
  }

  build() {
    Column({ space: 16 }) {
      Text('Q2-07 V0 NAPI slice')
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
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .padding(24)
  }
}
