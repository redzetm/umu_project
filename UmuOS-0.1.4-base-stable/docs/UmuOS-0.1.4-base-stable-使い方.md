---
title: UmuOS-0.1.4-base-stable 使い方
date: 2026-01-25
related_docs:
  - "./UmuOS-0.1.4-base-stable-基本設計書.md"
  - "./UmuOS-0.1.4-base-stable-詳細設計書.md"
  - "./UmuOS-0.1.4-base-stable-実装ノート.md"
status: draft
---

# 0. このドキュメントの目的

UmuOS-0.1.4-base-stable を「起動して使う（観測して切り分ける）」ための手順と前提をまとめる。

- ① 環境のこと（ビルド環境 / 起動環境 / ネットワーク前提）
- ② どこまで動作確認しているか（2026-01-25時点）
- ③ 0.1.3にtelnet機能が付いただけ（差分の説明）
- ④ telnet対応なので、ネットワーク上のサーバーで実行する必要がある（運用前提）
- ⑤ 各スクリプト、成果物の説明（何が何をするか）


# 1. 位置づけ（0.1.3との差分）

UmuOS-0.1.4-base-stable は、UmuOS 0.1.3 系の最小OSを維持しつつ、LAN 内端末からゲストへログインできるように **BusyBox の telnetd（TCP/23）** を追加しただけの「ベース安定版」である。

重要：このリポジトリ文脈では “telnet” が2種類ある。

- **ttyS1のtelnet（ホストローカル用途）**：QEMU の TCP シリアル（`127.0.0.1:5555`）に “telnet” で接続して、ゲストの ttyS1 を操作するもの。`connect_ttyS1.sh` がこれ。
- **telnetd（LANログイン用途）**：ゲスト側で `telnetd -p 23 -l /bin/login` を起動し、LAN から `192.168.0.202:23` に接続してログインするもの。

0.1.4-base-stable は **両方を同時に成立** させる前提で構成している。


# 2. 環境のこと（①）

## 2.1 推奨する役割分担

- **Ubuntu 24.04**：ビルド（kernel / busybox / initramfs / ISO / disk.img作成）
- **RockyLinux 9.7**：起動・受入（QEMU/KVM + br0 + tap でLANに接続）

理由：telnetd（TCP/23）で LAN から接続するため、ブリッジを張れる “常時稼働のサーバー” 側でゲストを動かす運用が安定する。


## 2.2 ビルド環境（Ubuntu側の例）

必要になるコマンド群（実装ノートの例）：

- ビルド系：`make`, `gcc`, `ld`, `bc`, `bison`, `flex`
- kernel：`libssl-dev`, `libelf-dev`, `libncurses-dev`, `dwarves` など
- ISO：`grub-mkrescue`, `xorriso`, `mtools`
- initramfs：`cpio`, `gzip`, `xz`, `musl-gcc`
- disk：`mkfs.ext4`, `tune2fs`, `blkid`
- 疎通確認：`telnet`, `nc`


## 2.3 起動環境（Rocky側の例）

- QEMU/KVM（`/dev/kvm` が使えること）
- TAP作成（`/dev/net/tun` があること）
- ブリッジ：`br0` が存在すること
- 依存コマンド：`ip`, `script`, `telnet`（ホストローカルの ttyS1 接続に使用）


## 2.4 ネットワーク前提（固定値）

基本設計で固定している前提値：

- Rocky（ホスト）：`192.168.0.200`
- Ubuntu（開発）：`192.168.0.201`
- UmuOS（ゲスト）：`192.168.0.202/24`、GW `192.168.0.1`

ゲストは TAP を介して `br0` に接続し、LAN に L2 参加する。


# 3. どこまで動作確認しているか（②）

2026-01-25 時点で、少なくとも以下は「再現手順として成立する状態」になっている（詳細は実装ノート参照）。

## 3.1 確認済み（手順・成果物生成の確認）

- kernel（6.18.1）のビルドが完走する
- BusyBox（1.36.1）のビルドが完走し、`ip/login/nc/telnetd` applet が含まれることを確認
- initramfs の rootfs 生成、`initrd.img-6.18.1` の生成と `iso_root/` への配置
- 永続ディスク `disk/disk.img` の作成と最小rootfs投入
- ゲスト側設定ファイル（例：`/etc/inittab`, `/etc/init.d/rcS`, `/etc/umu/network.conf`, `/etc/securetty`, `/etc/passwd`, `/etc/shadow`）の投入手順が固定

## 3.2 これから埋める（受入・疎通の実機確認）

次が実機（Rocky上のQEMU）での受入項目になる。

