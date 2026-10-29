# V0 最小切片：NAPI `.so` + ArkTS 调用 + 空 HAP

日期：2026-10-29 ｜ 分支：`feature/p1-v0-min` ｜ 本地执行，不推送远程

## 结果

| 产物 | 路径 | 状态 |
|---|---|---|
| NAPI 动态库（未 strip） | `build/napi/libentry.so` | **built**，15,128 B，ELF64 AArch64 |
| 放入 app 的 NAPI 库 | 由 hvigor `externalNativeOptions` 构建 | **native build**（不再手工拷 `.so`） |
| 未签名 HAP | `app/entry/build/default/outputs/default/entry-default-unsigned.hap` | **built unsigned**；大小/hash 以 `evidence/v0_build.json` 为准 |
| 自签名 HAP | `app/entry/build/default/outputs/default/entry-default-signed.hap` | **signed + verify-app 通过**；大小/hash 以 `evidence/sign_check.json` 为准；**installable=false** |
| 设备安装/运行 | — | **BLOCKED**：`hdc list targets` 为空，无板 |

## 构建命令

```bash
bash scripts/check_env.sh
bash scripts/build.sh
bash scripts/sign_hap.sh   # 自签名 + verify，不安装
```

`scripts/build.sh` 内部：

```bash
cmake -S host/napi -B build/napi -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$SDK/native/build/cmake/ohos.toolchain.cmake" \
  -DOHOS_ARCH=arm64-v8a -DOHOS_PLATFORM=OHOS -DOHOS_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/napi --target entry -j 4
```

HAP 构建额外需要（native 已接进 hvigor `externalNativeOptions`，见 `docs/v1-hvigor-native.md`）：

```bash
export DEVECO_SDK_HOME="$DEVECO/sdk"
export HOS_SDK_HOME="$DEVECO/sdk"
export NODE_HOME="/c/Program Files/nodejs"
export PATH="$NODE_HOME:$DEVECO/jbr/bin:$DEVECO/tools/hvigor/bin:$PATH"
cd app && hvigorw assembleHap --mode module -p product=default -p buildMode=debug --no-daemon
```

## 产物核验

- `libentry.so`：ELF64 AArch64，`NEEDED` = `libace_napi.z.so`, `libc++_shared.so`, `libc.so`；导入 `napi_module_register`（未定义符号，由 NAPI 运行时 `libace_napi.z.so` 解析；不是导出）。
- HAP：ZIP 内 `libs/arm64-v8a/libentry.so`（7,176 B，已 strip）和 `libs/arm64-v8a/libc++_shared.so`（1,262,504 B）存在，`module.json` 存在。
- 未签名 HAP 的 `signingConfigs` 为空；`scripts/sign_hap.sh` 用本地自签 CA 生成 signed HAP，`verify-app` 通过，但**自签 CA 不在设备信任根、无 UDID/profile，installable=false**。
- 未执行 `hdc install`，未启动应用，未验证 `ping()`/`add()` 真机行为。

## 代码接口

`app/entry/src/main/cpp/entry.cpp`（canonical；`host/napi/CMakeLists.txt` 复用该文件）：

- `ping()` → `"pong"`
- `add(a, b)` → `a + b`（double）

`app/entry/src/main/ets/pages/Index.ets`：

```ts
import entry from 'libentry.so';
this.pingText = entry.ping();
this.sumText = `${entry.add(2, 3)}`;
```

## 当前 BLOCKED

- 设备：无目标板，`hdc list targets` 为空 → 无法安装/运行。
- 签名：hvigor 无 signingConfigs → 只产出 unsigned HAP；`scripts/sign_hap.sh` 产出本地自签、可 verify 的 HAP，但因 CA 不在设备信任根/无 UDID，`installable=false`。
- `protoc`/`buf`：未安装；本切片不依赖。
- ArkTS `.d.ts` 已接通；执行 `ohpm install` 后 warning 消失，证据见 `docs/v0-dts-notes.md`。

## 下一步（本地）

1. 用官方证书/profile（或设备 UDID 的 debug profile）签可安装 HAP；`hdc install` → 启动 → 记录 `ping/add` 真机日志。
2. 确认目标 API 24 与方案基线 API 18/23 的验收口径；当前 HAP 是 API24/compileSdk 6.1.1.125。
3. 继续用 `scripts/inspect_so.sh` / `inspect_hap.py --sign-check evidence/sign_check.json` 作为证据门禁。
