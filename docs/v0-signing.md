# V0 · 本地自签名 HAP（无设备 / 不安装）

> 日期：2026-10-29 ｜ 范围：**只做本地自签名 + verify**，不采购、不上板、不推远程
> 脚本：`scripts/sign_hap.sh` ｜ 证据：`evidence/sign_check.json` ｜ 材料：`signing/`
> 工具：DevEco Studio 自带 `hap-sign-tool.jar` + JBR 21（未安装任何新软件）

---

## 0. 结论

| 项 | 结果 |
|---|---|
| 自签名材料 | **已生成**（本地根 CA → 应用子 CA / profile 子 CA → 叶子证书，三级链） |
| signed HAP | `app/entry/build/default/outputs/default/entry-default-signed.hap`（**1,455,782 B**，本次运行） |
| `sign-app` 退出码 | **0**（`Sign Hap success!`） |
| `verify-profile` 退出码 | **0**（`verifiedPassed: true`） |
| **`verify-app` 退出码** | **0**（`verify: Verify success`，`Digest verify result: true`，codesign 覆盖 `libc++_shared.so` + `libentry.so`） |
| **是否可安装** | **否（installable=false）** — 自签名 CA 不在设备信任根内，且无设备 UDID 无法签 debug profile |
| 当前 BLOCKED | **安装 / 设备侧接受性验证**（无板、无 `hdc` target、无 UDID、无官方签名材料） |

**一句话**：HAP 已经**签得出来、验得过去**（`verify-app` exit 0），但**装不上真机**；这一条写死在 `evidence/sign_check.json` 的 `installable: false` 与 `blocked[]` 里，不构成"可安装"结论。

---

## 1. 环境与输入

| 项 | 值 |
|---|---|
| Java | `/d/Program Files/Huawei/DevEco Studio/jbr/bin/java.exe` → `openjdk 21.0.8 (JBR-21.0.8)` |
| 签名工具 | `/d/Program Files/Huawei/DevEco Studio/sdk/default/openharmony/toolchains/lib/hap-sign-tool.jar`（12,639,504 B） |
| 未签名 HAP | `app/entry/build/default/outputs/default/entry-default-unsigned.hap` |
| 未签名 HAP 大小 / sha256 | **1,390,057 B** / `d65c556a…c3d2fe45`（见 `evidence/sign_check.json`） |
| bundleName | `com.example.q207`（`app/AppScope/app.json5`） |
| compatibleSdkVersion | `6.1.1(24)` → `sign-app -compatibleVersion 24` |
| 设备 | **无**（`hdc list targets` 空；无 UDID） |
| `signingConfigs` | 无（`app/build-profile.json5` 为空数组，本流程不依赖 hvigor 签名配置） |

> 注意：未签名 HAP 在本次签名前被重新构建过（13:10，包含 `libs/arm64-v8a/libc++_shared.so`，故从先前的 127,135 B 变为 1,390,057 B）。**以 `evidence/sign_check.json` 中记录的 sha256 为准**，因为该文件锁定"签的到底是哪个字节流"。

---

## 2. 实际执行的命令

全部通过 `scripts/sign_hap.sh` 执行；单条命令可直接复现（Windows 路径 + `MSYS2_ARG_CONV_EXCL='*'`，见 §7）。

### 2.1 生成最小自签名材料（5 个密钥对 + 5 张证书）

