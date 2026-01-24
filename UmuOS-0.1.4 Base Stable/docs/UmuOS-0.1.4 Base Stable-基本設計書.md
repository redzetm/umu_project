---
title: UmuOS-0.1.4 Base Stable 基本設計書
date: 2026-01-24
base_plan: ../../思考TEMP/0001_telnet機能実装の計画.md
status: draft-for-review
---

# 1. 目的 / 位置づけ

UmuOS-0.1.4 Base Stable は、UmuOS のユーザーランド開発へ進むためのベースOSである。
本バージョンは BusyBox `telnetd` を追加し、LAN 内端末からゲストへ telnet ログインできる状態を成立させる。

重要方針（固定）：

- 既存の UmuOS-0.1.3 を含む 0.1.x の成果物は参照のみとし、変更しない。
- UmuOS-0.1.4 Base Stable は「最初から再現できる」手順を固定して構築する。
- 0.1.3 文脈の「telnet（ttyS1 を TCP シリアル公開）」と、今回の「telnetd（TCP/23）」は別物として扱い、同時成立させる。

# 2. スコープ

## 2.1 やること（固定）

- RockyLinux 9.7 上で QEMU を CLI 起動し、常時稼働させる（virt-manager を使わない）。
- ゲスト（ext4 rootfs）で固定IPの最小ネットワーク初期化を行う。
- BusyBox `telnetd` を起動し、`/bin/login` 経由で `root` と `tama` がログインできる。
- Ubuntu からゲストへ `nc` でファイル転送できる（手順とポートを固定）。

## 2.2 やらないこと（固定）

- SSH 導入
- インターネット越し運用
- `su` の成立
- virt-manager の再現性問題（No bootable device 等）の調査

# 3. 前提環境 / 固定値

## 3.1 ネットワーク前提（固定IP）

- RockyLinux（ホスト）：`192.168.0.200`
- Ubuntu（開発）：`192.168.0.201`
- UmuOS（ゲスト）：`192.168.0.202/24`、GW `192.168.0.1`
- L2：Rocky 側にブリッジ `br0` が存在する。
- ゲストは TAP を介して `br0` に接続し、LAN に L2 参加する。
- UmuOS 用に手動作成する TAP 名は `tap-umu` に固定する。
- libvirt 管理下の `vnet*` は使用しない。

## 3.2 運用前提（固定）

- Rocky 側は GUI で root ログインして作業する（コマンド例は `sudo` 無し）。
- 常時稼働は `tmux` 内で行う（systemd ユニット化は対象外）。
- ホスト側の QEMU コンソールログは `script` で必ず採取する。

## 3.3 ソース / ツールの固定（0.1.4 Base Stable）

- Linux kernel：`6.18.1`（ソース基準：`external/linux-6.18.1-kernel/`）
- BusyBox：`1.36.1`（ソース基準：`external/busybox-1.36.1/`）
- ゲスト側ユーザーランドは BusyBox の `telnetd` / `login` / `nc` / `ip` を使用する（`iproute2` は前提にしない）。

# 4. 要件 / 受入基準

## 4.1 0.1.3 相当の既存機能（維持、固定）

- `switch_root` が成立する。
- ttyS0 で `root`/`tama` のパスワードログインが成立する。
- ttyS1（QEMU TCP シリアル）でも同時ログインが成立する。
- ゲスト側 `/logs/boot.log` が追記される。

## 4.2 追加機能（今回、固定）

- ゲスト `eth0` に `192.168.0.202/24` が設定される。
- デフォルトルート `default via 192.168.0.1` が設定される。
- ローカルPC（TeraTerm）から `192.168.0.202:23` に接続し `root` ログインが成功する。
- 同様に `tama` ログインが成功する。
- Ubuntu から `nc` でゲストへファイル転送が成功する。

# 5. 全体アーキテクチャ

## 5.1 構成要素

- ホスト（RockyLinux 9.7）
  - ブリッジ：`br0`
  - TAP：`tap-umu`（手動作成）
  - QEMU/KVM：`-enable-kvm` を使用
  - ログ採取：`script` により QEMU コンソールをファイル保存
- ゲスト（UmuOS-0.1.4 Base Stable）
  - カーネル：Linux 6.18.1
  - initramfs：起動初期化と `switch_root`
  - 永続 rootfs：ext4（`disk/disk.img`）
  - ユーザーランド：BusyBox
  - リモートログイン：BusyBox `telnetd` → BusyBox `login`

## 5.2 データフロー / 接続

- L2: `tap-umu` を `br0` に接続 → QEMU の `-netdev tap` でゲストへ渡す → ゲスト `eth0` として見える。
- ローカルPC、およびUbuntu から `192.168.0.202:23` へ TCP 接続し、telnet ログインする。
- 0.1.3 互換の ttyS1 は `-serial tcp:127.0.0.1:5555,server,nowait,telnet` でホストローカル用途として維持する。

