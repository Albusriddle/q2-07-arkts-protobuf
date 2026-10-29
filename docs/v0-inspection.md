# V0 产物只读核验：NAPI `.so` 与未签名 HAP

日期：2026-10-29 ｜ 分支：`feature/p1-v0-min` ｜ 本地执行，不推送远程
范围：**只读检查**。不重建 HAP、不 re-sign、不 strip、不改 NAPI/ArkTS/构建脚本。

配套脚本：

| 脚本 | 检查对象 | 产物 |
|---|---|---|
| `scripts/inspect_so.sh` | `build/napi/libentry.so` 等任意 ELF | stdout（人类可读）+ 退出码 |
| `scripts/inspect_hap.py` | HAP（ZIP 容器） | `evidence/v0_artifact_check.json` + 退出码 |
| `evidence/v0_so_check.log` | 上述 `.so` 脚本的原始输出记录 | 三组运行的 verbatim 输出 + runner 追加的退出码 |

---

## 0. 一句话结论

**两个产物都通过结构检查**（`inspect_so.sh` exit 0，`inspect_hap.py` exit 0），**但都还不能上真实设备**：HAP 未签名，且没有目标板；`libc++_shared.so` / `libace_napi.z.so` 依赖系统镜像提供，未在设备侧验证。

**本次检查新发现 3 个需要 V0 owner 决策的风险**（见 §4）：`libc++_shared.so` 未打包、SDK/HAP 是 **API 24** 而非方案基线的 API 18/23、HAP 未签名。

---

## 1. 脚本用法

### 1.1 `scripts/inspect_so.sh`

```bash
bash scripts/inspect_so.sh                      # 默认检查 build/napi/libentry.so
bash scripts/inspect_so.sh path/to/libfoo.so    # 检查任意 .so
bash scripts/inspect_so.sh --strict             # 把 WARN 升级为 FAIL
```

工具来源：DevEco SDK 的 LLVM binutils（`$SDK/native/llvm/bin/llvm-readelf{,.exe}`、`llvm-nm`），本次实测版本 **LLVM 15.0.4**。可用 `DEVECO_HOME` 或 `OHOS_SDK_ROOT` 覆盖 SDK 路径。

**退出码（任何非零都表示"不可接受"）**

| 码 | 含义 |
|---:|---|
| `0` | PASS：已检查，全部必检项通过 |
| `1` | FAIL：已检查，至少一个必检项失败 |
| `2` | ERROR：无法检查（文件不存在、或 SDK LLVM 工具缺失） |

**检查项**

| 段 | 内容 | 必检（FAIL） | 提示（WARN） |
|---|---|---|---|
| `[1]` ELF identity | magic `7f454c46`、`ELF64`、`AArch64`、`DYN`、小端 | 全部 4 项 | 非小端 |
| `[2]` NEEDED / SONAME | `DT_NEEDED` 列表、`SONAME`、`TEXTREL`、`BIND_NOW` | `libace_napi.z.so` 必须在 NEEDED 中 | 无 SONAME、有 TEXTREL、NEEDED 超出已知 OHOS 系统集 |
| `[3]` `napi_module_register` | 在动态符号表中的存在性与类型 | 必须存在 | 若为**定义**而非**导入** |
| `[4]` 导出符号 | 动态已定义符号 | 不能为空（至少 `_init`/`_fini`） | — |
| `[5]` 未定义导入 | 动态未定义符号；`napi_*` 能否被 NEEDED 解析 | `napi_*` 存在但 NEEDED 无 `libace_napi.z.so` | 无任何导入；C++ 运行时导入但 NEEDED 无 `libc++_shared.so` |
| `[6]` 本地回调 | 非动态定义的 `napi_env*` 函数（信息性） | — | — |

**两个容易判错的地方（本脚本按正确语义实现）**

1. **`napi_module_register` 应当是"未定义导入（U）"，不是"导出（T）"。** 它是 NAPI 运行时（`libace_napi.z.so`）导出的入口，模块的构造函数调用它来注册自己。因此"导出符号里找不到 `napi_module_register`"**不是**缺陷。`out/w0` 早期文档把它写成"导出 `napi_module_register`（动态未定义）"是自相矛盾的表述；`docs/v0-slice.md` 第 43 行沿用了这个说法——**以本文件与脚本输出为准**。
2. **导出符号只有 `_init`/`_fini` 是正常的。** NAPI 模块由它注册的模块名定位，不靠导出 C 符号。脚本因此只把"导出符号数为 0"当失败，不把"没有自定义导出"当失败。

