---
title: 0001 telnet機能（BusyBox telnetd）実装の計画
date: 2026-01-23
base: UmuOS-0.1.3
---

# UmuOS-0.1.4 Base Stable　の位置づけ

- telnetdを導入して、今後はユーザーランド開発に向けていくので、ベースOSとする
- 0.1.5以降は、0.1.4をベースに機能を追加していく


# 要件定義（確定）

- UmuOS-0.1.3 相当で成立していた機能は **全て使える前提**（成立条件＝ttyS0/ttyS1ログイン・/logs/boot.log 等は維持）
- LAN内のローカルPCから `telnet` を使い **TeraTerm でアクセス**できる
- `root` ログイン、`tama` ログインが **どちらもできる**
- virt-manager 起動は今回は扱わない（RockyLinux 9.7 上で QEMU をターミナル起動し **常時稼働**させる）
- 開発環境（Ubuntu）から `nc` を利用して UmuOS に **ファイル転送**できる


# 前提環境

- QEMU/KVMホスト：RockyLinux 9.7（固定：`192.168.0.200`）
	- 運用前提：Rocky 側は GUI で root ログインして作業する（本計画のコマンド例は `sudo` 無し）
		- root 以外で実行する場合は `sudo` を付与すること
- 開発環境：Ubuntu 24.04 LTS（固定：`192.168.0.201`、バイナリ作成・nc送信）
- ローカルPC（MiniPC）：TeraTerm 端末
- ゲスト（UmuOS）：固定IP `192.168.0.202/24`、GW `192.168.0.1`
- L2：RockyLinux 側にブリッジ `br0` が存在し、ゲストは TAP（Linux の仮想NIC）を介して `br0` に直結
  - TAP デバイス名は固定ではない（手動作成なら `tap0`/`tap-umu`、libvirt 管理下なら `vnet0` 等になり得る）
  - libvirt の `vnet*` は既存VMが使用中のことが多いため共有しない（混線・学習・フィルタ等で不安定化しやすい）
  - 本計画では UmuOS 用に TAP を 1 本追加し、それを `br0` に接続して QEMU（CLI）へ渡す（固定：`tap-umu`）
  - 手動で追加した TAP はホスト再起動で消えるため、UmuOS 起動手順（スクリプト等）で毎回作成する運用とする

ソースバージョン固定（UmuOS-0.1.4 Base Stable）：

- Linux kernel：`6.6.18`（ソース基準：`external/linux-6.6.18-kernel/`）
- BusyBox：`1.36.1`
	- 0.1.4 は BusyBox の `telnetd` / `login` / `nc` / `ip` を使用する（`iproute2` は前提にしない）


# 目的

UmuOS-0.1.4 Base Stable を「最初から再現できる」形で構築し（必要ソフトウェア準備→カーネルコンパイル→initramfs/ext4→ISO 作成まで）、その上で BusyBox `telnetd` を起動して同一セグメント上の端末（ローカルPC / Ubuntu）から `telnet 192.168.0.202:23` でログインできる状態を作る。

重要方針：

- 既存の UmuOS-0.1.3 / 0.1.x の成果物（既に合格している成果物）は変更しない（参照のみ）。
- UmuOS-0.1.4 Base Stable は「過去の0.1.xへ戻れる」ように、最初から再現手順を固定して構築する。

重要：0.1.3 の文書で言う `telnet`（ttyS1 を TCP シリアル公開し接続する方式）と、今回の `telnetd`（ゲストのTCP 23番）は別物。
ただし「0.1.3の機能は全て使える前提」なので、**ttyS0/ttyS1 同時ログイン（既存）も維持**したまま追加する。


# 用語

- Rocky（ホスト）：QEMU/KVM を直接起動するマシン
- UmuOS（ゲスト）：起動対象（UmuOS-0.1.4 Base Stable）
- ローカルPC：TeraTerm で `192.168.0.202:23` に接続する端末
- Ubuntu（開発）：`nc` で UmuOS にファイルを送る端末


# スコープ

## やること（今回）

- Rocky上で QEMU を **CLI起動**し、常時稼働させる（virt-manager 前提を排除）
- ゲスト（UmuOS / ext4 rootfs）でネットワークを最小初期化（固定IP）
- BusyBox `telnetd` を起動し、`/bin/login` 経由で `root`/`tama` がログインできる
- Ubuntu→UmuOS へ `nc` でファイル転送できる（手順を固定）

## やらないこと（今回）

- SSH導入
- インターネット越しの運用
- `su` の成立（混線させない）
- virt-manager の再現性問題（No bootable device 等）の調査


# 変更点（成果物）

作業は既存の UmuOS-0.1.x（特に 0.1.3）を壊さないため、UmuOS-0.1.4 Base Stable を新規に構築し、最初から再現できる手順を固定する。

成果物の配置（固定）：

