# `signing/` — 本地自签名材料（仅本机，禁止提交私钥）

> 由 `scripts/sign_hap.sh` 生成 ｜ 流程说明见 [`docs/v0-signing.md`](../docs/v0-signing.md) ｜ 证据见 [`evidence/sign_check.json`](../evidence/sign_check.json)

## 目录内容

| 路径 | 内容 | 是否进版本库 |
|---|---|---|
| `q207-local.p12` | **含 5 个私钥**（root CA、app 子 CA、profile 子 CA、app key、profile key），口令为脚本内本地测试值 | **否**（`.gitignore` 已忽略） |
| `certs/q207-root-ca.cer` | 本地自签根 CA（pathLen=2，10 年） | 否 |
| `certs/q207-app-sub-ca.cer`、`certs/q207-profile-sub-ca.cer` | 两个子 CA（pathLen=1，5 年） | 否 |
| `certs/q207-app-cert.pem` | 应用叶子证书链（leaf + app 子 CA + root，3 张） | 否 |
| `certs/q207-profile-cert.pem` | profile 叶子证书链（leaf + profile 子 CA + root，3 张） | 否 |
| `profile/q207-profile.json` | Provision Profile 明文（release 类型，bundle `com.example.q207`，无 UDID） | **是**（不含私钥，便于复现） |
| `profile/q207-profile.p7b` | 已签 profile（p7b） | 否 |
| `verify/` | `verify-profile` / `verify-app` 的输出（`profile_verify.json`、`verified-certchain.cer`、`verified-profile.p7b`） | 否 |
| `logs/` | 每条签名命令的日志 + `records.tsv` 汇总 | 否 |

## 重要限制

- **不可安装**：根 CA 是本地自签的，不在设备信任根内；且没有设备 UDID，无法签 debug profile。签出来的 HAP 只能**本地验签**，不能上真机（详见 `docs/v0-signing.md` §4）。
- 口令 `***REMOVED***` 是**本地测试值**，硬编码在 `scripts/sign_hap.sh` 里；任何发布用途都必须换掉并改由密钥管理提供。
- 重新生成：`bash scripts/sign_hap.sh --force`（删掉生成的密钥/证书/profile/日志并重建，保留本 README）；日常重跑 `bash scripts/sign_hap.sh`（复用 p12，只重建 profile 与 signed HAP）。

## 提交前自检

```bash
git status --porcelain signing/     # 期望只看到 README.md 与 profile/q207-profile.json
git check-ignore -v signing/q207-local.p12 signing/certs/q207-app-cert.pem
```