### 1.2 `scripts/inspect_hap.py`

```bash
python scripts/inspect_hap.py
python scripts/inspect_hap.py --hap path/to/entry-default-unsigned.hap
python scripts/inspect_hap.py --out evidence/v0_artifact_check.json
python scripts/inspect_hap.py --wait 90     # HAP 正在被并发构建重写时，先等它出现
```

纯标准库（`zipfile` + `struct`），不依赖 SDK；ELF 头与 `DT_NEEDED` 由脚本内一个最小 ELF64 读取器解析（已与 `llvm-readelf -d` 逐项比对一致）。

| 码 | 含义 |
|---:|---|
| `0` | PASS |
| `1` | FAIL：容器/清单/库文件有必检项不通过 |
| `2` | ERROR：HAP 文件不存在或无法打开 |

> **并发构建的坑（本次实际撞到）**：`hvigorw` 重建 HAP 时会先删后写，中间有短暂窗口文件不存在，此时脚本按设计返回 **exit 2**（`HAP not found`）。这不是缺陷，是"无法检查"的正确表达。要等构建完成再检查，用 `--wait <秒>`；不要把它当成"检查通过"。

**检查项**：ZIP 有效性（含 CRC `testzip()`）→ 条目清单 → `module.json` 存在/JSON 合法性/必需键 → `libs/arm64-v8a/libentry.so` 存在/大小/SHA-256/ELF 身份/`DT_NEEDED` → 签名块扫描 → 按 ABI 汇总打包的原生库 → 与仓库内副本交叉核对 → HAP 与 SDK 的 API 对齐。

**`signed` 的判定规则（不伪造）**：只根据 ZIP 中央目录里的条目名判定——命中 `META-INF/*`、`*.SF`、`*.RSA`、`*.EC`、`*.DSA`、`*.P7B` 或名称含 `signature` 才记 `true`；否则记 `false`，并在 `signature.evidence` 里写明"查过哪些模式、结果为空"。**不做密码学验签**（那是 `hap-sign-tool verify-app` 的事，本次未做）；若有签名条目，脚本会把它列入 `unverified` 而不是当作已验证。

**JSON 顶层键**：`schema` / `generated_utc` / `generator` / `repo` / `mode` / `input` / `container` / `module_manifest` / `native_lib` / `signature` / `cross_checks` / `api_alignment` / `status` / `failures` / `warnings` / `unverified` / `blocked`。

---

## 2. 当前实测结果

### 2.1 NAPI `.so`（`build/napi/libentry.so`）

命令 `bash scripts/inspect_so.sh` → **exit 0（PASS，passed=8 failed=0 warnings=0）**

| 项 | 实测值 |
|---|---|
| 大小 | `14,696` B |
| SHA-256 | `9879c20a69282be91181cb152744e14f2532c4f143ccd7f99d5aed6f1b86f48e` |
| 类型 | `ELF64` / `AArch64` / `DYN (Shared object file)` / 小端 |
| `SONAME` | `libentry.so` |
| `NEEDED` | `libace_napi.z.so`、`libc++_shared.so`、`libc.so` |
| 导出（动态已定义） | 仅 `_init`、`_fini` —— 正常 |
| 未定义导入（动态） | 12 个，其中 `napi_*` 6 个：`napi_create_double`、`napi_create_string_utf8`、`napi_define_properties`、`napi_get_cb_info`、`napi_get_value_double`、`napi_module_register` |
| `napi_module_register` | **未定义导入（U）**，由 `libace_napi.z.so` 解析 —— 正确形态 |
| 本地回调（已 demangle） | `(anonymous namespace)::Init`、`::Ping`、`::Add` |
| 加固 | `FLAGS BIND_NOW` / `FLAGS_1 NOW`（急切绑定）；无 `TEXTREL`；导入 `__stack_chk_fail`/`__stack_chk_guard`（栈保护已开） |