- `UmuOS-0.1.4/` を新規に作り、ビルド〜起動までをこの配下で完結させる
- 既存の `UmuOS-0.1.3/` を含む 0.1.x ディレクトリは参照のみ（変更しない）

`UmuOS-0.1.4/` 配下の成果物（固定）：

- `kernel/`：Linux 6.6.18 のビルド用ツリー（`.config` とビルド手順を固定し、ビルド成果物をここに置く）
- `initramfs/`：initramfs 生成に必要なソース・rootfs・生成物（`initrd.cpio` 等）
- `disk/`：永続 ext4 ディスク（`disk.img`）
- `iso_root/`：ISO 生成素材（GRUB設定含む）
- `run/`：起動用の固定引数メモ（後で起動スクリプト化する前提の I/F 定義）
- `logs/`：ホスト側ログ（QEMUコンソール等）と、ゲスト側ログ（/logs/boot.log）の観測メモ
- `docs/`：0.1.4 固有の設計・再現手順（基本設計書/詳細設計書をここから作る）


# 重要な設計判断（先に固定）

## 1) root telnet ログインは必須

`su` が未成立のため、telnet で `root` に入れることを必須受入条件とする。
また `tama` ログインも必須とする（運用と切り分けのため）。

## 2) BusyBox login と /etc/securetty

BusyBox `login` はビルド設定によって `root` のログイン端末を `/etc/securetty` で制限する。
telnetd セッションは通常 `/dev/pts/*` になるため、root を許可するなら ext4 側に `/etc/securetty` を用意して `pts/*` を許可する。

テンプレ（同時接続上限を 10 に固定）：

```txt
ttyS0
ttyS1
pts/0
pts/1
pts/2
pts/3
pts/4
pts/5
pts/6
pts/7
pts/8
pts/9
```

## 3) ネットワークは br0 ブリッジ（ホストは Rocky）

ゲストは TAP で `br0` に接続し、LANに直結する。
これによりローカルPCやUbuntuから `192.168.0.202` へ直接到達できる。

補足：

- `tap0` は「例の名前」であり固定ではない。事故防止のため、本計画では用途が分かる名前（固定：`tap-umu`）を使う。
- 既に `vnet0` 等が `br0` に参加していても、それは既存VM（libvirt）用である可能性が高いので使い回さない。


# 受入基準（合格条件）

## A. 0.1.3 既存機能（維持できていること）

- `switch_root` 成立
- ttyS0 で `root`/`tama` パスワードログイン成立
- ttyS1（QEMU TCPシリアル）でも同時ログイン成立（ホストローカル用途）
- ゲスト側 `/logs/boot.log` が追記される

## B. 追加機能（今回の要件）

- ゲスト `eth0` に `192.168.0.202/24` が付く
- `default via 192.168.0.1` が入る
- ローカルPC（TeraTerm）から `192.168.0.202:23` へ接続 → `login:` → `root` ログイン成功
- 同様に `tama` ログイン成功
- Ubuntu から `nc` で UmuOS にファイル転送できる


# 実装計画（手順を固定）

## フェーズ 0: 作業コピーの作成

- 既存 0.1.x は基準点として不変（参照のみ）
- UmuOS-0.1.4 Base Stable を新規構築し、最初から再現できる手順（依存・ビルド順・成果物配置・ログ採取）を固定する

## フェーズ 1: Rocky（ホスト）のブリッジ確認

観測点：

- `br0` が存在し、物理NICが参加している
- `br0` は `192.168.0.200/24` を持つ

判定（固定）：

- `br0` が存在しない場合、本計画は中止（今回は br0 の新規作成手順は扱わない）

補足（観測用コマンド例）：

```bash
ip a
bridge link show
```

補足：br0 の作り方自体は環境依存のため、本計画では「既にある」前提に寄せる。

## フェーズ 2: QEMU を Rocky 上でCLI起動（常時稼働）

方針：virt-manager を使わず、引数を固定した QEMU コマンドで起動する。

要点：

- `-enable-kvm`（必須：KVM が使えない場合は本計画は中止）
- `-serial stdio`（ttyS0 観測）
- `-serial tcp:127.0.0.1:5555,server,nowait,telnet`（ttyS1 同時ログイン＝0.1.3の機能維持）
- ネット：UmuOS 用の TAP を 1 本作って `br0` に接続し、`virtio-net` でゲストへ渡す
- 運用：常時稼働は `tmux` 固定（systemd ユニット化は今回のスコープ外）
- ログ：ホスト側の QEMU コンソールログを必ずファイルに保存する

補足（権限）：

- Rocky 側は root ログインで作業する前提のため、本計画のコマンド例では `sudo` を付けない。
- root 以外で実行する場合は `sudo` を付与すること。

例（コマンドの骨子、実際のパスは作業コピーに合わせる）：

