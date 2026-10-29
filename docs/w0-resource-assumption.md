# W0 资源假设覆盖说明（2026-10-29）

用户明确：V0–V6、W0-L、W0-P、W0-A 所需资源均可获得/可解决，不再视作 block。

因此：
1. W0 的 9 个 `BLOCKED` 重分类为 `RESOURCE-GATED`（资源待到位，不是技术不可行）。
2. W0 的 `W0_GoNoGo_Decision.md` 判 A（暂不投标）**暂停作为最终结论**；它仍保留为“资源未到位时的历史判定”。
3. 新的可行性分析见 `out/Q2-07_可行性重估_资源可得版.md`：资源可得后判 **B-（有条件可投，约 3.55/5）**。
4. 资源可得不等于已获得；V0/V2/V3/V4/V5/V6 仍需 D 级证据才能最终验收。当前不写生成器、不承诺 700 ms 的规则仍适用，直到 G1 通过。

## 重分类表

| W0 gate | 原判定 | 重分类 | 说明 |
|---|---|---|---|
| V0 | BLOCKED | RESOURCE-GATED | 板/工具链可采购/安装 |
| V1 | BLOCKED | RESOURCE-GATED | 手机可借、工程/数据可生成 |
| V2 | BLOCKED | RESOURCE-GATED | 开源代码/自写代码可获取；接口定义仍需拍板 |
| V3 | BLOCKED | RESOURCE-GATED | tape/materializer 可自建 |
| V4 | BLOCKED | RESOURCE-GATED | NAPI 工程可生成/自建 |
| V5 | BLOCKED | RESOURCE-GATED | schema/工具可获取 |
| V6 | BLOCKED | RESOURCE-GATED | 工具可安装、`.so` 可构建；设备 ASan 可能受限 |
| W0-L | BLOCKED | RESOURCE-GATED | 已授权；保留书面记录 |
| W0-P | BLOCKED | RESOURCE-GATED | 不需要客户；学校经费 |
| W0-A | PARTIAL | PARTIAL | 设计完成；待 demo/实测 |