### 2.2 HAP（`app/entry/build/default/outputs/default/entry-default-unsigned.hap`）

命令 `python scripts/inspect_hap.py` → **exit 0（PASS，failures=0 warnings=2 unverified=6）**

| 项 | 类别 | 实测值 |
|---|---|---|
| 大小 / 外层 SHA-256 | **不稳定** | 产物正被并行任务反复重建，外层 SHA 每次变化。**以 `evidence/v0_artifact_check.json → input.size / input.sha256` 为准**（写作期间观测到 127,070 / 127,135 / 127,137 B 三种外层大小，见 §2.3 快照表） |
| 容器 | 稳定 | 有效 ZIP，**14 个条目**，CRC 校验 ok |
| `module.json` | **稳定** | 1587 B，`sha256 db7e9bd2a1ce87c6d544822d07eb71de417627764411062397b1942282bb2868`，JSON 合法，**必需键 0 缺失** |
| `libs/arm64-v8a/libentry.so` | **稳定** | 6,240 B，`sha256 fb8483d7b6d3fb4b4bcc9f55c9b9b39da1b696f0fc03d74e02601f9fb1270cd4`，`ELF64`/`AArch64`/`DYN` |
| `ets/modules.abc` | 稳定 | 12,596 B |
| `pkgSdkInfo.json` | 不稳定 | 64 → 129 B（随依赖登记变化） |
| bundleName / 版本 | 稳定 | `com.example.q207` / `1.0.0`（code `1000000`） |
| module | 稳定 | `name=entry`，`type=entry`，`mainElement=EntryAbility`，`abilities=["EntryAbility"]` |
| deviceTypes | 稳定 | `phone`、`tablet`、`2in1`、`wearable` |
| buildMode | 稳定 | `debug` |
| compileSdk | 稳定 | `6.1.1.125`，`compileSdkType=HarmonyOS`，`virtualMachine=ark24.0.0.0` |
| min/target API（原始值） | 稳定 | `60101024` / `60101024` |
| 打包的原生库 | 稳定 | 仅 `libs/arm64-v8a/libentry.so` 一个；无其他 ABI |
| **签名** | 稳定 | **`signed=false`** —— 无 `META-INF/`，无 `*.SF`/`*.RSA`/`*.EC`/`*.DSA`/`*.P7B` |

### 2.3 交叉核对

| 核对 | 结果 |
|---|---|
| `build/napi/libentry.so` vs `app/entry/libs/arm64-v8a/libentry.so` | **完全相同**（14,696 B，SHA-256 均为 `9879c20a…86f48e`） |
| HAP 内 `.so` vs 仓库副本 | **不同**：6,240 B vs 14,696 B（0.425×）→ 打包时被 strip |
| strip 是否改变了动态段 | **没有**：两份 `.so` 的 `NEEDED`（3 项）与动态符号（导出 2 / 导入 12）**逐项一致**，仅本地符号被移除 |
| `module.json` 的 API 与 SDK | SDK `apiVersion=24`（`version=6.1.1.125`）；HAP `compileSdkVersion=6.1.1.125` → 同代 |

> **关于产物 SHA 变化（重要）**：本次检查期间 HAP 被**并行任务反复重建**，外层 SHA-256 每次都变。但载荷是稳的。下表是**写作期间观测到的快照（不保证等于当前产物）**：

| 快照（本地） | 外层大小 | 外层 SHA-256 | `module.json` | 内层 `libentry.so` | 变的是什么 |
|---|---:|---|---|---|---|
| 12:56 | 127,070 | `42f6f412…913617` | `db7e9bd2…bb2868` | `fb8483d7…270cd4` | — |
| 13:01 | 127,070 | `2d3546aa…3ca814` | 同上 | 同上 | 仅 ZIP 时间戳 |
| 13:03 | 127,135 | `9aaa4a6e…9771a4` | 同上 | 同上 | 仅 `pkgSdkInfo.json` 64→129 B |
| 13:03 | 127,137 | `a0139183…12c57f` | 同上 | 同上 | 仅 `pkgSdkInfo.json` 内容（新增 `libentry` 依赖登记） |