```bash
# 密钥对（ECC NIST-P-256，全部放在 signing/q207-local.p12）
for alias in q207-root-ca q207-app-sub-ca q207-profile-sub-ca q207-app-key q207-profile-key; do
  java -jar hap-sign-tool.jar generate-keypair -keyAlias "$alias" -keyPwd '<local>' \
    -keyAlg ECC -keySize NIST-P-256 -keystoreFile signing/q207-local.p12 -keystorePwd '<local>'
done

# 根 CA（自签，pathLen=2）
java -jar hap-sign-tool.jar generate-cert -keyAlias q207-root-ca -keyPwd '<local>' \
  -issuer "C=CN,O=Q207,OU=Local Dev,CN=Q207 Local Root CA" \
  -issuerKeyAlias q207-root-ca -issuerKeyPwd '<local>' \
  -subject "C=CN,O=Q207,OU=Local Dev,CN=Q207 Local Root CA" -validity 3650 \
  -keyUsage certificateSignature,crlSignature \
  -basicConstraints true -basicConstraintsCa true -basicConstraintsPathLen 2 \
  -signAlg SHA256withECDSA \
  -keystoreFile signing/q207-local.p12 -keystorePwd '<local>' -outFile signing/certs/q207-root-ca.cer

# 应用子 CA / profile 子 CA（由根 CA 签发，pathLen=1）
java -jar hap-sign-tool.jar generate-cert -keyAlias q207-app-sub-ca ... \
  -subject "C=CN,O=Q207,OU=Local Dev,CN=Q207 App Signing CA" -outFile signing/certs/q207-app-sub-ca.cer
java -jar hap-sign-tool.jar generate-cert -keyAlias q207-profile-sub-ca ... \
  -subject "C=CN,O=Q207,OU=Local Dev,CN=Q207 Profile Signing CA" -outFile signing/certs/q207-profile-sub-ca.cer

# 叶子证书链（leaf + sub-CA + root，共 3 张）
java -jar hap-sign-tool.jar generate-app-cert -keyAlias q207-app-key -keyPwd '<local>' \
  -issuer "C=CN,O=Q207,OU=Local Dev,CN=Q207 App Signing CA" \
  -issuerKeyAlias q207-app-sub-ca -issuerKeyPwd '<local>' \
  -subject "C=CN,O=Q207,OU=Local Dev,CN=Q207 App Release" -validity 365 -signAlg SHA256withECDSA \
  -rootCaCertFile signing/certs/q207-root-ca.cer -subCaCertFile signing/certs/q207-app-sub-ca.cer \
  -outForm certChain -outFile signing/certs/q207-app-cert.pem

java -jar hap-sign-tool.jar generate-profile-cert -keyAlias q207-profile-key -keyPwd '<local>' \
  -issuer "C=CN,O=Q207,OU=Local Dev,CN=Q207 Profile Signing CA" \
  -issuerKeyAlias q207-profile-sub-ca -issuerKeyPwd '<local>' \
  -subject "C=CN,O=Q207,OU=Local Dev,CN=Q207 Profile Release" -validity 365 -signAlg SHA256withECDSA \
  -rootCaCertFile signing/certs/q207-root-ca.cer -subCaCertFile signing/certs/q207-profile-sub-ca.cer \
  -outForm certChain -outFile signing/certs/q207-profile-cert.pem
```

`signing/profile/q207-profile.json` 由脚本内联 Python 生成（**release 类型**，因此不需要 `debug-info.device-ids`）：

```json
{
  "version-name": "2.0.0",
  "version-code": 2,
  "app-distribution-type": "os_integration",
  "uuid": "<uuid4>",
  "validity": { "not-before": "<now-1h>", "not-after": "<now+300d>" },
  "type": "release",
  "bundle-info": {
    "developer-id": "Q207Local",
    "distribution-certificate": "<app 叶子证书 PEM>",
    "bundle-name": "com.example.q207",
    "apl": "normal",
    "app-feature": "hos_normal_app"
  },
  "acls": { "allowed-acls": [""] },
  "permissions": { "restricted-permissions": [] },
  "issuer": "pki_internal"
}
```

> `"issuer": "pki_internal"` 是**本地标签**，不代表 OpenHarmony 官方签发；这一点直接影响 installable（§4）。

### 2.2 签 profile 并验签

```bash
java -jar hap-sign-tool.jar sign-profile -mode localSign -keyAlias q207-profile-key -keyPwd '<local>' \
  -profileCertFile signing/certs/q207-profile-cert.pem \
  -inFile signing/profile/q207-profile.json -signAlg SHA256withECDSA \
  -keystoreFile signing/q207-local.p12 -keystorePwd '<local>' \
  -outFile signing/profile/q207-profile.p7b            # exit 0

java -jar hap-sign-tool.jar verify-profile \
  -inFile signing/profile/q207-profile.p7b \
  -outFile signing/verify/profile_verify.json          # exit 0
```

`signing/verify/profile_verify.json`：

```json
{ "verifiedPassed": true, "message": "OK", "content": { "...": "已签 profile 的完整内容" } }
```

### 2.3 签 HAP 并验签

```bash
java -jar hap-sign-tool.jar sign-app -mode localSign -keyAlias q207-app-key -keyPwd '<local>' \
  -appCertFile signing/certs/q207-app-cert.pem \
  -profileFile signing/profile/q207-profile.p7b \
  -inFile  app/entry/build/default/outputs/default/entry-default-unsigned.hap \
  -signAlg SHA256withECDSA \
  -keystoreFile signing/q207-local.p12 -keystorePwd '<local>' \
  -outFile app/entry/build/default/outputs/default/entry-default-signed.hap \
  -compatibleVersion 24 -signCode 1                    # exit 0

java -jar hap-sign-tool.jar verify-app \
  -inFile       app/entry/build/default/outputs/default/entry-default-signed.hap \
  -outCertChain signing/verify/verified-certchain.cer \
  -outProfile   signing/verify/verified-profile.p7b    # exit 0
```

