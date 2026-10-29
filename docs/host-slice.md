# Host 垂直切片 v0：wire reader + tape + materializer + 差分/基准（HOST-ONLY）

日期：2026-10-29 ｜ 分支：`feature/p1-v0-min` ｜ 已由 `d70f912` 提交（HOST-ONLY 独立纵切，不属于 V0 设备证据）
范围：`repo/src/host/**`、`repo/scripts/build_host.sh`、`repo/bench/**`、`repo/evidence/host/**`

> **本文件所有数字都是 HOST-ONLY**：在开发 PC 上用原生 host 编译器测得，**不是设备数据**，
> **不构成 1.5× 或 700 ms 的任何主张**。无开发板参与（`hdc list targets` 仍为空）。
> 机器可读证据：`evidence/host_slice.json`（`"scope": "HOST-ONLY"`，
> `"not_claimed": ["device performance","1.5x","700ms"]`）。

## 一行结论

在 host 上把 `.proto → 解码(POD) → tape → materializer` 这条链路**跑通并判否**了：
38/38 条 W0 golden wire 向量、425 条断言、25/25 对象图差分、22/22 字节回归、
8,178 条消息的 tape→对象重建与 C++ 对象图**逐字节一致**；单条 100 MB 类数据卡的
wire 解码约 0.57–0.65 GB/s（host，参考实现）。**设备侧仍未测。**

## 交付物

| 产物 | 路径 | 状态 |
|---|---|---|
| 一键构建+测试+基准+差分+证据 | `scripts/build_host.sh` | **exit 0** |
| C++ wire reader / 解码 / tape / 基准 | `src/host/decoder`、`src/host/tape`、`src/host/bench` | **built** |
| TS 参考 materializer | `src/host/materializer/materializer.ts` | **runs under node 24 type-stripping** |
| 单元测试（含 38 向量） | `src/host/tests` | **425 passed / 0 failed** |
| 差分 harness（protobufjs 7.6.5） | `src/host/ts/*.mjs` | **25 cases PASS** |
| 基准 CSV（含卡大小+sha256） | `evidence/host/host_bench.csv` | **30 行数据 + 表头** |
| 差分报告 | `evidence/host/differential.json` | **ok: true** |
| materializer 报告 | `evidence/host/materializer_check.json` | **ok: true** |
| 证据总账 | `evidence/host_slice.json` | **29 个产物 sha256 + 每步命令/退出码/耗时** |
| 数据卡 | `bench/data/*.bin`（生成物，gitignore） | **S1 8.4 MB / S3 100 MiB payload** |

## 证明了什么

1. **校验顺序是可执行且可判否的**：`tag → wire type → len ≤ remaining → len ≤ cap → 分配`
   在 `decoder/wire_reader.cpp` 里是唯一顺序；对 `12 ff ff ff ff 0f 00`（4 GiB 长度前缀）
   这类输入，**读数器自身计数为 0、进程级 `operator new` 计数增量 0 字节**（测试
   `wire/allocation-order` 断言，输出见 `build/host/logs/tests.log`）。
2. **38/38 条 W0 golden wire 向量按原期望通过**（`E_OK`/`E_TRUNCATED`/`E_VARINT_10TH_BYTE`/
   `E_LENGTH_EXCEEDS_CAP`/`E_MAP_KEY_DANGEROUS`/`E_DEPTH`/… 名字与原文件逐字一致）。
   R1/D17 要求的三条额外向量也在回归集里：非最短 varint `80 00` 必须**接受**；
   负数 int32（10 字节）→ −1；`ff×9+01`（2^64−1，判 OK）与 `ff×9+02`（判
   `E_VARINT_10TH_BYTE`）成对存在。
3. **混合 schema 解得出**：`int64`、`string`、`repeated string`、嵌套 `Profile`、
   `oneof(keep-all + 判别位)`、`proto3 optional` 三态（缺失 / 显式 0 / −1）、
   `map<string,int32>`（重复 key last-wins）、未知字段跳过、packed/unpacked、zigzag。
