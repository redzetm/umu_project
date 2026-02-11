---
title: UmuOS-0.1.5-dev 基本設計書
date: 2026-02-11
status: rewrite-for-manual
---

# UmuOS-0.1.5-dev 基本設計書

この文書は「何を作るか（要件/固定値/責務境界）」を定義する。
作業コマンド（どう作るか）は詳細設計書（`UmuOS-0.1.5-dev-詳細設計書.md`）に集約する。

## 1. 目的 / 位置づけ

UmuOS-0.1.5-dev は、UmuOS-0.1.4-base-stable（成功実績あり）を前提に、ユーザーランド開発で必要な最小の周辺機能を「永続 rootfs（ext4: disk.img）側」に統合する。

追加するのは、0.1.4 までは手作業で差し込んでいた機能（DNS/JST/NTP、`/umu_bin`、`ll`、自作 `su`、`ftpd`）であり、起動フローの責務境界に沿って rcS と永続 rootfs の構成として固定する。

## 2. 重要方針（固定）

- 0.1.4-base-stable の成立条件を壊さない。
- 追加機能は initramfs 側に入れず、永続 rootfs（disk.img）側へ統合する。
- 作業手順は「TeraTerm でコピペ実行できる」ことを最優先にする。
  - 1ブロック＝1貼り付け、対話操作（`menuconfig`）を避ける。
  - パスは絶対パスで固定し、変数を使う場合も絶対パス値のみを格納する。
- 検証（観測点）は“必須のゲート”にしない。失敗時の切り分け点としてのみ提示する。

## 3. スコープ

### 3.1 やること（固定）

0.1.4 互換のベース（カーネル/initramfs/switch_root/永続 rootfs/ログイン/ネットワーク/telnet）を保ったまま、以下を統合する。

- DNS：`/etc/resolv.conf` を固定し、名前解決が可能。
- JST：`/etc/TZ` を `JST-9` に固定。
- NTP：BusyBox `ntpd` を手動起動できる（起動時自動同期はしない）。
- `/umu_bin`：PATH 先頭固定（rcS で export）。
- `ll`：`/umu_bin/ll`（`ls -lF` ラッパ）。
- `su`：BusyBox `su` に依存しない自作 `/umu_bin/su`（setuid）。
- FTP：BusyBox `tcpsvd` + `ftpd` を LAN 内限定で常駐。

### 3.2 やらないこと（固定）

- SSH 導入
- インターネット越し運用（WAN 露出）
- 監視・自動再起動（systemd ユニット化等）
- 0.1.4-base-stable の受入条件に無関係な大規模改変（initramfs設計の全面変更など）

## 4. 前提環境 / 固定値（この文書で固定）

### 4.1 作業環境

- 作業ホスト：Ubuntu 24.04 LTS
- 操作：TeraTerm から貼り付けて bash へ投入できること

### 4.2 ネットワーク前提（固定IP）

- RockyLinux（ホスト）：`192.168.0.200`
- Ubuntu（開発）：`192.168.0.201`
- UmuOS（ゲスト）：`192.168.0.202/24`、GW `192.168.0.1`
- L2：Rocky 側にブリッジ `br0` が存在する。
- ゲストは TAP（`tap-umu`）を介して `br0` に接続し、LAN に L2 参加する。
- libvirt 管理下の `vnet*` は使用しない。

### 4.3 ツール/ソース（固定）

- Linux kernel：`6.18.1`（ソース：`/home/tama/umu/umu_project/external/linux-6.18.1-kernel/`）
- BusyBox：`1.36.1`（ソース：`/home/tama/umu/umu_project/external/busybox-1.36.1/`）

### 4.4 生成物（固定名）

- ISO：`UmuOS-0.1.5-boot.iso`
- kernel（ISO入力）：`iso_root/boot/vmlinuz-6.18.1`
- initrd（ISO入力）：`iso_root/boot/initrd.img-6.18.1`
- 永続ディスク：`disk/disk.img`
- rootfs UUID：`d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15`

## 5. 要件（合格条件の定義）

### 5.1 0.1.4 相当（維持）

- `switch_root` が成立する。
- ttyS0 で `root`/`tama` のパスワードログインが成立する。
- ttyS1（QEMU TCP シリアル）でも同時ログインが成立する。
- ゲスト側 `/logs/boot.log` が追記される。
- ゲスト `eth0` に `192.168.0.202/24` が設定される。
- デフォルトルート `default via 192.168.0.1` が設定される。
- LAN 端末から `192.168.0.202:23` に接続し、`root` と `tama` のログインが成功する。
- Ubuntu から `nc` でゲストへファイル転送が成功する。

