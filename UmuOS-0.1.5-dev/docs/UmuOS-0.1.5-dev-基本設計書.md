---
title: UmuOS-0.1.5-dev 基本設計書
date: 2026-02-09
base_design: "../../UmuOS-0.1.4-base-stable/docs/UmuOS-0.1.4-base-stable-基本設計書.md"
status: draft-for-review
---

# 1. 目的 / 位置づけ

UmuOS-0.1.5-dev は、UmuOS-0.1.4-base-stable（受入済み）を踏襲しつつ、ユーザーランド開発を進めるために必要な「開発に直結する最小機能」を OS 側へ統合する。

0.1.4 まで「手動で実装していた」追加機能（DNS、JST、NTP、`/umu_bin`、`ll`、自作 `su`、`ftpd`）を、**再現可能なビルド/設定手順**として固定化する。

重要方針（固定）：

- 0.1.4-base-stable の成立条件（`switch_root`、ttyS0/ttyS1ログイン、`/logs/boot.log`、telnetd、固定IP、nc転送）を壊さない。
- 追加機能は「最後に追記」ではなく、**起動フロー/責務境界に合わせて設計上自然な位置**へ組み込む。
- 追加機能の成立に必要な前提（DNS/外界疎通、setuid、ポート21等）を設計上の必須条件として明文化し、実装ノート（0.1.4）で判明したハマりどころを先に潰す。
- 本書および詳細設計書の手順では、`/home/tama/...` など **絶対パスを用いる**（`$HOME` や `~` など、環境依存の書き方をしない）。

# 2. スコープ

## 2.1 やること（固定）

- 0.1.4-base-stable 相当のベースを再現可能に構築する。
  - Linux kernel 6.18.1 + initramfs（`switch_root`）+ ext4 永続 rootfs（`disk.img`）+ BusyBox ユーザーランド
  - ttyS0/ttyS1 同時ログイン（`root`/`tama`）
  - `/logs/boot.log` 追記
  - ゲスト固定IP（`192.168.0.202/24`）+ default route（`192.168.0.1`）
  - BusyBox `telnetd` を TCP/23 で待受（`/bin/login`）
  - BusyBox `nc` によるファイル転送

- 0.1.5-dev の追加機能を成立させる。
  - DNS：`/etc/resolv.conf` を Google Public DNS に固定し、ホスト名解決を成立させる
  - タイムゾーン：JST を固定（`/etc/TZ` 方式）
  - 時刻同期：BusyBox `ntpd` により時刻同期を実施できる（手動/自動の方針は詳細設計で固定）
  - 自作コマンド置き場：`/umu_bin` を追加し、検索パス先頭へ固定する
  - `ll`：`/umu_bin/ll` として提供する
  - `su`：BusyBox `su` の setuid 問題に依存せず、最小の自作 `su` を `/umu_bin/su` として成立させる（rootパスワード必須）
  - FTP：BusyBox `tcpsvd` + BusyBox `ftpd` を用い、LAN 内でファイル転送を可能にする（平文なのでLAN内限定）

## 2.2 やらないこと（固定）

- SSH 導入
- インターネット越し運用（WAN 露出）
- 監視・自動再起動（systemd ユニット化等）
- 0.1.4-base-stable の受入条件に無関係な大規模改変（initramfs設計の全面変更など）

# 3. 前提環境 / 固定値

## 3.1 ネットワーク前提（固定IP）

- RockyLinux（ホスト）：`192.168.0.200`
- Ubuntu（開発）：`192.168.0.201`
- UmuOS（ゲスト）：`192.168.0.202/24`、GW `192.168.0.1`
- L2：Rocky 側にブリッジ `br0` が存在する。
- ゲストは TAP（`tap-umu`）を介して `br0` に接続し、LAN に L2 参加する。
- libvirt 管理下の `vnet*` は使用しない。

## 3.2 ツール/ソース（固定）

- Linux kernel：`6.18.1`（ソース：`/home/tama/umu/umu_project/external/linux-6.18.1-kernel/`）
- BusyBox：`1.36.1`（ソース：`/home/tama/umu/umu_project/external/busybox-1.36.1/`）

# 4. 要件 / 受入基準

## 4.1 0.1.4 相当（維持、固定）

- `switch_root` が成立する。
- ttyS0 で `root`/`tama` のパスワードログインが成立する。
- ttyS1（QEMU TCP シリアル）でも同時ログインが成立する。
- ゲスト側 `/logs/boot.log` が追記される。
- ゲスト `eth0` に `192.168.0.202/24` が設定される。
- デフォルトルート `default via 192.168.0.1` が設定される。
- LAN 端末から `192.168.0.202:23` に接続し、`root` と `tama` のログインが成功する。
- Ubuntu から `nc` でゲストへファイル転送が成功する。

