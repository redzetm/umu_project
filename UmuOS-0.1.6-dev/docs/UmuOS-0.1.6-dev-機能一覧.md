---
title: UmuOS-0.1.6-dev 機能一覧
date: 2026-02-15
status: accepted
related_docs:
  - "./UmuOS-0.1.6-dev-基本設計書.md"
  - "./UmuOS-0.1.6-dev-詳細設計書.md"
  - "./UmuOS-0.1.6-dev-実装ノート.md"
---

# UmuOS-0.1.6-dev 機能一覧

この文書は UmuOS-0.1.6-dev の「何ができるか（できないか）」を、基本設計書/詳細設計書/実装ノートの内容に準拠して一覧化する。


## 0. 前提（固定値）

- Kernel: `6.18.1`
- BusyBox: `1.36.1`
- ISO: `UmuOS-0.1.6-boot.iso`
- kernel (ISO内): `vmlinuz-6.18.1`
- initramfs (ISO内): `initrd.img-6.18.1`
- 永続ディスク: `disk/disk.img`（ext4, 4GiB, UUID固定）
  - rootfs UUID: `d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15`
- ゲストIP（static）: `192.168.0.202/24`
  - GW: `192.168.0.1`
  - DNS: `8.8.8.8`, `8.8.4.4`
- タイムゾーン: `JST-9`
- NTPサーバ: `time.google.com`
- デバッグ用シリアル: `ttyS1` をホスト `127.0.0.1:5555` へ TCP 転送
- ネットワーク（ホスト側）: `br0` + `tap-umu`（または `NET_MODE=none`）

## 1. 成果物（3点セット）

- `UmuOS-0.1.6-boot.iso`
  - `grub.cfg` + `vmlinuz-6.18.1` + `initrd.img-6.18.1`
- `disk.img`
  - Ubuntuで作った `disk/disk.img` を Rocky 側へ `disk.img` として配置する
- `UmuOS-0.1.6-dev_start.sh`
  - 実行環境の `/root` に上記3点だけ置けば起動できる（ネット有無は環境変数で切替）※/rootでなくても任意のディレクトリでOK

## 2. 起動・ブート機能

- ブート方式
  - GRUB → Linux kernel → initramfs → ext4(rootfs) へ `switch_root`
  - kernel cmdline で `root=UUID=...` を指定して rootfs を確定する
- コンソール
  - `ttyS0`: 主コンソール（QEMU `-serial stdio`）
  - `ttyS1`: デバッグ用の追加コンソール（ゲスト `getty`、ホストへ TCP 転送）
- ネットワーク起動モード（ホスト起動スクリプト）
  - `NET_MODE=tap`（デフォルト）: TAP を作成し bridge に接続して起動
  - `NET_MODE=none`: ネットワーク無しで起動（切り分け用）

## 3. initramfs 機能

- 役割
  - rootfs（ext4 disk.img）をマウントし、`switch_root` でユーザーランドへ遷移する
- BusyBox（静的リンク）
  - initramfs 内で BusyBox を使う（applet は `--install -s` による symlink 展開）

## 4. 永続 rootfs（disk.img）ユーザーランド機能

### 4.1 init / rcS

- init の構成
  - `/sbin/init` は BusyBox（symlink）
  - `/etc/inittab` により `::sysinit:/etc/init.d/rcS`
  - `ttyS0` / `ttyS1` で `getty` を respawn
- `rcS` が行う初期化
  - `proc/sys/devtmpfs/devpts` のマウント
  - `/logs` `/run` `/var/run` `/umu_bin` 作成、`/var/run/utmp` 初期化
  - `PATH=/umu_bin:/sbin:/bin` と `TZ=JST-9` の固定（`/etc/profile` にも反映）

### 4.2 起動ログ（永続）

- `/logs/boot.log` へ追記（起動のたびに増える）
  - `boot_id`, `time`, `uptime`, `cmdline`, `mount`
  - NTP同期: `[ntp_sync] before/after` と `ntp_sync` の出力

### 4.3 ネットワーク（ゲスト）

- インターフェース
  - 設定ファイル: `/etc/umu/network.conf`
  - キー: `IFNAME`, `MODE`, `IP`, `GW`（`MODE=static` で有効）
- 動作
  - `ip link set up` / `ip addr add` / `ip route replace default`
  - DNS: `/etc/resolv.conf` を利用

### 4.4 時刻（JST + NTP）

- `TZ=JST-9` を固定
- 起動時に1回だけ `ntp_sync` を実行
  - 疎通確認 `ping -c 1 8.8.8.8`
  - NTP: BusyBox `ntpd`（`-n -q` で一回同期→失敗時は常駐で再試行）

### 4.5 リモート機能

- telnet ログイン
  - `telnetd -p 23 -l /bin/login`
  - ユーザー: `root` / `tama`（`/etc/passwd` + `/etc/shadow`）
- FTP サーバ
  - BusyBox `tcpsvd` + `ftpd`
  - 公開ルート: `/`（全ディレクトリが見える。実アクセスは権限に従う）
  - PID ファイル: `/run/ftpd.pid`

### 4.6 ユーティリティ

- `/umu_bin` を PATH 最優先にする
- `ll`
  - `/umu_bin/ll` は `ls -l` の薄いラッパ
- `su`（自作）
  - `/umu_bin/su`（静的リンク、SUID root `4755`）
  - `/etc/shadow` の root 行の `$6$...` を `crypt(3)` で検証し、成功したら `setuid(0)` して `/bin/sh`

## 5. 観測点（成功判定に直結）

- `mount` で `/dev/vda on / type ext4` が確認できる（virtio-blk + 永続 rootfs）
- `/logs/boot.log` に以下が追記される
  - `boot_id`
  - `[ntp_sync] before/after`
- ログイン後、`echo "$PATH"` の先頭が `/umu_bin`
- `date` が `JST` 表示で出る
- FTP/telnet が起動している（例: `/run/ftpd.pid`、`telnetd` プロセス）

## 6. 非目的（この版では扱わない）

- systemd 前提のユーザーランド互換
- パッケージ管理、堅牢なセキュリティ設計
- 複雑なネットワーク（VLAN/iptables 等）

## 7. 受け入れ

- 詳細設計書の手順を Tera Term 経由で実施し、想定テストを完走したため受け入れ合格（2026-02-15）