# 6. 成果物 / ディレクトリ設計（固定）

生成物（ビルド出力と運用に必要な成果物）は `UmuOS-0.1.4 Base Stable/` 配下に閉じる。
ソース（入力）は `external/` 配下を正とし、参照のみとする。

ソースの正（固定）：

- `external/linux-6.18.1-kernel/`
- `external/busybox-1.36.1/`

- `kernel/`：Linux 6.18.1 のビルド（`.config` とビルド手順、成果物）
- `initramfs/`：initramfs のソース、rootfs、生成物（`initrd.cpio` 等）
- `disk/`：永続 ext4 ディスク（`disk.img`）
- `iso_root/`：ISO 生成素材（GRUB 設定含む）
- `run/`：起動 I/F（固定引数・固定ファイル名・環境変数の定義）
- `logs/`：ホストログ（QEMU コンソール）と、ゲストログの観測メモ
- `docs/`：基本設計書と詳細設計書

# 7. 外部インタフェース（I/F）

## 7.1 ホスト起動I/F（固定）

- 事前条件：`br0` が存在しない場合は中止。
- `tap-umu` は起動時に作成し、停止時に削除する。
- QEMU は `-enable-kvm` を必須とし、使えない場合は中止。
- QEMU コンソールログは `logs/host_qemu.console.log` に保存する。

## 7.2 ゲスト設定I/F（固定）

ext4 rootfs 側に以下の設定ファイルを置く。

- `/etc/umu/network.conf`

固定キー（必須）：

- `IFNAME`（固定値：`eth0`）
- `MODE`（固定値：`static`）
- `IP`（固定値：`192.168.0.202/24`）
- `GW`（固定値：`192.168.0.1`）
- `DNS`（固定値：`192.168.0.1`）

ネットワーク初期化は BusyBox `ip` で実装し、`ifconfig`/`route` への分岐は作らない。

## 7.3 認証I/F（固定）

- telnet は BusyBox `telnetd` が TCP/23 で待ち受ける。
- `telnetd` は `-l /bin/login` でログイン処理を行う。
- `root` ログインと `tama` ログインの両方を必須とする。

## 7.4 `/etc/securetty`（固定）

- ext4 rootfs 側に `/etc/securetty` を作成する。
- `ttyS0`、`ttyS1`、`pts/0`〜`pts/9` を許可する（同時接続上限は 10 に固定）。

# 8. 運用設計（固定）

- ホスト側は `tmux` 内で QEMU を稼働させ、セッションで保持する。
- 退出・異常終了を含め、停止後は必ず `tap-umu` を down→del して状態をクリーンに戻す。

# 9. 観測点（観測コマンドの位置づけ）

## 9.1 ホスト（Rocky）観測点

- `br0` の存在と、物理NICが参加していること
- `br0` が `192.168.0.200/24` を持つこと
- `tap-umu` が `br0` の master になっていること

## 9.2 ゲスト（UmuOS）観測点

- `ip addr show dev eth0` で `192.168.0.202/24` が付与されていること
- `ip route` に `default via 192.168.0.1` が存在すること
- telnet 接続で `login:` が出ること
- `root`/`tama` のログインが成立すること

# 10. ファイル転送（nc）設計（固定）

- フローは「ゲストが受信、Ubuntu が送信」に固定する。
- ポートは `12345` に固定する。

手順：

- ゲスト側：`/tmp/in` を作成し、`nc -l -p 12345 > payload.bin` で受信する。
- Ubuntu 側：`nc 192.168.0.202 12345 < payload.bin` で送信する。

# 11. トラブルシュート（切り分け順、固定）

1. ttyS0 でログインできるか（成立しない場合、telnet 以前の問題）
2. ゲストに IP/route が入っているか（成立しない場合、rcS のネットワーク初期化）
3. telnetd が起動しているか（成立しない場合、rcS の telnetd 起動）
4. LAN 到達性（ローカルPC/Ubuntu → 192.168.0.202）
5. `login`/`shadow`/`securetty`（`root` だけ失敗する場合は securetty を最優先）

# 12. セキュリティ（固定）

- telnet は平文通信である。
- 運用は自宅 LAN 内に限定する。
- SSH への置換は本バージョンの対象外とする。

# 13. 運用設計（RockyLinux 9.7 /home/tama 配置、固定）

## 13.1 配置（固定）

配置ルート（固定）：`/home/tama/UmuOS-0.1.4 Base Stable/`

RockyLinux 9.7 側では、成果物一式を `/home/tama/UmuOS-0.1.4 Base Stable/` に配置し、このディレクトリをカレントにして起動する。

