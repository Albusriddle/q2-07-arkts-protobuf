# V0 ohosTest：ping/add 测试构建（未执行）

日期：2026-10-29 ｜ 分支：`feature/p1-v0-min`

## 结果

| 项 | 值 |
|---|---|
| 测试文件 | `app/entry/src/ohosTest/ets/test/EntryNapi.test.ets` |
| 用例 | `ping() == 'pong'`；`add(2,3) == 5` |
| 依赖 | `@ohos/hypium 1.0.30`（`app/entry/oh-package.json5` devDependencies） |
| 构建命令 | `hvigorw assembleHap --mode module -p module=entry@ohosTest -p product=default -p buildMode=debug --no-daemon` |
| 构建结果 | **BUILD SUCCESSFUL**，exit 0 |
| 产物 | `app/entry/build/default/outputs/ohosTest/entry-ohosTest-unsigned.hap` |
| 设备执行 | **未执行**（`hdc list targets` 为空，无板） |
| 证据 | `evidence/v0_ohostest_build.json`、`evidence/hap_build_ohostest.log` |

## 边界

- 只证明测试代码/测试 HAP 可编译，不证明 `ping/add` 在真机通过。
- 测试 HAP 未签名；即使有板也需先签名/配置 profile。
- `entry-ohosTest-unsigned.hap` 与主 HAP 不同，安装/运行需分别处理。