---

## 3. 结果（可复跑）

| # | 命令 | 退出码 | 关键输出 |
|---:|---|---:|---|
| 1–5 | `generate-keypair` ×5 | 0 | `generate-keypair success` |
| 6 | `generate-cert`（root） | 0 | `generate-cert success` |
| 7–8 | `generate-cert`（app/profile 子 CA） | 0 | 同上 |
| 9 | `generate-app-cert`（certChain） | 0 | `generate-app-cert success`，链内 3 张证书 |
| 10 | `generate-profile-cert`（certChain） | 0 | `generate-profile-cert success`，链内 3 张证书 |
| 11 | `sign-profile` | 0 | `sign-profile success` |
| 12 | `verify-profile` | 0 | `verifiedPassed: true, message: OK` |
| 13 | `sign-app` | 0 | `profile type is: release` / `Sign Hap success!` |
| 14 | **`verify-app`** | **0** | `Find Hap Signing Block success, version: 3, block count: 3`；`verify codesign success`；`Digest verify result: true, DigestAlgorithm: SHA-256`；`verify: Verify success` |

`verify-app` 关键细节（`signing/logs/verify-app.log`）：

```text
Find Hap Signing Block success, version: 3, block count: 3
verify lib: libs/arm64-v8a/libc++_shared.so
verify lib: libs/arm64-v8a/libentry.so
verify codesign success
Subject: CN=Q207 App Signing CA,  Issuer: CN=Q207 Local Root CA
Subject: CN=Q207 Local Root CA,   Issuer: CN=Q207 Local Root CA   (自签根)
Subject: CN=Q207 App Release,     Issuer: CN=Q207 App Signing CA
Digest verify result: true, DigestAlgorithm: SHA-256
verify: Verify success
```

产物对照：

| 产物 | 大小 | 说明 |
|---|---:|---|
| `entry-default-unsigned.hap` | 1,390,057 B | 15 个 zip 条目 |
| `entry-default-signed.hap` | 1,455,782 B | 16 个条目（签名后新增 `.pages.info`，并按签名要求改为未压缩存储，故体积增大） |
| `signing/profile/q207-profile.p7b` | ~3.0 KB | 已签 profile |
| `signing/certs/q207-app-cert.pem` | ~2.1 KB | 应用证书链（3 张） |
| `signing/certs/q207-profile-cert.pem` | ~2.2 KB | profile 证书链（3 张） |
| `signing/q207-local.p12` | ~5 KB | **含私钥，仅本地** |

> 每次重签，profile 的 `uuid` 与 `validity` 时间戳会变，signed HAP 大小/哈希会有几十字节级差异（例如 1,455,763 / 1,455,782 B）。**以 `evidence/sign_check.json` 中的 size/sha256 为唯一准确值**。

签名前后 HAP 内业务内容一致（`module.json` / `resources.index` / `ets/modules.abc` / `libs/arm64-v8a/libentry.so` / `libc++_shared.so` 均在）；`sign-app` 只追加签名块与 `.pages.info`。

---

## 4. 为什么 `installable=false`

签名是"自洽"的，但设备只认官方信任根。三条独立原因，任一条都足以导致装不上：

1. **证书链锚点不被信任**：根 CA 是本地自签的 `CN=Q207 Local Root CA`，不在设备/系统的 OpenHarmony 信任根列表里 → 设备验签会失败（`9568289 install failed verify signature` 一类错误）。
2. **没有官方 Provision Profile**：`issuer` 写的是本地 `pki_internal`，`app-distribution-type` 是 `os_integration`；真机要求 profile 由官方（或对应设备镜像信任的）根签发，且 profile 内的 `distribution-certificate` 必须与签名证书一致并同样可链到信任根。
3. **没有设备 UDID 走 debug 路线**：`UnsgnedDebugProfileTemplate.json` 要求 `debug-info.device-ids` 至少一个真实 UDID；当前无板、无 `hdc` target，拿不到 UDID，因此连"debug 可装"的替代路径也走不通。

> 结论：**HAP 可用于本地验签 / 结构检查 / 打包链路验证**，但**不得声称可安装、可上架、可过审**。

---

## 5. 当前 BLOCKED（需要外部条件，不是脚本问题）

