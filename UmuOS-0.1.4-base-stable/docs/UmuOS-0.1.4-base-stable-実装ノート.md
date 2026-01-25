---
title: UmuOS-0.1.4-base-stable 実装ノート
date: 2026-01-25
related_design: "./UmuOS-0.1.4-base-stable-詳細設計書.md"
status: ongoing
---

# 0. このノートの目的

- 試行錯誤を「再現可能な手順」と「切り分けの根拠（ログ/観測点）」として残す。
- 失敗を成果にする（どこが制約で、どこを変えると成立するかを明確化）。
- 1回の試行で変える点を最小化し、原因同定の速度を上げる。

# 1. ルール（固定）

## 1.1 1試行 = 1エントリ
各試行は「ID（連番）」「変更点（1〜2個）」「観測」「結論」を必ず持つ。

## 1.2 必ず残すログ
可能な範囲で以下を保存・参照する。

- ホスト（Rocky）: `logs/host_qemu.console.log`（`script` で採取）
- ゲスト（UmuOS）: `/logs/boot.log`
- 追加: `run/qemu.cmdline.txt` の実際に実行した内容

## 1.3 変更の粒度
- 1回の試行で触るのは「1〜2点まで」。
- 変えたら、必ず「戻し方（ロールバック）」も書く。

# 2. 実行環境メモ（最初に1回だけ埋める）

- Ubuntu: 24.04（ビルド）
- Rocky: 9.7（起動/受入）
- QEMU 実体: `/usr/libexec/qemu-kvm`
- br0 の有無・IP: （例）`192.168.0.200/24`
- firewalld: active/inactive
- SELinux: Enforcing/Permissive/Disabled

# 3. 合格条件チェック表（毎回◯×を埋める）

## 3.1 0.1.3互換（維持）
- [ ] switch_root 成立（ttyS0ログに `exec: /bin/switch_root /newroot /sbin/init`）
- [ ] ttyS0 root ログイン
- [ ] ttyS0 tama ログイン
- [ ] ttyS1 同時ログイン
- [ ] `/logs/boot.log` 追記

## 3.2 0.1.4追加（telnet）
- [ ] `eth0` に `192.168.0.202/24`
- [ ] default route `via 192.168.0.1`
- [ ] LAN から `192.168.0.202:23` 接続
- [ ] telnet で root ログイン
- [ ] telnet で tama ログイン

## 3.3 追加（nc転送）
- [ ] ゲスト受信（`nc -l -p 12345 > payload.bin`）
- [ ] Ubuntu送信（`nc 192.168.0.202 12345 < payload.bin`）

# 4. 試行ログ（ここに追記していく）

---

## 試行 0001（YYYY-MM-DD）

### 変更点（この試行で変えたこと）
- （例）`grub.cfg` に `rootwait` を追加
- （例）BusyBox を `make CC=musl-gcc` に変更

### 実行手順（実際に打ったコマンド）
- Ubuntu:
  - 
- Rocky:
  - 

### 観測（ログ/画面で見えた事実）
- ttyS0:
  - 
- host（Rocky）`logs/host_qemu.console.log`:
  - 
- ゲスト `/logs/boot.log`:
  - 

### 判定（チェック表の◯×と理由）
- 0.1.3互換:
  - 
- telnet:
  - 

### 仮説（なぜこうなったか）
- 

### 次の最小変更（次試行で変える1点）
- 

### ロールバック（元に戻す方法）
- 

---

## 試行 0002（YYYY-MM-DD）
（追記）