ディレクトリ構造（固定）：

- `/home/tama/UmuOS-0.1.4 Base Stable/`
  - `disk/disk.img`
  - `UmuOS-0.1.4-boot.iso`
  - `logs/host_qemu.console.log`
  - `logs/host_qemu.console.log.prev`
  - `run/qemu.cmdline.txt`

## 13.2 起動（固定）

常時稼働は `tmux` 内で行い、1 セッション = 1 ゲスト稼働とする。

起動手順（固定）：

1. `tmux` セッションを作成し、作業ディレクトリを `/home/tama/UmuOS-0.1.4 Base Stable/` にする。
2. 起動前クリーンアップを実行する（前回の異常終了で `tap-umu` が残っている場合に備える）。
  - `ip link set dev tap-umu down || true`
  - `ip link del dev tap-umu || true`
3. `tap-umu` を作成し、`br0` に接続して UP にする。
  - `ip tuntap add dev tap-umu mode tap user "$USER"`
  - `ip link set dev tap-umu master br0`
  - `ip link set dev tap-umu up`
4. 起動ログの世代を固定する。
  - `logs/host_qemu.console.log` が存在する場合、`logs/host_qemu.console.log.prev` に上書き移動する。
5. `script` により QEMU を起動し、コンソールログを `logs/host_qemu.console.log` に保存する。
  - QEMU 引数は 7.1 のホスト起動I/Fに従う。
  - QEMU 引数の完成形は `run/qemu.cmdline.txt` に固定する。

## 13.3 停止（固定）

停止手順（固定）：

1. QEMU を正常終了させる（終了操作は QEMU の標準操作に従う）。
2. 停止後クリーンアップを必ず実行する。
  - `ip link set dev tap-umu down || true`
  - `ip link del dev tap-umu || true`

## 13.4 異常終了時（固定）

QEMU の異常終了や回線断が起きた場合は、直後に 13.3 の停止後クリーンアップ（`tap-umu` down→del）を実行し、次回起動に影響を残さない。

## 13.5 ログ運用（固定）

- ホスト側：毎回の起動で `logs/host_qemu.console.log` を生成する。
- 直前回のログは `logs/host_qemu.console.log.prev` に保持する。
- ゲスト側：`/logs/boot.log` の追記が継続することを受入基準とする。

# 14. 再現可能ビルド定義（Input / Process / Output、固定）

## 14.1 Input（固定）

ビルド実行環境（固定）：Ubuntu 24.04 LTS

ソースの正（固定、参照のみ）：

- kernel：`external/linux-6.18.1-kernel/`
- BusyBox：`external/busybox-1.36.1/`

0.1.4 側で固定する入力（必須）：

- kernel `.config`
- BusyBox `.config`
- initramfs / rootfs の構造と設定（例：`/etc/umu/network.conf`）
- `iso_root/`（GRUB 設定を含む ISO 生成素材）

## 14.2 Process（固定）

Ubuntu 上で kernel / BusyBox / initramfs / ext4 / ISO をビルドし、成果物を RockyLinux 9.7 へコピーして起動する。
Rocky 側はビルドを行わず、起動と運用のみを行う。

## 14.3 Output（固定）

Rocky 側の配置ルート（固定）：`/home/tama/UmuOS-0.1.4 Base Stable/`

起動に必要な成果物（固定名）：

- ISO：`UmuOS-0.1.4-boot.iso`
- 永続ディスク：`disk/disk.img`

起動I/F定義（固定名）：

- `run/qemu.cmdline.txt`

ホスト側ログ（固定名）：

- `logs/host_qemu.console.log`
- `logs/host_qemu.console.log.prev`

# 15. 詳細設計で確定する項目

基本設計では要件（必須/禁止、成立条件）までを固定し、具体的な `.config` の確定は詳細設計で行う。

- Linux 6.18.1 の kernel `.config`（具体的な `CONFIG_...` 一覧の確定）
- BusyBox 1.36.1 の `.config`（具体的な `CONFIG_...` 一覧の確定）

確定場所：`docs/UmuOS-0.1.4 Base Stable-詳細設計書.md`

# 16. Kernel `.config` 必須要件（基本設計で確定済み）

0.1.4 の kernel は、0.1.3 相当の起動要件（initramfs → `switch_root`、ext4、virtio-blk、devtmpfs 等）を維持した上で、telnet 運用の前提となるネットワーク機能を成立させる。

確定事項（固定）：

- ゲスト NIC は `virtio-net` を前提とし、kernel 側で必須要件として有効化する。
- `virtio-net` は module ではなく built-in とする（initramfs 側に `.ko` 配置/ロード要件を持ち込まない）。

残タスク（未確定）：

- 上記を満たす Linux 6.18.1 の `CONFIG_...` 一覧を確定し、0.1.4 側の kernel `.config` を固定する。

