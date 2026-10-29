# V0 最小切片风险登记（RISKS.md）

> 状态：**不阻塞当前代码切片**。本文件只记录已知风险；当前授权范围是本地写代码、建分支、构建、跑本机测试，不含采购、真实设备、远程推送、公开仓库、申报。

## R1 原红队三类项目级风险（用户接受，不阻塞）

| # | 风险 | 现状 | 影响 | 当前处理 |
|---:|---|---|---|---|
| R1.1 | ArkTS 值语义无法完全复刻 protobuf.js | 语言限制：无 prototype 写权、禁 `delete`/`Object.defineProperty`/动态索引/`in` | “业务零改动”只能按 API 级兼容；JSON/Object.keys/oneof 可能不同 | 本切片只验证 NAPI 调用，不承诺值语义；后续用差异清单/验收签字 |
| R1.2 | 700 ms/4.29× 物理地板 | 未测；依赖 H、c、对象构造地板 | 不能承诺 700 ms/4.29× | 本切片不测性能；只做 .so + NAPI + ArkTS 调用 |
| R1.3 | 20 周/人日与兼容/安全工作量 | 未闭环；设备日 33–35 vs 14 工作日 | 项目级排期风险 | 本切片时间盒 2 小时，不承担项目排期 |

## R2 设备缺失（不阻塞）

- 目标 RK3568/OpenHarmony 板未连接；Android 对照机未连接。
- 当前只能构建 arm64 `.so` 与 HAP（若模板/签名允许），**不能安装/运行/测性能**。
- `evidence/v0_build.json` 的 `hdc_status` 必须写实际状态；没有设备就写 `blocked`，不得伪造安装结果。

## R3 HAP 签名/安装不确定（不阻塞）

- 本切片实际产出了 **unsigned HAP**：`app/entry/build/default/outputs/default/entry-default-unsigned.hap`。
- 没有 `signingConfigs`/profile，hvigor 明确 `Will skip sign`；因此 **HAP 不能安装到真实设备**。
- 签名、profile、安装、启动均未执行；不得把“unsigned HAP 构建成功”写成“设备可运行”。

## R4 未采购（不阻塞）

- 不采购板、手机、证书、云资源。
- 本切片只使用本机已安装的 DevEco/SDK/NDK/cmake/ninja/hdc（在盘，不一定在 PATH）。

## R5 未跑真实设备（不阻塞）

- 未执行 `hdc install`、未启动 HAP、未跑 NAPI 真机调用。
- 任何“ping/add 已跑通”只能指“编译/链接通过”，不能指“设备运行通过”；除非后续有真实设备日志。

## R6 构建环境风险（不阻塞）

- `protoc`/`buf` 未安装；本切片不依赖它们。
- `hdc`/`cmake`/`ninja` 在 SDK 目录但可能不在 PATH；`scripts/check_env.sh` 会检测并记录。
- SDK 为 API 24 / 6.1.1.125；HAP `compatibleSdkVersion` 如与模板不匹配，HAP 可能失败，记日志即可。

## 授权边界

- 允许：本地工作区写代码、建分支、构建、跑本机测试。
- 禁止：采购、真实设备、远程推送、公开仓库、提交申报、伪造设备/性能证据。
