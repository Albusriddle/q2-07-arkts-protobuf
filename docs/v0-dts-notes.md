# V0 NAPI `.d.ts` 接线排查记录（ArkTS `is not verified` warning）

日期：2026-10-29 ｜ 分支：`feature/p1-v0-min` ｜ 执行：本地，无设备
环境：DevEco Studio 6.1.1.290 ｜ HarmonyOS SDK 6.1.1（API 24）｜ ohpm 6.1.2.285 ｜ Windows + git-bash

> 本文只记录**排查与验证**，不改协议、不承诺指标。HAP 仍为 unsigned、未上板。

## 0. 结论（TL;DR）

| 问题 | 结论 |
|---|---|
| warning 是否消除 | **已消除**（clean build 与增量构建都无 `is not verified`） |
| 改了什么源文件 | **没有改任何源文件**。`app/entry/oh-package.json5` 与 `types/libentry/**` 的原有接线**本来就是对的**，验证后已回滚到原始字节 |
| 真正原因 | 依赖声明了 `file:` 本地包，但**从未执行 `ohpm install`** → `app/entry/oh_modules/` 不存在 → ArkTS 解析不到 `libentry.so` 的类型 → 走"模块未找到"分支，对这一类 `.so` 只报 **warning（TS 28014）而不报 error**，所以 HAP 照样成功 |
| 这个 warning 是不是纯噪音 | **不是**。实测：`oh_modules` 缺失时，把 `index.d.ts` 故意写错类型，构建**依然成功** → 说明 .d.ts 之前**完全没被消费**，`import entry from 'libentry.so'` 退化为隐式 `any` |
| 修复动作 | 在 `app/` 下执行一次 `ohpm install`（生成 `app/entry/oh_modules/libentry.so`，含 `types: ./index.d.ts`） |
| HAP | **构建成功**，`entry-default-unsigned.hap`，约 127 KB（native 集成前历史快照；当前以 `evidence/v0_artifact_check.json` 为准），未签名 |
| 仍未解除 | **BLOCKED**：无目标板（`hdc list targets` 为空）、HAP 未签名 → 不能安装/运行。**UNVERIFIED**：`ping()`/`add()` 真机行为 |

## 1. 证据（每次实验都做了 clean 重编，`CompileArkTS` 均实际执行）

| # | 实验 | 改动 | 结果 | 日志 |
|---:|---|---|---|---|
| 0 | 基线复现 | 无（原接线，无 `oh_modules`） | warning 复现；`BUILD SUCCESSFUL`；build.sh exit 0 | `evidence/hap_build_dts.log`¹ |
| 1 | **ohpm install** | `cd app && ohpm install` | **warning 消失**；`BUILD SUCCESSFUL`；HAP ok | `evidence/hap_build_dts_ohpm.log` |
| 2 | d.ts 是否真被消费（正例） | 有 `oh_modules`，把 `add` 改成 `(a:string,b:string)=>string` | **BUILD FAILED**，3 个 error 精确指到 `Index.ets:11:33` 与 `26:39` | `evidence/hap_build_dts_probe.log` |
| 3 | d.ts 是否真被消费（反例） | 同上错误 d.ts，但移走 `oh_modules` | **BUILD SUCCESSFUL** + warning → 证明 .d.ts 未生效 | `evidence/hap_build_dts_wrongtype_nodeps.log` |
| 4 | 因果验证 | 正确 d.ts，移走 `oh_modules` | warning 复现 → 证明"缺 `oh_modules`"就是原因 | `evidence/hap_build_dts_nodeps.log` |
| 5 | 依赖 key 反例 | key/包名改 `libentry`（去掉 `.so`），安装后构建 | warning 复现 → **依赖 key 必须与 import 说明符逐字一致** | `evidence/hap_build_dts_keylibentry.log` |
| 6 | 最终状态 | 全部回滚 + `ohpm install` + clean build | **无 warning**；`BUILD SUCCESSFUL`；HAP 约 127 KB（历史快照） | `evidence/hap_build.log`、`evidence/hap_build_dts_fixed_final.log` |

¹ 10:55 的历史日志 `evidence/hap_build_dts.log`（warning 在第 27 行）；本次的等价复现是实验 4 的 `hap_build_dts_nodeps.log`（同一状态：原接线 + 无 `oh_modules`）。

## 2. 根因（编译器内部路径）

对 `.so` 模块的解析，ets-loader 里的 TS 分支有两条会导致这条 warning（diagnostic **28014**，级别 Warning）：

```
# 位置：sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript/lib/typescript.js
isSoFile = moduleReference.includes(".so")
A) if (isSoFile && !(isInETSFile && needDoArkTsLinter && !isCompatibleVersion)) { warn; return unresolved; }
B) if (moduleNotFoundError) { ... if (isSoFile) { warn } else { error } }   # 解析失败分支
```

- 分支 A 只在"非 standard ArkTS linter 模式"下命中，命中后**永远**解析不出类型。
- 本工程 `app/build-profile.json5` 的 `compatibleSdkVersion` 是 `6.1.1(24)`，hvigor 传给 ets-loader 的 `projectConfig.compatibleSdkVersion` 实测为 **24**（见 `.tsbuildinfo` 的 `"compatibleSdkVersion":24`），`executeArkTSLinter`/`standardArkTSLinter` 默认为 `true` → 运行在 **STANDARD_MODE**，分支 A 不命中。
- 因此本项目命中的是**分支 B：解析失败**。失败原因是 `oh_modules` 从未生成（`file:` 依赖没被 materialize），`libentry.so` 这个模块名找不到对应包。
- 关键设计后果：**对 `.so` 只给 warning、不给 error**，所以"构建成功"并不意味着类型检查通过——这正是这条 warning 容易被误当成噪音的原因（实验 3 已经证伪"它只是噪音"）。