## 4.2 0.1.5 追加（固定）

DNS：

- `ping -c 1 8.8.8.8` が成功する（L3 到達性）
- `ping -c 1 google.com` が成功する（DNS 解決）
- BusyBox `wget` を使える場合は `wget -O - http://example.com` が成功する（外界疎通）

タイムゾーン/時刻：

- `date` 表示が JST（UTC+9）として扱われる（`/etc/TZ` により固定）
- `ntpd` により時刻同期が実施できる（手動で実行して `date` が更新される）

`/umu_bin`：

- `/umu_bin` が `root:root` で、書き込みは root のみに制限される（`0755`）
- 検索パスは `/umu_bin` が先頭になる（`/umu_bin` 上のコマンドが優先される）

`ll`：

- `ll /` が `ls -lF /` 相当として動作する。

`su`：

- 一般ユーザー（例：`tama`）でログイン後、`/umu_bin/su` 実行で root へ切替できる。
- root への切替はパスワード必須で、`/etc/shadow` の root ハッシュで検証される。

FTP：

- BusyBox `tcpsvd` を常駐させ、接続ごとに BusyBox `ftpd` が起動する。
- LAN 内の Ubuntu から FTP 接続し、`binary` モードで `get/put` ができる。

# 5. 全体アーキテクチャ

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

# 6. 成果物 / ディレクトリ設計（固定）

生成物は `UmuOS-0.1.5-dev/` 配下に閉じる。

- `kernel/`：Linux 6.18.1 のビルド（`.config` とビルド成果物）
- `initramfs/`：initramfs のソース、rootfs、生成物
- `disk/`：永続 ext4 ディスク（`disk.img`）
- `iso_root/`：ISO 生成素材（GRUB 設定含む）
- `run/`：起動 I/F（固定引数・固定ファイル名）
- `logs/`：ホストログ/観測メモ
- `docs/`：設計書

# 7. 外部インタフェース（I/F）

## 7.1 ホスト起動I/F

- `br0` が存在しない場合は中止（受入環境は `br0` 前提）。
- `tap-umu` は起動時に作成し、停止時に削除する。
- QEMU コンソールログは `logs/` 配下へ必ず保存する。

## 7.2 ゲスト設定I/F（永続 rootfs 側）

- `/etc/umu/network.conf`：固定IPとGW（0.1.4踏襲）
- `/etc/resolv.conf`：DNS（0.1.5で固定）
- `/etc/TZ`：JST（0.1.5で固定）
- `/etc/securetty`：root ログイン制御（telnetの root 失敗を最優先切り分け点として維持）

## 7.3 rcS 責務境界（0.1.5で拡張）

rcS は `switch_root` 後（ext4 rootfs 側）で実行される初期化スクリプト。

0.1.5 の rcS は以下を責務とする：

- `/proc`/`/sys`/`/dev`/`/dev/pts` の mount（0.1.4踏襲）
- `/logs/boot.log` 追記（0.1.4踏襲）
- ネットワーク初期化（固定IP + default route、0.1.4踏襲）
- `PATH` の先頭を `/umu_bin` に固定し、`/umu_bin` を作成する
- `telnetd` 起動（0.1.4踏襲）
- FTP サービス起動（`tcpsvd` 常駐、PID を `/run/ftpd.pid` へ保存）
- （方針は詳細設計で固定）NTP 同期（`ntpd`）を起動時に実行できる設計にする

# 8. ハマりどころ（0.1.4 実装ノートからの設計反映）

- BusyBox は kernel のような `olddefconfig` が無い。整合は `make oldconfig`。
- initrd filelist の生成（`find ... > filelist`）は **無音が正常**。途中で中断した場合は作り直す。
- `cpio` は `rootfs` をカレントにして実行する（ディレクトリ基準を誤ると `stat` エラーになりやすい）。
- telnet で root だけ失敗する場合は `/etc/securetty` を最優先。
- setuid を使う `su` は、`/` が `nosuid` でマウントされていると成立しない。`mount` で切り分ける。
- BusyBox の `tc` は環境によりコンパイルエラーになりやすく、必須でないなら無効化して通す。

# 9. 詳細設計で確定する項目

- Linux 6.18.1 の kernel `.config`（`virtio-net` を built-in にする等）
- BusyBox 1.36.1 の `.config`（0.1.4必須 applet + 0.1.5追加 applet：`ntpd`/`tcpsvd`/`ftpd`/`wget`/`nslookup` 等）
- rcS の具体コマンド列（失敗時の扱い、NTP を自動で走らせるか、FTP を 21/2121 のどちらに固定するか）

確定場所：`docs/UmuOS-0.1.5-dev-詳細設計書.md`