```bash
# （ホスト：RockyLinux 9.7 側で）UmuOS 用 TAP を作って br0 に接続（固定：tap-umu）
# ※計画段階では「固定手順」を記載する。実装フェーズで起動スクリプト化する。

# 1) 事前クリーンアップ（前回の異常終了等で tap-umu が残っていた場合）
ip link set dev tap-umu down || true
ip link del dev tap-umu || true

# 2) TAP 作成→br0 に接続→UP
ip tuntap add dev tap-umu mode tap user "$USER"
ip link set dev tap-umu master br0
ip link set dev tap-umu up

# 3) QEMU 起動（ホスト側ログは必ず script で採取する）
#    ※QEMU はフォアグラウンドで起動し、終了したら 4) のクリーンアップへ進む。
script -q -c "qemu-system-x86_64 \
  -enable-kvm -cpu host -m 1024 \
  -machine q35,accel=kvm \
  -nographic \
  -serial stdio \
  -serial tcp:127.0.0.1:5555,server,nowait,telnet \
  -drive file=./disk/disk.img,format=raw,if=virtio \
  -cdrom ./UmuOS-0.1.4-boot.iso \
  -boot order=d \
  -netdev tap,id=net0,ifname=tap-umu,script=no,downscript=no \
  -device virtio-net-pci,netdev=net0 \
  -monitor none" ./logs/host_qemu.console.log

# 4) 停止後クリーンアップ（必須：次回起動時に状態をブレさせない）
ip link set dev tap-umu down || true
ip link del dev tap-umu || true
```

起動スクリプト化（必須：手順固定と後片付け自動化）：

- Rocky（ホスト）側で「tap 作成 → QEMU 起動 → 終了時に tap 削除」を 1 本の起動スクリプトにまとめる。
- QEMU が異常終了しても tap が残らないよう、`trap`（`EXIT`）でクリーンアップを必ず実行する。
- スクリプトは詳細設計書で I/F（引数：`disk.img`/`boot.iso`、環境変数：`TAP_DEV`/`BR_DEV` 等）を固定してから実装する。

常時稼働の運用：

- `tmux` 内で起動し、サーバ上で保持する（固定）

## フェーズ 3: ゲスト（ext4 rootfs）のネットワーク初期化

目的：telnetd より先に「IPが付く」「デフォルトルートが入る」を確定する。

追加する設定ファイル（ext4）：`/etc/umu/network.conf`

```conf
IFNAME=eth0
MODE=static
IP=192.168.0.202/24
GW=192.168.0.1
DNS=192.168.0.1
TELNETD_ENABLE=1
NC_RECV_ENABLE=0
```

rcS 変更方針：

- 既存の mount / boot.log の杭は維持
- BusyBox の `ip` を必須とし、`ip link/addr/route` で設定する（`ifconfig`/`route` への分岐は作らない）

観測点（ゲスト）：

- `ip addr show dev eth0`
- `ip route`

## フェーズ 4: telnetd を常時起動（/bin/login）

手順固定：フォアグラウンドで観測できる形（`-F`）で開始し、成立後にデーモン化（`-F` なし）へ移行する。

- 手動観測（まずはttyS0上）：`telnetd -F -p 23 -l /bin/login`
- 常時起動（rcS）：`telnetd -p 23 -l /bin/login`

受入：

- ローカルPC（TeraTerm）で `192.168.0.202:23` に接続 → `root` ログイン成功
- 同様に `tama` ログイン成功

## フェーズ 5: /etc/securetty（root telnet ログイン必須のため）

ext4 側に `/etc/securetty` を作成し、`ttyS0`/`ttyS1` と `pts/*` を許可する。

観測：

- `tama` は通るが `root` だけ失敗する場合、securetty を最優先で疑う

## フェーズ 6: Ubuntu → UmuOS へ nc でファイル転送

前提：UmuOS 側に `nc` が存在し、Ubuntu 側も `nc` を持つ。
実装差があるため、最初に UmuOS 側で `nc -h`（または `nc --help`）で listen 書式を確認する。

基本フロー（固定：UmuOSが受け、Ubuntuが送る）：

```bash
# UmuOS 側（telnetログイン後）
mkdir -p /tmp/in
cd /tmp/in
nc -l -p 12345 > payload.bin

# Ubuntu 側
nc 192.168.0.202 12345 < payload.bin
```

受入：

- UmuOS 側でファイルが受信できる


# トラブルシュート（順序固定）

1) ttyS0 でログインできるか（できないなら telnet 以前）
2) ゲストに IP/route が入っているか（できないなら rcS/net 初期化）
3) telnetd が起動しているか（できないなら rcS/telnetd 起動）
4) LAN到達性（ローカルPC/Ubuntu → 192.168.0.202）
5) `login/shadow/securetty`（`root` だけ失敗するなら securetty）


# セキュリティ/運用メモ

- telnet は平文。今回は自宅LAN内を前提に利便性を優先する
- SSH への置換は本計画では扱わない（0.1.4 Base Stable は telnet 運用で固定）

ネットワーク実装メモ：

- MODE=static：`ip link set up` / `ip addr add` / `ip route replace default`

