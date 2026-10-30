# History rewrite：移除旧测试口令

日期：2026-10-30

## 操作

- 使用 `git filter-repo --replace-text` 将历史提交中的旧测试口令替换为
  `***REMOVED***`；
- 本地 mirror 备份：`../repo-pre-secret-rewrite`（不在仓库内）；
- 所有远端分支已 force-push；当前 `main` 为 rewrite 后的提交。

## 影响

- 所有 rewrite 前的 commit SHA 与依赖这些 SHA 的 stage evidence 不再可达；
  docs/evidence 中的旧 SHA 只作为“pre-rewrite 历史记录”阅读。
- GitHub PR refs 可能仍缓存旧对象；如要彻底清理，需要 GitHub Support/仓库 GC
  或删除旧 PR refs。
- 当前 p12 早已轮换；旧测试口令不再有效。

## 验证

- `git log --all -S '<old secret>'` 应为空；
- 当前树不包含旧口令；
- 构建/测试仍以当前 `main` 为准。