| # | BLOCKED 项 | 影响 | 解锁所需材料 |
|---:|---|---|---|
| B1 | **设备侧安装与接受性验证** | 无法证明 `hdc install` 成功、无法证明设备验签通过 | 一块目标板/手机 + 对应镜像 + `hdc list targets` 非空 |
| B2 | **官方签名材料** | 自签名 HAP 在真机上必被拒 | 发榜方/OpenHarmony 签发的应用证书链 + Provision Profile（release），或官方 debug 签发通道 |
| B3 | **设备 UDID（debug 路线）** | debug profile 无法生成 | `hdc shell bm get -u`（或 `hdc shell bm get --udid`）拿 UDID 后填入 `debug-info.device-ids` |
| B4 | **hvigor 签名配置（可选自动化）** | 当前签名不经过 `signingConfigs`，构建产物仍是 unsigned | `app/build-profile.json5` 加 `signingConfigs`（本任务禁止修改该文件，故未做） |

---

## 6. 复现方式

```bash
# 首次：生成全部材料 + 签名 + 验签 + 写证据
bash scripts/sign_hap.sh

# 强制重建签名材料（换一套 CA/密钥；保留 signing/README.md）
bash scripts/sign_hap.sh --force

# 幂等重跑：复用 signing/q207-local.p12，只重建 profile 与 signed HAP
bash scripts/sign_hap.sh
```

产物位置：
- signed HAP → `app/entry/build/default/outputs/default/entry-default-signed.hap`
- 证据 → `evidence/sign_check.json`（含每条命令退出码、产物 sha256、`installable=false`、`blocked[]`）
- 日志 → `signing/logs/`（每个命令一份；`records.tsv` 为汇总）
- 验签输出 → `signing/verify/`

---

## 7. 踩坑记录（错误码 + 处理）

| # | 报错 | 原因 | 处理 |
|---:|---|---|---|
| 1 | `ERROR: 11011005 Param is not trusted` | `generate-cert` 的 `-keyUsage` 不接受 X.509 惯用名 `keyCertSign,cRLSign`，只接受工具白名单值 | 改用 `-keyUsage certificateSignature,crlSignature` |
| 2 | `ERROR: 11014007 Key alias {q207-root-ca} password error`（`UnrecoverableKeyException`） | 用 `generate-ca` 建的密钥，其口令与后续 `-issuerKeyPwd` 不匹配（`generate-ca` 没有 `-keyPwd` 参数） | 改为先用 `generate-keypair -keyPwd <pwd>` 显式建密钥，再用 `generate-cert` 签 CA 证书 |
| 3 | `ERROR: 11012002 File not exist`（路径被改写） | MSYS2 会把 `-inFile /d/...` 这类 POSIX 参数自动转成 Windows 路径；再叠加 `cygpath` 会二次转换 | 统一在脚本内用 `sed` 转 `D:\...` 并 `export MSYS2_ARG_CONV_EXCL='*'` 关闭自动转换 |
| 4 | 签名后 HAP 从 1.39 MB 变 1.45 MB | `sign-app` 追加签名块、新增 `.pages.info`，且按签名要求改为未压缩存储 | 属预期现象，不影响内容；`verify-app` 通过即说明结构正确 |

---

## 8. 安全注意

- `signing/q207-local.p12` **包含私钥**；口令是脚本里的本地测试值 `***REMOVED***`，**只能用于本地演示**，不得用于任何发布物。
- `.gitignore` 已补 `signing/*.p12`、`signing/*.jks`、`signing/*.pem`、`signing/*.cer`、`signing/*.csr`、`signing/*.key`、`signing/*.p7b`、`signing/logs/`、`signing/verify/`；**私钥与证书链不进版本库**。
- 提交前自检：`git status --porcelain signing/` 应只显示 `signing/README.md` 与 `signing/profile/q207-profile.json`（无密钥/证书）。
- 本流程**没有**安装任何软件、**没有**连接/刷写任何设备、**没有**推远程。

---

## 9. 与既有证据的关系

- `evidence/v0_build.json`（上一阶段构建证据）**未改动**；该文件记录 `signed: false` 的 unsigned HAP，本文件不覆盖它，只新增 `evidence/sign_check.json` 作为签名阶段的独立记录。
- `evidence/sign_check.json` 的 `artifacts.hap_unsigned.sha256` 锁定了本次实际签名的 unsigned HAP，便于事后核对"HAP 是否被重建过"。
- `scripts/sign_hap.sh` 是**新增**脚本，不修改 `scripts/build.sh`；`host/napi`、`Index.ets`、`RISKS.md` 均未触碰。
