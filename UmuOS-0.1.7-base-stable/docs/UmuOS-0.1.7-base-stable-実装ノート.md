---
title: UmuOS-0.1.7-base-stable 実装ノート
date: 2026-02-22
status: in_progress
---

# UmuOS-0.1.7-base-stable 実装ノート

このノートは、[UmuOS-0.1.7-base-stable 詳細設計書](UmuOS-0.1.7-base-stable-詳細設計書.md) を **Tera Term で手動実装**した際の「差分・詰まり・判断」を記録する。

目的：詳細設計書の手順をそのまま実行して、途中修正なしで完走できることを確認する。

---

## 固定値（今回の実装で使うもの）

- 作業ルート（Ubuntu）：`/home/tama/umu_project/UmuOS-0.1.7-base-stable`
- Kernel source：`/home/tama/umu_project/external/linux-6.18.1`
- BusyBox source：`/home/tama/umu_project/external/busybox-1.36.1`
- ISO：`UmuOS-0.1.7-base-stable-boot.iso`
- 起動スクリプト：`UmuOS-0.1.7-base-stable_start.sh`

---

## 進捗ログ

### 2026-02-22

- 実施範囲：
- 結果：

**観測点（この時点）**
- 

---

## 詰まり・差分（重要）

形式：

- 章/コマンド：
- 期待：
- 実際：
- 対処：
- 再発防止（設計書への反映要否）：

---

## 起動観測（ゲスト）

### 2026-02-22

- `mount` で `/dev/vda on / type ext4` が見えるか：
- `/logs/boot.log` が追記されるか：
- `date` が `JST` 表示か：
- NTP 同期（`[ntp_sync] before/after`）の有無：

---

## テスト結果（ゲスト）

### 2026-02-22

- JST / ntpd 同期：
- su：
- ll：
- FTP：
- telnet：

---

## 受け入れ

### 2026-02-22

- 判定：
- 根拠（観測点）：
