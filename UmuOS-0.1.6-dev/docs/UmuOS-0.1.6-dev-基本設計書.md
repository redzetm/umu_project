---
title: UmuOS-0.1.6-dev 基本設計書
date: 2026-02-11
status: baseline
related_docs:
  - "./UmuOS-0.1.6-dev-詳細設計書.md"
---

# UmuOS-0.1.6-dev 基本設計書

## 1. 目的

- UmuOS-0.1.6-dev を「Kernel + initramfs + switch_root + ext4(rootfs)」の最小構成で起動できる。
- 永続 rootfs（disk.img）側に、運用に必要な最低限のユーザーランド機能を統合する。
- **手順の再現性を最優先**し、途中で修正が入らない“完成形”を最初から組み立てる。

## 2. 非目的

- systemd を使った本格的なディストリビューション互換。
- パッケージ管理、ユーザー追加/削除、堅牢なセキュリティ設計（0.1.6-dev は学習・検証用）。
- 複数マシン構成、複雑なネットワーク機能（VLAN/iptables等）。

## 3. スコープ（0.1.6-devで成立させる機能）

- 起動
  - GRUB → Linux kernel → initramfs → ext4 rootfs へ `switch_root`
  - `root=UUID=...` で disk.img を rootfs としてマウント
- コンソール/ログ
  - `ttyS0` を主コンソール
  - `/logs/boot.log` に起動情報（boot_id/time/uptime/cmdline/mount/ntp）を追記
- ネットワーク（ゲスト）
  - `eth0` に static IP 設定（`/etc/umu/network.conf`）
  - `/etc/resolv.conf` に DNS
- 時刻
  - `TZ=JST-9` を固定
  - 起動時に1回だけ NTP 同期（BusyBox `ntpd` を利用）
- ユーティリティ
  - `/umu_bin` を最優先 PATH にする
  - `ll`（`ls -lF` のラッパ）
- リモート
  - telnet ログイン（BusyBox `telnetd` + `login`）
  - FTP サーバ（BusyBox `tcpsvd` + `ftpd`、公開ルート `/`）
- 自作 `su`
  - 静的リンクの小さな `su` を `/umu_bin/su` に配置（SUID root）

## 4. 実行環境の前提

- ビルド環境（Ubuntu）
  - kernel/busybox をビルドできるツールチェーンがある
  - ISO 作成に `grub-mkrescue` 等が使える
- 実行環境（Rocky）
  - QEMU/KVM が利用可能（`/dev/kvm`）
  - ネットワークを使う場合は bridge + TAP が用意される（例：`br0` + `tap-umu`）
  - ネット無し起動モードも用意（切り分け用途）

## 5. 成果物（Artifacts）

ビルドの最終成果物は次の3点。

- ISO: `UmuOS-0.1.6-boot.iso`
  - `vmlinuz-6.18.1`（kernel）
  - `initrd.img-6.18.1`（initramfs）
  - `grub.cfg`
- 永続ディスク: `disk/disk.img`
  - ext4 / UUID 固定 / 4GiB
  - `/etc/init.d/rcS`、`/etc/profile`、`/umu_bin`、`/logs` を含む
- 起動スクリプト: `UmuOS-0.1.6-dev_start.sh`
  - Rocky `/root` に **ISO + disk.img + start.sh** だけ置けば起動可能

## 6. ディレクトリ設計（リポジトリ内）

- `kernel/build/`
  - out-of-tree build 出力
- `initramfs/`
  - `rootfs/`（initramfsの素材）
  - `busybox/work/`（BusyBox作業コピー）
- `disk/`
  - `disk.img`
- `iso_root/`
  - `boot/`（kernel/initrd/grub.cfg）
- `tools/`
  - `rcS_umuos016.sh`（rcSテンプレ。唯一の正）
  - `patch_diskimg_rcS.sh`（既存disk.imgへ rcS 差し替え。安全弁）
- `logs/`
  - ビルドログ

## 7. 主要コンポーネント設計

### 7.1 起動フロー

- GRUB が kernel + initrd をロード
- initramfs の `/init` が rootfs（disk.img）をマウント
- `switch_root` で rootfs に遷移
- rootfs 側 `/sbin/init`（BusyBox）→ `/etc/inittab` → `/etc/init.d/rcS`

### 7.2 rootfs 初期化（rcS）

- `proc/sys/devtmpfs/devpts` をマウント
- `/logs` 等の作業ディレクトリを用意
- 起動ログを `/logs/boot.log` に追記
- `/etc/umu/network.conf` に従って static 設定（`ip link/addr/route`）
- NTP 同期を1回実行し、前後の時刻と出力をログへ記録
- telnetd/ftpd を起動

### 7.3 PATH / TZ 方針

- `rcS` の `export` だけではログイン後に反映されないケースがあるため、`/etc/profile` にも `PATH` と `TZ` を固定する。

### 7.4 FTP 公開範囲

- BusyBox `ftpd` の公開ルートは引数で決まるため、要求どおり `/` を公開ルートとする。
  - ただし実際に見える範囲は Linux のパーミッションに従う。

## 8. インターフェース

- シリアル
  - `ttyS0`: コンソール
  - `ttyS1`: TCP で接続できるデバッグ用（ホスト127.0.0.1:5555）
- ネットワーク
  - ゲスト `eth0` を static で設定
  - ホストは TAP/bridge（または `NET_MODE=none`）

## 9. 成功条件（Doneの定義）

- ゲストが rootfs（ext4）で起動し `mount` で `/dev/vda on / type ext4` が確認できる。
- `/logs/boot.log` に `boot_id` と `[ntp_sync] before/after` が追記される。
- ログイン後 `PATH` 先頭が `/umu_bin`。
- FTP/telnet が起動している（プロセス/`/run/ftpd.pid`）。

## 10. 参照

- 詳細な手順（コピペ手順書）: `UmuOS-0.1.6-dev-詳細設計書.md`