## 16.1 必須 `CONFIG_...`（固定：すべて `=y`）

起動/ユーザーランド土台：

- `CONFIG_DEVTMPFS=y`
- `CONFIG_DEVTMPFS_MOUNT=y`
- `CONFIG_TMPFS=y`
- `CONFIG_PROC_FS=y`
- `CONFIG_SYSFS=y`

initramfs / switch_root / 永続 rootfs：

- `CONFIG_BLK_DEV_INITRD=y`
- `CONFIG_EXT4_FS=y`

virtio：

- `CONFIG_VIRTIO=y`
- `CONFIG_VIRTIO_PCI=y`
- `CONFIG_VIRTIO_BLK=y`
- `CONFIG_VIRTIO_NET=y`

IPv4（telnetの前提）：

- `CONFIG_NET=y`
- `CONFIG_INET=y`
- `CONFIG_NETDEVICES=y`
- `CONFIG_ETHERNET=y`

telnetログインの前提（pts/pty）：

- `CONFIG_UNIX98_PTYS=y`

## 16.2 禁止 `CONFIG_...`（固定：すべて `is not set`）

- `# CONFIG_IPV6 is not set`
- `# CONFIG_IP_PNP is not set`

# 17. BusyBox `.config` 必須要件（基本設計で確定済み）

0.1.4 のユーザーランドは BusyBox を中核として構成し、telnet 運用のためのアプレットを含める。

確定事項（固定）：

- BusyBox は static link とする（動的リンクは許可しない）。
- 0.1.4 は BusyBox の以下アプレットを必須とする：`telnetd` / `login` / `nc` / `ip`
- BusyBox `login` は `/etc/securetty` による root ログイン制御を有効とする。
- 認証方式は `/etc/shadow` を使用する（shadow 方式）。
- `telnetd` は inetd 経由にせず、rcS から standalone 起動する。
- `nc` は受信側（UmuOS）で `-l` と `-p` が使用でき、標準入出力のリダイレクトでファイル受信できることを必須とする。
- BusyBox `ip` は `ip link` / `ip addr` / `ip route` が動作することを必須とする。

観測/検査（固定）：

- 詳細設計で BusyBox をビルドした直後に、`busybox ip link` / `busybox ip addr` / `busybox ip route` を実行し、成立しない場合は BusyBox `.config` を修正して作り直す。

# 18. rcS 責務境界（基本設計で確定済み）

本書で言う rcS は、`switch_root` 後の永続 rootfs（ext4）側で実行される初期化スクリプトを指す。

確定事項（固定）：

- rcS の責務は「ネットワーク初期化」「telnetd 起動」「`/logs/boot.log` 追記」である。
- ネットワーク初期化と `telnetd` 起動は `switch_root` 後の ext4 rootfs 側 rcS で実施する。
- ネットワーク初期化は BusyBox `ip link/addr/route` を使用する。
- `telnetd` は rcS から standalone 起動し、`-l /bin/login` でログイン処理を行う。

処理順序（固定）：

1. `/logs/boot.log` を追記する（既存の 0.1.3 相当のログ杭を維持する）。
2. `/etc/umu/network.conf` を読み取り、固定IP（`192.168.0.202/24`）と default route（`192.168.0.1`）を設定する。
3. `telnetd` を起動し、TCP/23 待受を開始する。

詳細設計タスク（別紙で確定）：

- rcS の具体コマンド列、エラー処理（失敗時に続行/停止のどちらにするか）、観測ログの詳細を詳細設計で確定する。

# 19. run/ 起動I/F（基本設計で確定済み）

確定事項（固定）：

- Rocky 側の起動は、作業ディレクトリを `/home/tama/UmuOS-0.1.4 Base Stable/` とし、相対パスで実行する。
- QEMU 引数の完成形は `run/qemu.cmdline.txt` に固定する（定義ファイル方式）。

運用上の扱い（固定）：

- `run/qemu.cmdline.txt` は「1行=1コマンド」を表す（`\\` による継続行を許可する）。
- `run/qemu.cmdline.txt` は `script` によりログ採取しながら実行する（実行方法の具体コマンドは詳細設計で確定する）。

`CONFIG_...`（確定対象の候補、固定方針）：

- `CONFIG_STATIC=y`
- `CONFIG_TELNETD=y`
- `CONFIG_LOGIN=y`
- `CONFIG_NC=y`
- `CONFIG_IP=y`
- `CONFIG_FEATURE_SECURETTY=y`
- `CONFIG_FEATURE_SHADOWPASSWDS=y`

残タスク（未確定）：

- 上記を満たす BusyBox 1.36.1 の `CONFIG_...` 一覧を確定し、0.1.4 側の BusyBox `.config` を固定する。