### 5.2 0.1.5 追加（固定）

DNS：名前解決できる（例：`ping -c 1 google.com`）。

タイムゾーン/時刻：`/etc/TZ` が `JST-9`、かつ `ntpd` による同期が実行できる。

`/umu_bin`：PATH 先頭、権限 `root:root 0755`。

`ll`：`/umu_bin/ll` が `ls -lF` 相当。

`su`：一般ユーザーから `/umu_bin/su` で root へ切替（root パスワード必須）。

FTP：LAN 内から接続でき、`get/put` が可能。

## 6. 全体アーキテクチャ（責務境界）

0.1.4-base-stable と同様：

- ホスト（RockyLinux 9.7）
  - ブリッジ：`br0`
  - TAP：`tap-umu`
  - QEMU/KVM：`/usr/libexec/qemu-kvm` を使用
  - ログ採取：`script` により QEMU コンソールをファイル保存
- ゲスト（UmuOS-0.1.5-dev）
  - カーネル：Linux 6.18.1
  - initramfs：起動初期化と `switch_root`
  - 永続 rootfs：ext4（`disk/disk.img`）
  - ユーザーランド：BusyBox + `/umu_bin`（自作コマンド）
  - リモートログイン：BusyBox `telnetd` → BusyBox `login`
  - FTP：BusyBox `tcpsvd` → BusyBox `ftpd`

## 7. 成果物 / ディレクトリ設計（固定）

生成物は `UmuOS-0.1.5-dev/` 配下に閉じる。

- `kernel/`：Linux 6.18.1 のビルド（`.config` とビルド成果物）
- `initramfs/`：initramfs のソース、rootfs、生成物
- `disk/`：永続 ext4 ディスク（`disk.img`）
- `iso_root/`：ISO 生成素材（GRUB 設定含む）
- `run/`：起動 I/F（固定引数・固定ファイル名）
- `logs/`：ホストログ/観測メモ
- `docs/`：設計書

## 8. 外部インタフェース（I/F）

## 7.1 ホスト起動I/F

- `br0` が存在しない場合は中止（受入環境は `br0` 前提）。
- `tap-umu` は起動時に作成し、停止時に削除する。
- QEMU コンソールログは `logs/` 配下へ必ず保存する。

## 7.2 ゲスト設定I/F（永続 rootfs 側）

- `/etc/umu/network.conf`：固定IPとGW（0.1.4踏襲）
- `/etc/resolv.conf`：DNS（0.1.5で固定）
- `/etc/TZ`：JST（0.1.5で固定）
- `/etc/securetty`：root ログイン制御（telnetの root 失敗を最優先切り分け点として維持）

### 8.3 rcS 責務境界（0.1.5で拡張）

rcS は `switch_root` 後（ext4 rootfs 側）で実行される初期化スクリプト。

0.1.5 の rcS は以下を責務とする：

- `/proc`/`/sys`/`/dev`/`/dev/pts` の mount（0.1.4踏襲）
- `/logs/boot.log` 追記（0.1.4踏襲）
- ネットワーク初期化（固定IP + default route、0.1.4踏襲）
- `PATH` の先頭を `/umu_bin` に固定し、`/umu_bin` を作成する
- `telnetd` 起動（0.1.4踏襲）
- FTP サービス起動（`tcpsvd` 常駐、PID を `/run/ftpd.pid` へ保存）
- （方針は詳細設計で固定）NTP 同期（`ntpd`）を起動時に実行できる設計にする
## 9. 実装方針（ハマりどころを設計に吸収）

- BusyBox は `olddefconfig` が無い。`.config` の反映は `make oldconfig` を使う。
- initrd filelist 生成（`find ... > filelist`）は無音が正常。
- `cpio` は `rootfs` をカレントにして実行する（ディレクトリ基準のミスを避ける）。
- telnet で root だけ失敗する場合は `/etc/securetty` を最優先切り分け。
- setuid を使う `su` は、`/` が `nosuid` だと成立しない（ゲスト側 `mount` で確認）。

## 10. 手順書

作業コマンドは詳細設計書に集約する：`docs/UmuOS-0.1.5-dev-詳細設計書.md`