- `switch_root` 成立（ttyS0ログで確認）
- ttyS0/ttyS1 でのログイン
- ゲスト `eth0` へ `192.168.0.202/24` 設定、default route 設定
- LAN から `192.168.0.202:23` の telnet ログイン（root/tama）
- Ubuntu から `nc` によるファイル転送


# 4. telnet対応なのでサーバーで動かす（④）

この版は「LAN内からゲストへ入る」ために telnetd（TCP/23）を使う。

そのため、次の条件を満たせる “ネットワーク上のサーバー（例：Rocky）” での実行が前提になる。

- `br0` を作って物理NICにブリッジできる
- `tap` を作成できる（CAP_NET_ADMIN）
- LAN からゲストIP `192.168.0.202` へ到達できる
- （必要に応じて）ホストの firewall/SELinux で TCP/23 を遮断していない

注意：`./umuOSstart.sh --net none` は起動切り分け用であり、telnetd のLAN受入は成立しない。


# 5. 起動手順（最小）

## 5.1 事前準備（Rocky）

- `br0` が存在することを確認する
- 依存スクリプトに実行権を付ける（初回のみ）
- 必要なら TAP を作成する

例：

1) TAP作成（推奨：明示的に作る）

- `sudo ./run/tap_up.sh tap-umu`

2) 起動（デフォルトは `--net tap`）

- `sudo ./umuOSstart.sh`

3) 別ターミナルで ttyS1 に接続（任意）

- `./connect_ttyS1.sh 5555`


## 5.2 ネット無しで起動だけしたい場合

- `./umuOSstart.sh --net none`

br0 が無い環境や、ネットワーク切り分け時に使う。


# 6. 各スクリプトの説明（⑤）

## 6.1 起動I/F

- `umuOSstart.sh`
  - QEMU を起動するエントリポイント。
  - `UmuOS-0.1.4-boot.iso` と `disk/disk.img` を使ってブートする。
  - ttyS0 は標準入出力へ、ttyS1 は `127.0.0.1:${TTYS1_PORT}`（デフォルト5555）で TCP シリアル公開する。
  - `--net tap|none`（または `NET_MODE=tap|none`）でネットワークを切替。
  - ホスト側ログを `logs/host_qemu.console_*.log` に保存する。

- `connect_ttyS1.sh [PORT]`
  - QEMU の TCP シリアル（ttyS1）へ `telnet 127.0.0.1 PORT` で接続する。
  - 出力は `logs/ttyS1_*.log` に保存する。
  - 接続後に何も出ない場合は Enter を数回押す。


## 6.2 ネットワーク補助

- `run/tap_up.sh [TAP_IF]`
  - `br0` にぶら下げる TAP（デフォルト `tap-umu`）を作成して有効化する。
  - root権限が必要。
  - 環境変数：
    - `BRIDGE`（デフォルト `br0`）
    - `OWNER`（TAPの所有ユーザー。`sudo` 実行時は `SUDO_USER` が暗黙に使われる）

- `run/tap_down.sh [TAP_IF]`
  - TAP を削除するクリーンアップ。
  - root権限が必要。


## 6.3 補助ファイル

- `run/qemu.cmdline.txt`
  - 実行した QEMU のコマンドラインを控える用途（運用メモ）。
  - 実際の起動は `umuOSstart.sh` が行う。


# 7. 成果物の説明（⑤）

- `UmuOS-0.1.4-boot.iso`
  - 起動用ISO。`iso_root/` を元に生成した成果物。

- `disk/disk.img`
  - 永続 rootfs（ext4）。ユーザー、初期化スクリプト、telnetd起動設定などが入る。

- `iso_root/boot/`
  - ISO の素材（kernel / initrd / grub設定など）。

- `kernel/build/`
  - out-of-tree カーネルビルド成果物。

- `initramfs/`
  - initramfs の素材と生成物。
  - `initrd.img-6.18.1` が実際にブートで使われる initrd。

- `logs/`
  - ホスト側で採取したログの保存先。
  - 起動すると `logs/host_qemu.console_*.log` と `logs/ttyS1_*.log` が増える。


# 8. 典型的な詰まりどころ

- `br0` が無い：`./umuOSstart.sh --net none` で起動だけ切り分け、Rocky側のブリッジ構成を見直す。
- `tap-umu` が作れない：`sudo` で実行しているか、`/dev/net/tun` があるか確認する。
- `5555` が使用中：別のQEMUが残っている可能性。`TTYS1_PORT` を変えるか、既存プロセスを停止する。
- LANから `192.168.0.202:23` に繋がらない：ホストの firewall/SELinux、br0 への接続、ゲストのIP設定（`/etc/umu/network.conf`）を疑う。
