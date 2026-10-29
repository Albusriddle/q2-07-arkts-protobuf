# Q2-07 W0 执行总结：已完成什么、当前判什么、下一步补什么

> 执行日期：2026-10-29 ｜ 执行方式：8 名团队成员按角色并行；Lead 汇总
> 基线：主报告 v2.2、R6 第二轮复核报告、`out/Q2-07_W0决策阶段_智能体任务分派.md`
> 状态：**W0 静态阶段已执行；设备侧/外部书面材料原标 BLOCKED。2026-10-29 按用户口径重分类为 RESOURCE-GATED（资源可得，不是 no-go）；红队原判 A 暂停作为最终结论。新可行性分析见 `out/Q2-07_可行性重估_资源可得版.md`（B-，有条件可投）。**

## 0. 一句话结论

**W0 能做的静态工作已经做完：50+ 份证据/模板/实验协议落盘，38/38 wire 向量、stats 自检、MANIFEST 14/14 等可复跑检查通过。但 V0/V1/V2/V3/V4/V5/V6/法律/产品共 9 个 gate 仍 BLOCKED，唯一 PARTIAL 是架构设计。红队结论：当前判 A（暂不投标）；2026-11-22 前若 V0、V2、V3、V5 闭环，复评转 B（内部工具），否则 A 定案。**

## 1. 执行结果总表

| Gate | 判定 | 已完成 | 关键缺件 |
|---|---|---|---|
| V0 平台 | **BLOCKED** | 代际三选一、31 项 API 12–24 矩阵、AOT/签名/构建 gate、ANI 15–25 人日拆分 | 目标板、系统镜像、`hdc`/`hvigorw`/DevEco/SDK/NDK、证书/profile |
| V1 H/k | **BLOCKED** | 预注册、H/k 判定、数据卡、replay kit | 目标板、Android 对照机、同一 C++ decoder、100 MB 数据 |
| V2 ArkTS 接口面 | **BLOCKED** | owner/protocol/results 模板、3 段业务代码审计表、API 级 vs 值语义级矩阵 | 真实业务代码、ArkTS 工程、@Observed/JSON 回归 |
| V3 构造地板 | **BLOCKED** | A/B 双臂协议、95% 上界 0.45/0.50 判定、GC/RSS/shape 指标 | tape/materializer 最小实现、目标板、真实数据 |
| V4 NAPI/P3 | **BLOCKED** | 空调用/new_instance/set_property/external buffer/P3-batch/P4 三臂协议、break-even N | NAPI 最小工程、目标板、`napi_new_instance` 实测 |
| V5 Schema | **BLOCKED** | 7.6.5 vs 8.x 版本条件表、WKT/optional 矩阵、buf 清单、热点模板、Node probe 脚本/输出 | 现网 `protobufjs/package.json`、pbjs 版本、真实 `.proto`/调用代码、`buf`/`protoc` |
| V6 安全 | **BLOCKED** | 威胁模型、wire 校验、NAPI 生命周期、external buffer 规则、fuzz/sanitizer 矩阵、SBOM 模板、38/38 host 向量 | 设备侧 ASan/压力测试、NDK/`.so`、osv-scanner/syft/scancode |
| W0-L 法律 | **BLOCKED** | 主体三选一、IP/CLA 路径、26 项许可证矩阵、专利/开源决策表、治理模板 | 发榜方书面答复、学校授权/IP 意见、CLA 签署、2+1 maintainer |
| W0-P 产品 | **BLOCKED** | TCO 三档、ROI 三情景、采用计划、价值指标、positioning、buy/build、product_case | 1 个内部 App 意向书、奖金/合同金额、真实客户 |
| W0-A 架构 | **PARTIAL** | 18-cell 五轴矩阵、P3-batch 设计、线程 A/B、streaming、row/vtable、Wasm 计划 | NAPI demo、TaskPool/sendable 验证、V3/V4 实测 |

**合计：0 PASS、1 PARTIAL、9 BLOCKED。**

## 2. 已经可以确定的事（不依赖设备）