4. **tape v0 布局可用**：80 B 定长 user 记录 + 16 B profile 记录 + roots(字节偏移) +
   blob + values，crc32 校验；**同输入两次写入字节完全相同**；verifier 对 13 类篡改
   （magic/version/record size/crc/未对齐 root/越界 root/presence 位/oneof 位/blob 越界/
   values 越界/profile 未对齐/记录数撒谎/段未对齐）全部拒绝。
5. **tape → 普通对象可重建**：`materializer.ts` 从 9,463,448 B tape 重建 8,178 个对象，
   与 C++ norm dump **JSON 文本级完全一致**（sha256 `0640ce08…` 相同，0 处差异）；耗时/吞吐以 `evidence/host_slice.json` 的 `materializer.materialize_ms` / `users_per_s` 为准（HOST-ONLY）。
6. **与 protobufjs 7.6.5 无未登记差异**：25 例语料上对象图 25/25 相等；
   按"wire 上真实出现过哪些字段"（presence 位）重建对象后重新 encode，22/22 **字节完全一致**；
   仅 3 例有**已登记**差异（重复 map key 折叠、非最短 varint 规范化、未知字段按设计丢弃）。
7. **host 基准链路可复跑**：S1 类（8,178 条、~1,026 B/条、191,858 字段）与
   S3 类（100 MiB payload、100 帧、102,100 条、2,393,694 字段）都产出 CSV 行，
   每行带 `card_bytes` + `card_sha256` + 编译器 + 主机 OS。

## 关键数字（HOST-ONLY，非设备）

| 数据卡 | 大小 / sha256(前 8) | 条数 | wire 解码 | tape 写入 |
|---|---|---|---|---|
| `s1_user_1kb`（S1 类：10–30 字段、≈1 KiB/条，单条 UserBatch） | 8,397,700 B / `1bebc88a` | 8,178 | 570.6 MB/s，555,650 msg/s，14.7 ms/卡 | 94.2 ms/卡 ≈ 9,713 ns/条（仅 tape 段）；tape 9,463,448 B = 1,157 B/条 |
| `s3_100mb`（S3 类：100 MiB payload，100 帧 × ≈1 MiB） | 105,005,840 B / `cbc9efa9` | 102,100 | 646.4 MB/s，628,555 msg/s，162.4 ms/卡 | 1,123 ms/卡 ≈ 9,410 ns/条（仅 tape 段）；tape 118,217,296 B = 1,158 B/条 |

- 量纲：MB = 10^6 B；`reps=5` 取中位数；tape 行含"解码+写 tape"，另给
  `tape_write_only_*`（= tape 段 − 解码段）作派生量。
- **tape 写入是未优化参考实现**（逐字段 `std::string` 拷贝到增长中的 blob，无 arena），
  这些数字是**上界**，不是设计目标，也不能外推到设备。
- 数值随机器负载波动（同一台机多次运行 ±10% 内），以 `evidence/host/host_bench.csv` 为准。
- 未测：NAPI 单次成本 `c_call`、对象构造 `c_con`、RSS/GC、真机 CPU。**不可**据此推 1.5×/700 ms。

## 没证明什么（明确边界）

- **没有设备数据**：无板、无 ArkTS 运行时、无 NAPI、无真机 GC/CPU；本切片不碰
  `repo/app/**` 与 `repo/host/napi/**`。
- **没有 ASan/UBSan**："先校验后分配"是用**插桩计数器 + 覆写全局 `operator new`** 断言的，
  不是 sanitizer 证明；C++ ASan 设备版仍 `BLOCKED`。
- **没有 codegen**：`decoder/decode_user.cpp` 是手写的"生成器形状"代码；`.proto → IR → 模板`
  这一段未做。
- **没有 P3/P4 完整路径**：未做 `decodeMany` 的 NAPI 句柄 scope、external ArrayBuffer、
  tape 分块 arena、worker 线程模型。
- **oneof 只做 keep-all + 判别位**（对齐 7.6.5）；spec/last-wins 臂与 protobufjs 8.x 语义未实现。
- **未知字段不保留**（对齐 proto3 默认丢弃）；`discardUnknown=false` / `$unknowns` 未做。
- **map 迭代顺序、int64 用 bigint、TypedArray 快路径**均未实现。
- **S3 卡是合成数据卡**，不是真实业务 trace；"100 MB 类"只对齐量级。
- **未跑 fuzz**：边界表 17 条 + 38 向量 + 25 例差分是回归下限，不是 fuzz 覆盖。

