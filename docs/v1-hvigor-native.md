# V1 切片：把 native 构建接进 hvigor（一键 assembleHap）

> **HISTORICAL**：V1 构建侧快照；当前 V0.1 尺寸/行为以 `evidence/v0_build.json`、`evidence/v0_1_decode.json` 为准。

> 日期：2026-10-29 ｜ 状态：**PASS（构建侧）** ｜ 证据：`evidence/v1_hvigor_native.json`、`evidence/hvigor_native_build.log`
> 设备侧：**未验证**（`hdc list targets` 为空；未安装、未启动）

## 1. 改了什么

| 文件 | 变更 | 目的 |
|---|---|---|
| `app/entry/build-profile.json5` | `buildOption` 增加 `externalNativeOptions`（`path: ./src/main/cpp/CMakeLists.txt`，`abiFilters: ["arm64-v8a"]`） | 让 hvigor 自己调 CMake/Ninja，不再"旁路构建 + 手工拷 `.so`" |
| `app/entry/src/main/cpp/CMakeLists.txt` | 新增：`add_library(entry SHARED ...)` + `target_link_libraries(entry PUBLIC libace_napi.z.so)` | hvigor 的 CMake 入口 |
| `scripts/build_hap.sh` | 新增一键脚本（只调 hvigor） | 复跑入口，替代手工两条命令 |
| `app/entry/libs/arm64-v8a/libentry.so` | **删除**（手工拷贝已无必要） | 避免 HAP 里出现两份 `.so` |

> NAPI canonical 源在 `app/entry/src/main/cpp/entry.cpp`；`host/napi/CMakeLists.txt` 反向引用这份 app 源，不再保留第二份源码（原 `host/napi/entry.cpp` 已移入 app）。

## 2. 结果（E2：本机实测）

构建日志里出现了完整 native 阶段：`BuildNativeWithCmake → BuildNativeWithNinja → ProcessLibs → DoNativeStrip → CacheNativeLibs → PackageHap`，`BUILD SUCCESSFUL`。

| 产物 | 大小 | 说明 |
|---|---:|---|
| `libentry.so`（hvigor 构建，未 strip） | 37,264 B | `intermediates/cmake/default/obj/arm64-v8a/` |
| `libentry.so`（strip 后进 HAP） | 7,176 B | `intermediates/stripped_native_libs/...` |
| `entry-default-unsigned.hap` | 以 `evidence/v0_build.json` 为准 | 未签名（HAP 大小随构建变化） |
| HAP 内 `libs/arm64-v8a/libc++_shared.so` | 1,262,504 B | **新增**，见下方发现 |
| HAP 内 `libs/arm64-v8a/libentry.so` | 7,176 B | 由 hvigor 产出 |

## 3. 两个必须记录的发现

### 3.1 HAP 体积从 127 KB 涨到 1.39 MB

原因是 hvigor 的 native 构建默认按 **`c++_shared`** 打包 `libc++_shared.so`（1.26 MB）。这不影响功能，但对"库要分发给第三方应用"的场景是硬成本。**待决策（W2 前）**：

| 方案 | 收益 | 代价 |
|---|---|---|
| 保持 `c++_shared` | 多模块共享 STL，体积不重复 | 每个 HAP 都背 1.26 MB |
| 改 `c++_static` | HAP 变小（只剩自己的代码） | 多个 native 库时 STL 代码重复、跨库传递 `std::string` 有 ABI 风险 |
| `c++_shared` + 由 HAR/HSP 统一提供 STL | 折中 | 分发形态更复杂（要验证 HAR 承载 native） |

> 这条同时解释了为什么"交付形态"必须先验证 HAR（报告 §4.7 已标 W1 必须验证）。

### 3.2 未签名（与 v0 相同）

`signingConfigs` 仍为空，hvigor 打日志 `Will skip sign`。**HAP 不可安装到真实设备**；要安装必须先配调试证书/profile（绑定 UDID）。这不是代码问题，是证书问题。

## 4. 这证明了什么 / 没证明什么

| 证明了 | 没证明 |
|---|---|
| `externalNativeOptions` 在本机 DevEco/SDK API 24 下可用 | 设备能安装、能启动 |
| hvigor 能完整跑 CMake + Ninja + strip + 打包 | `ping()/add()` 在真机返回正确值 |
| 不再需要手工拷 `.so`（可复跑：`bash scripts/build_hap.sh`） | 任何性能、内存、启动数据 |

## 5. 下一步（与报告 §8.3 对齐）

1. 配调试证书/profile（要设备 UDID）→ `hdc install` → 启动 → 记录 `ping/add`（V0 设备侧）；
2. 决定 `c++_shared` 还是 `c++_static`（影响 HAP 体积与 ABI，建议在 HAR 验证前定）；
3. 接 `src/host` 的 decoder 后，把 `libentry.so` 的 `ping/add` 换成真实 decode 入口。
