---
title: UmuOS-0.1.6-dev 実装ノート
date: 2026-02-15
status: in-progress
---

# UmuOS-0.1.6-dev 実装ノート

このノートは、[UmuOS-0.1.6-dev 詳細設計書](UmuOS-0.1.6-dev-詳細設計書.md) を手動で実行した際の「差分・詰まり・判断」を記録する。

---

## 進捗

### 2026-02-15

- 章「1. Ubuntu 事前準備（パッケージ）」〜「4. BusyBox（静的リンク、対話なし）」まで実施
- 結果: 問題なし（詳細設計書の手順どおりに完了、途中修正なし）

**観測点（この時点）**
- BusyBox: `busybox` が static で生成され、`ntpd` / `tcpsvd` / `ftpd` が `--list` で確認できた
- ログ: kernel / busybox のビルドログは以下に出力されている想定
  - `logs/kernel_build_bzImage.log`
  - `logs/busybox_build.log`