1. **平台主推荐**：OpenHarmony 6.x/API 23 + ArkTS 1.1/1.2 混合 + NAPI interop；保守备选 5.1/API18 + NAPI-only；ANI 仅在发榜方强制 1.2/ANI 时启用，+15–25 人日。
2. **Schema 不能写死版本**：protobufjs 7.6.5 与 8.x 在默认值/oneof/`toObject({oneofs:true})`/verify/encode/未知字段上行为不同；G0 必须先读 `package.json` 锁 P7/P8。
3. **安全必须改写法**：`napi_value` 不能跨调用缓存；external buffer 不能靠“文档禁止 transfer”；wire 必须 `length ≤ remaining` 再分配；这些已写成规格/模板。
4. **法律主体**：推荐“高校/学院”作为揭榜单位，俱乐部作为执行团队；学生社团通常无法人资格；W2 公开前必须完成专利/学校审批。
5. **产品定位**：只值得先做内部工具（1 个 App、3 个热点 schema、40–60 人日）；社区标准件要 2–3 个采用者 + 2+1 maintainer；商业组件要 1 个付费试点。
6. **架构顺序**：P3-batch（一次 NAPI + N 次构造、无 tape）先试；再 P4-worker-sendable；再 P4-vtable；最后才是 P4-row 双端生成器。P4 不是唯一解。

## 3. 当前不能确定的事（硬阻断）

| 阻断 | 缺什么 | 谁补 | 截止 |
|---|---|---|---|
| V0 平台 | 目标板/镜像、DevEco/SDK/NDK、`hdc`/`hvigorw`、证书/profile | 用户/学校/俱乐部 | W0 D4 |
| V1/V3/V4 实测 | 目标板、Android 对照机、100 MB 真实数据、C++/NAPI 工程 | 用户/团队 | W0 D9 |
| V2 接口面 | 3 段真实业务代码、ArkTS 工程 | 发榜方/业务方 | W0 D9 |
| V5 Schema | 现网 `protobufjs` 包、pbjs 版本、热点 `.proto`/调用代码、`buf`/`protoc` | 发榜方/业务方 | W0 D6 |
| V6 安全 | 设备侧 ASan/NDK、osv-scanner/syft/scancode | 团队/CI | W0 D12 |
| 法律 | 发榜方主体答复、学校 IP 意见、CLA、2+1 maintainer | 学校/俱乐部 | W0 D4/W1 |
| 产品 | 1 个内部 App 意向书、奖金/合同金额 | 发榜方/业务方 | W4 |

## 4. 可复跑检查（已通过，但不是性能实测）

| 检查 | 命令 | 结果 |
|---|---|---|
| wire 参考模型 | `python out/w0/security/reference/test_wire_validator.py` | 38/38 通过（host Python，非 C++/设备） |
| 统计脚本 | `python out/w0/measurement/replay_kit/stats.py --selftest` | 通过（脚本自检，不是测量） |
| 数据 MANIFEST | `sha256sum -c out/w0/measurement/MANIFEST.sha256` | 14/14 通过 |
| 复跑骨架 | `bash out/w0/measurement/replay_kit/run_all.sh check` | 退出 2（5 个 BLOCKED，符合预期） |
| SBOM manifest | `python out/w0/security/sbom_template/ci/check_manifest.py` | template OK；release 因 TBD 失败（故意） |
| 安全门禁 | `bash out/w0/security/sbom_template/ci/security_gate.sh template` | fail=0，blocked=7（工具缺失） |

## 5. 红队结论

正式文件：`out/w0/redteam/W0_GoNoGo_Decision.md`

- **当前判 A（暂不投标/不按现方案投）**。
- 2026-11-22 前若 **V0、V2、V3、V5** 四个 gate 闭环，复评转 **B（内部工具）**；否则 A 定案。
- 不得判 GO；不得把 BLOCKED 改成 PARTIAL；不得用估算冒充实测。
- 红队已指出并已补：V2 无 owner（已补 `out/w0/v2/`）、`product_case.md` 缺失（已补）、schema Node 实测未入库（已补 `schema/evidence/`）、thread_model 过期提醒（已修）。

## 6. 下一步（只做三件）

1. **补 V0 平台件**：目标板/镜像 + DevEco/SDK/NDK + `hdc`/`hvigorw` + 证书；先装“空 HAP + NAPI `.so`”。
2. **补 V5/V2 真实物料**：现网 `protobufjs/package.json` + pbjs 版本 + 3–5 个热点 `.proto` + 3 段真实调用代码。
3. **补法律主体**：发榜方书面确认“高校/学院”可揭榜 + 学校 IP/Apache-2.0 出站意见 + 2+1 maintainer。

**在这三件补齐前，不写生成器、不采购大件、不公开仓库、不提交申报。**

## 7. 文件索引

- 平台：`out/w0/platform/`
- Schema：`out/w0/schema/`
- 测量：`out/w0/measurement/`
- 安全：`out/w0/security/`
- 法律：`out/w0/legal/`
- 产品：`out/w0/product/`
- 架构：`out/w0/architecture/`
- V2 接口面：`out/w0/v2/`
- 红队：`out/w0/redteam/`
- 基线对齐：`out/w0/BASELINE_ALIGNMENT.md`