## 已知差异 / 需要在验收侧签字的部分

| # | 差异 | 方向 | 处理 |
|---|---|---|---|
| D-a | map 重复 key 折叠为一条（last-wins） | 与 protobufjs 7.6.5 相同 | 差分已登记 |
| D-b | 非最短 varint 接受但**不重编码**为最短形式 | 与 protobuf 实现一致 | 差分已登记 |
| D-c | 未知字段丢弃 | 与 proto3 默认/7.6.5 相同，与 8.x `$unknowns` 不同 | 差分已登记 |
| D-d | `__proto__`/`constructor` 等 map key 在 strict 模式**拒绝** | 安全策略，protobufjs 会写入原型链 | 需验收侧确认（安全优先） |
| D-e | 非法 UTF-8 在 strict 模式报 `E_UTF8_INVALID`，compat 模式保留原始字节 | 与 Node 的 U+FFFD 替换不同 | 需按 `protobufjs_profile` 定 |
| D-f | `int64` 走 decimal string（差分用 `longs:String`），native 侧应为 `Long`/`bigint` | 未定 | V2 再定 |
| D-g | tape 只服务本切片的 `User/UserBatch`，`RECORD_SIZE=80` 写死 | 需 codegen 算 | 后续 |

## 下一步接什么

1. **P3 批量（先）**：把 `decode_user.cpp` 的 POD 换成"直接造 ArkTS 对象"的 NAPI 版本，
   加每 1k–4k 对象一个 `napi_handle_scope` + `napi_escape_handle`，先在 host 上量 `c_call`/`c_con`
   的**量级**（仍标 host-only），再上板。
2. **codegen 骨架**：`.proto → IR 字段表 → C++ 生成物 + tape 常量`，把
   `pod.h` 里手写的字段号/presence 位换成生成物（§5.6）。
3. **tape 优化**：blob arena + 预分配 values（当前 ~10 µs/条是未优化上界），
   再接 §5.5.1 的 P4-vtable/P4-row 两档 A/B。
4. **设备门**：板到后按 V0/V1/V3/V4 跑；本切片的所有 host 数字只作为回归基线，
   **不得**折进设备加速比。
5. **安全**：C++ ASan/UBSan（host 可先跑）→ 再设备；补 fuzz（§6.3.4 三 oracle）。

## 如何复跑

```bash
bash repo/scripts/build_host.sh          # 全流程，约 30 s；失败非 0 退出
bash repo/scripts/build_host.sh --no-npm # 跳过 protobufjs 差分（离线场景）
```

无 npm 时脚本会**降级为 Python 自写编码器 + golden**：
`src/host/tools/py_fallback_diff.py`（14 例，独立 Python 编码器 + 期望对象图），
报告里 `"baseline": "python-encoder-fallback (protobufjs unavailable: DEGRADED)"` 并写明降级原因。
本次实际运行用的是 **protobufjs 7.6.5**（`diff mode : protobufjs-7.6.5`）。

## 工具链偏差（必须记录）

要求的 host 编译器是 `g++`/`clang++`。本机实际情况：

- **没有 MinGW/g++**；
- DevEco 自带 clang 15.0.4 是 `x86_64-w64-windows-gnu` 目标但**没有 Windows 头文件/库**，
  且 MSVC STL 14.44 明确拒绝 clang < 19（`STL1000`）；
- 因此本切片用 **MSVC 19.44.35229（cl，x64，VS 2022 Build Tools）**：仍是**原生 host 编译器**，
  **不是** OpenHarmony NDK 交叉编译（那才是硬约束禁止的对象）。

`scripts/build_host.sh` 的探测顺序是 `$CXX → g++ → clang++ → MSVC`，
所以在有 g++/clang++ 的机器上会自动走 g++/clang++；本次证据里记为
`"toolchain": {"kind": "msvc", "compiler": "cl", "compiler_version": "MSVC cl 19.44.35229"}`。