**结论**：`module.json`、`libs/arm64-v8a/libentry.so`、`ets/modules.abc` 在四次重建中**逐字节不变**；只有 ZIP 元数据和 `pkgSdkInfo.json` 变化。写作末期又观测到 127,135 B（外层 SHA `bfb117d9…`）再次出现 —— 说明并发重建期间外层产物会来回摆动，只有载荷哈希是可靠的锚点。

**因此证据要锁"载荷哈希"，不能只锁外层 HAP SHA-256。** 本文与 `evidence/v0_artifact_check.json` 同时记录外层（`input.sha256`）与载荷（`module_manifest.sha256`、`native_lib.sha256`）；对外引用时**以载荷为准**。需要刷新到当前产物时，重跑 `python scripts/inspect_hap.py` 即可。

---

## 3. 退出码实测

| # | 命令 | 退出码 | 说明 |
|---:|---|---:|---|
| 1 | `bash scripts/inspect_so.sh` | **0** | `RESULT: PASS (passed=8 failed=0 warnings=0)` |
| 2 | `bash scripts/inspect_so.sh <tmp>/notelf.so`（纯文本文件） | **1** | 8 项必检失败（含 magic / class / machine / type / NEEDED / `napi_module_register` / 导出符号为空）→ 证明非零退出确实触发 |
| 3 | `bash scripts/inspect_so.sh does/not/exist.so` | **2** | `ERROR (cannot inspect)` |
| 4 | `python scripts/inspect_hap.py` | **0** | `status=PASS failures=0 warnings=2 unverified=6` |

运行 1–3 的完整输出见 `evidence/v0_so_check.log`（脚本 stdout 逐字保留，退出码由 runner 追加，且已标注为 runner 追加）。

---

## 4. 检查发现的三个风险（交给 V0 owner）

### R1｜`libc++_shared.so` 是 NEEDED，但没有打包进 HAP

- 事实：`.so` 的 `DT_NEEDED` 含 `libc++_shared.so`（因为 `OHOS_STL=c++_shared`）；HAP 的 `libs/` 下**只有** `libentry.so`。
- 含义：加载 `.so` 时，`libc++_shared.so` 只能由**系统镜像**提供。若目标镜像没有该库，安装成功但 `dlopen` 失败，表现为 `import libentry.so` 直接报错。
- 状态：**未在设备验证**。脚本已把它记为 `needed_resolution.libc++_shared.so = "system_provided_assumed"` 并写入 `unverified`，同时进 `warnings`。
- 处置（板到位后二选一）：① 证伪/证实系统是否提供，实测 `import`；② 改 `OHOS_STL=c++_static` 或把 `libc++_shared.so` 一并拷入 `libs/arm64-v8a/`。**本次不改构建脚本。**

### R2｜产物是 API 24，与方案基线的 API 18/23 不一致

- 事实：SDK `apiVersion=24`（`6.1.1.125`）；HAP `compileSdkVersion=6.1.1.125`；`minAPIVersion`/`targetAPIVersion` 原始值均为 `60101024`。
- 解码：`60101024` 按 `<major><minor:02><patch:02><build:03>` 拆为 **6.1.1 / build 24**，与 `compileSdkVersion=6.1.1.125` 自洽。**该解码是 `INFERRED`，不是 DOC_VERIFIED**（`evidence/v0_artifact_check.json → api_alignment.hap_min_api.confidence`）。
- 含义：无论解码细节如何，**`minAPIVersion` 的数值远大于 23**，因此不能假定这个 HAP 能装到 API 18 或 API 23 的设备上。方案里"API 18 + NAPI-only 保守备选"与"API 23 混合"两条路，当前构建**都不覆盖**。
- 状态：需要 V0 owner 书面确认目标设备代际；脚本已作为 `warnings` 写出。

### R3｜HAP 未签名

- 事实：`signed=false`，无任何签名条目；构建时 `hvigor` 明确 `Will skip sign`（见 `evidence/hap_build.log`）。
- 含义：真实设备会拒绝安装。**在拿到签名证书/profile 之前，"能不能装"这个问题无法回答**。
- 状态：`blocked`。本次不生成签名、不做假签名。

---

## 5. UNVERIFIED 清单

> 这些**不是通过**，是**本次检查手段无法确认**。逐条对应 `evidence/v0_artifact_check.json → unverified`。