## 3. 正确接线（当前仓库状态，未改源文件）

`app/entry/oh-package.json5`

```json5
"dependencies": {
  "libentry.so": "file:./src/main/ets/types/libentry"
}
```

`app/entry/src/main/ets/types/libentry/oh-package.json5`

```json5
{
  "name": "libentry.so",
  "version": "1.0.0",
  "types": "./index.d.ts"
}
```

`app/entry/src/main/ets/types/libentry/index.d.ts`：声明 `ping` / `add`（与 `app/entry/src/main/cpp/entry.cpp` 导出一致）。

要点：

1. **依赖 key 必须等于 import 说明符**。`Index.ets` 写的是 `import entry from 'libentry.so'`，所以包名必须是 `libentry.so`；改成 `libentry` 立刻复现 warning（实验 5）。
2. **必须有 `oh_modules`**。`file:` 依赖不会自动生效，必须 `ohpm install` 一次。
3. `types` 字段指向 `index.d.ts`；路径放在 `ets/types/**` 还是 DevEco 模板的 `cpp/types/**` 都行——解析走 `oh_modules`，与目录位置无关（本次用 `ets/types/**` 通过）。

## 4. 复现与日常操作

```bash
# 一次性：materialize 本地依赖（必须在 app/ 下）
cd repo/app
node "/d/Program Files/Huawei/DevEco Studio/tools/ohpm/bin/pm-cli.js" install

# 之后照常构建
cd repo
bash scripts/build.sh
```

> git-bash 下 `ohpm` 包装脚本依赖 `cygpath`（本机没有），直接 `node .../ohpm/bin/pm-cli.js install` 等价且可用。

**运维注意（重要）**：本机 ohpm 6.1.2 把本地包物化为 **symlink**（`app/entry/oh_modules/libentry.so -> ../../src/main/ets/types/libentry`），不是复制；但不同 ohpm 版本可能是复制。无论哪种，改完 `types/libentry/index.d.ts` 都建议重跑 `ohpm install`（或删 `oh_modules` 再装），避免读到旧副本。

**fresh clone**：`oh_modules/`、`build/`、`.hvigor/` 都在 `.gitignore` 里，`scripts/build.sh` 也不调用 ohpm。所以新克隆必须手工跑一次 `ohpm install`，否则 warning 会回来（这是**已知的操作前置**，不是新的技术问题；若要在 CI 里固定，需要另开任务改 `build.sh`，本次按约束未改）。

## 5. 本次写入的文件

| 文件 | 说明 |
|---|---|
| `app/entry/oh-package-lock.json5` | `ohpm install` 生成的锁文件（新增，未跟踪）。记录本地 `file:` 依赖解析结果；是否入库请评审决定（`.gitignore` 目前未忽略它） |
| `evidence/hap_build_dts_ohpm.log` / `_probe.log` / `_nodeps.log` / `_keylibentry.log` / `_wrongtype_nodeps.log` / `_fixed_final.log` | 上述实验的 hvigor 原始日志 |
| `evidence/hap_build.log`、`v0_build.json`、`env_check.json`、`check_env.log`、`cmake_*.log`、`hdc_targets.log` | 由 `scripts/build.sh` 正常重写 |
| `docs/v0-dts-notes.md` | 本文 |

**未改动**（当时快照）：`app/entry/src/main/cpp/entry.cpp`（原 `host/napi/entry.cpp`）、`Index.ets`、`scripts/build.sh`、`RISKS.md`、`app/entry/oh-package.json5`、`types/libentry/**`。

## 6. `scripts/build.sh` 的两点观察（只记录，未改）

1. **退出码不反映 HAP 失败**：实验 2 里 hvigor `BUILD FAILED`（`hap_status=failed: hvigor exit 127`），但 `build.sh` 最后仍是 `[ -n "$SO_PATH" ] && exit 0` → **exit 0**。判断 HAP 成功与否必须看 `hap_status` 或 `evidence/hap_build.log`。
2. **`status` 恒为 `PARTIAL`**：脚本里 `[ "$HAP_STATUS" = "built" ]` 永远不成立（实际值形如 `built unsigned: ...`），所以只要 `.so` 构建成功就恒 `PARTIAL`。这是脚本既有逻辑，不是本次引入。

## 7. 当前状态

| 项 | 状态 |
|---|---|
| ArkTS `libentry.so` warning | **已消除**（clean 与增量构建均无） |
| HAP 构建 | **成功**，unsigned，约 127 KB（历史快照；当前以 `v0_artifact_check.json` 为准） |
| 设备安装 / 运行 | **BLOCKED**：`hdc list targets` 为空；HAP 无 signingConfig，不可安装到真机 |
| `ping()` / `add()` 真机行为 | **UNVERIFIED** |
| 签名 / profile | **BLOCKED**（无 `signingConfigs`，hvigor 明确 `Will skip sign`） |

## 8. 变更记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0-dts-notes-1 | 2026-10-29 | 初版：6 次实验、根因、正确接线、复现步骤、build.sh 观察 |
