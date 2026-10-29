# V0 最小切片：NAPI `.so` + ArkTS 调用 + 空 HAP

日期：2026-10-29 ｜ 分支：`feature/p1-v0-min` ｜ 本地执行，不推送远程

## 结果

| 产物 | 路径 | 状态 |
|---|---|---|
| NAPI 动态库（未 strip） | `build/napi/libentry.so` | **built**，14,696 B，ELF64 AArch64 |
| 放入 app 的 NAPI 库 | `app/entry/libs/arm64-v8a/libentry.so` | **copied** |
| 未签名 HAP | `app/entry/build/default/outputs/default/entry-default-unsigned.hap` | **built unsigned**，127,070 B |
| 设备安装/运行 | — | **BLOCKED**：`hdc list targets` 为空，无板 |

## 构建命令

```bash
bash scripts/check_env.sh
bash scripts/build.sh
```

`scripts/build.sh` 内部：

```bash
cmake -S host/napi -B build/napi -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$SDK/native/build/cmake/ohos.toolchain.cmake" \
  -DOHOS_ARCH=arm64-v8a -DOHOS_PLATFORM=OHOS -DOHOS_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/napi --target entry -j 4
```

HAP 构建额外需要：

```bash
export DEVECO_SDK_HOME="$DEVECO/sdk"
export HOS_SDK_HOME="$DEVECO/sdk"
export NODE_HOME="/c/Program Files/nodejs"
export PATH="$NODE_HOME:$DEVECO/jbr/bin:$DEVECO/tools/hvigor/bin:$PATH"
cd app && hvigorw assembleHap --mode module -p product=default -p buildMode=debug --no-daemon
```

## 产物核验

- `libentry.so`：ELF64 AArch64，`NEEDED` = `libace_napi.z.so`, `libc++_shared.so`, `libc.so`；导出 `napi_module_register`（动态未定义，由 NAPI 运行时提供）。
- HAP：ZIP 内 `libs/arm64-v8a/libentry.so` 存在（6240 B，已 strip），`module.json` 存在。
- HAP **未签名**：`build-profile.json5` 没有 `signingConfigs`，hvigor 明确 `Will skip sign`；因此不可安装到真实设备。
- 未执行 `hdc install`，未启动应用，未验证 `ping()`/`add()` 真机行为。

## 代码接口

`host/napi/entry.cpp`：

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
- 签名：无 signingConfig/profile → 只能产出 unsigned HAP。
- `protoc`/`buf`：未安装；本切片不依赖。
- ArkTS 对 `libentry.so` 的类型声明仍缺 `.d.ts`，编译有 warning，但 HAP 构建成功。

## 下一步（本地）

1. 补 `types/libentry/index.d.ts`，消除 NAPI 模块 warning。
2. 用 `llvm-readelf`/`llvm-nm` 做 `.so` 证据归档。
3. 板到后：签名/配置 profile → `hdc install` → 启动 → 记录 `ping/add` 真机日志。