| # | UNVERIFIED | 为什么无法确认 | 何时能确认 |
|---:|---|---|---|
| U1 | 设备安装 / 启动 / `ping()` / `add()` 行为 | 无目标板（`evidence/env_check.json` 记录 `board.status="no target"`），且本脚本不调用 `hdc` | 板 + 签名后 |
| U2 | `libace_napi.z.so`、`libc++_shared.so`、`libc.so` 是否由目标镜像提供 | 纯静态检查无法回答；只能在设备上 `dlopen` 实测 | 板到位 |
| U3 | 签名证书链 / profile 有效期 / 权限 | 当前无签名；脚本也不做密码学验签 | 拿到签名材料后用 `hap-sign-tool verify-app` |
| U4 | `minAPIVersion`/`targetAPIVersion` 的语义 | `60101024` 的解码是 `INFERRED`（与 `5.0.0.12→50000012` 的已知编码类比所得），无官方映射文档入库 | 官方文档或发榜方确认 |
| U5 | ArkTS 侧 `import libentry.so` 的类型检查 | 打包物里有 `ets/types/libentry/oh-package.json5`，但 `.d.ts` 是否被 ArkTS 类型系统接受、是否清掉了构建 warning，本脚本不解析 `.abc` | 复跑 `hvigorw assembleHap` 看 warning，或 IDE 检查 |
| U6 | HAP 内被 strip 的 `.so` 的回调函数名 | strip 移除了本地符号；只能确认动态段与仓库副本一致，不能从 HAP 内重新读出 `Init/Ping/Add` | 需要未 strip 副本（已有）或反汇编 |
| U7 | 除 `arm64-v8a` 以外的 ABI | HAP 只打了 `arm64-v8a`；`x86_64` 模拟器等路径未构建、未检查 | 若需要模拟器再构建 |

**BLOCKED 清单**（来自 `evidence/v0_artifact_check.json → blocked`）

- 无目标设备：`hdc list targets` 为空。
- HAP 未签名 → 真实设备会拒绝安装。

---

## 6. 复跑方法

```bash
# 0) 环境（已有证据，勿重复采购/安装）
bash scripts/check_env.sh

# 1) .so 结构核验（exit 0 = PASS）
bash scripts/inspect_so.sh
echo $?

# 2) HAP 结构核验 + 写 JSON 证据（--wait 防止撞上并发构建）
python scripts/inspect_hap.py --wait 90
echo $?

# 3) 自证退出码确实会失败（负向对照，不需要板）
bash scripts/inspect_so.sh /tmp/not-an-elf.so ; echo $?   # 期望 1
bash scripts/inspect_so.sh does/not/exist.so ; echo $?    # 期望 2

# 4) 留档 .so 输出
bash scripts/inspect_so.sh > evidence/v0_so_check.log 2>&1
```

两个脚本都是只读的：`inspect_so.sh` 只调用 `llvm-readelf`/`llvm-nm`；`inspect_hap.py` 只用 `zipfile` 读中央目录 + `struct` 读内存里的 `.so` 字节，**不写回 HAP、不解包到仓库目录**。

---

## 7. 本次明确没做

1. 没有重建 HAP（并行任务的 13:01 重建不是本次触发；本次只读）。
2. 没有 `strip` / 没有改 `CMakeLists.txt`、`build-profile.json5`、`entry/build-profile.json5` 或任何 NAPI/ArkTS 源码。
3. 没有签名、没有伪造 `signed=true`。
4. 没有 `hdc install`、没有启动应用。
5. 没有做密码学验签。
6. 没有把 host 侧结论当作设备结论。

---

## 8. 与上游文档的关系

| 文档 | 关系 |
|---|---|
| `docs/v0-slice.md` | 该文的产物清单与本次实测一致（14,696 B / 127,070 B / 6,240 B 内层）；但第 43 行"导出 `napi_module_register`（动态未定义）"表述矛盾，**以本文件 §1.1 的澄清为准**；该文未记录 SHA-256，本文件补齐 |
| `evidence/v0_build.json`、`evidence/env_check.json` | 本文件消费其 `board.status` 与 `api_version`，不覆盖 |
| `evidence/v0_artifact_check.json` | 本文件的机器可读版本；两者数字必须一致，改动以重跑脚本为准 |
