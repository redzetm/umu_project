---
title: 0001 telnet機能（BusyBox telnetd）実装の計画
date: 2026-01-23
base: UmuOS-0.1.3
---

# 要件定義（確定）

- UmuOS-0.1.3 の機能は **全て使える前提**（既存の成立条件＝ttyS0/ttyS1ログイン・/logs/boot.log 等は維持）
- LAN内のローカルPCから `telnet` を使い **TeraTerm でアクセス**できる
- `root` ログイン、`tama` ログインが **どちらもできる**
- virt-manager 起動は今回は扱わない（RockyLinux 9.7 上で QEMU をターミナル起動し **常時稼働**させる）
- 開発環境（Ubuntu）から `nc` を利用して UmuOS に **ファイル転送**できる


# 前提環境

- QEMU/KVMホスト：RockyLinux 9.7（例：`192.168.0.200`）
- 開発環境：Ubuntu 24.04 LTS（例：`192.168.0.201`、バイナリ作成・nc送信）
- ローカルPC（MiniPC）：TeraTerm 端末
- ゲスト（UmuOS）：固定IP `192.168.0.202/24`、GW `192.168.0.1`
- L2：RockyLinux 側にブリッジ `br0` が存在し、ゲストは tap を介して `br0` に直結


# 目的

UmuOS-0.1.3 をベースに、ゲスト（UmuOS）上で BusyBox `telnetd` を起動し、同一セグメント上の端末（ローカルPC / Ubuntu）から `telnet 192.168.0.202:23` でログインできる状態を作る。

重要：0.1.3 の文書で言う `telnet`（ttyS1 を TCP シリアル公開し接続する方式）と、今回の `telnetd`（ゲストのTCP 23番）は別物。
ただし「0.1.3の機能は全て使える前提」なので、**ttyS0/ttyS1 同時ログイン（既存）も維持**したまま追加する。


# 用語

- Rocky（ホスト）：QEMU/KVM を直接起動するマシン
- UmuOS（ゲスト）：起動対象（UmuOS-0.1.3）
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

作業は UmuOS-0.1.3 を壊さないため、作業用コピーを作って実施する。

- 推奨：`UmuOS-0.1.3` → `UmuOS-0.1.3-telnetd`（または `UmuOS-0.1.4`）

変更が入る見込み（作業コピー側）：

- `disk/disk.img`（ext4 rootfs：rcS拡張、設定ファイル追加）
- （必要なら）`kernel/linux-6.18.1/.config`（virtio-net 等の built-in 確認）


# 重要な設計判断（先に固定）

## 1) root telnet ログインは必須

`su` が未成立のため、telnet で `root` に入れることを必須受入条件とする。
また `tama` ログインも必須とする（運用と切り分けのため）。

## 2) BusyBox login と /etc/securetty

BusyBox `login` はビルド設定によって `root` のログイン端末を `/etc/securetty` で制限する。
telnetd セッションは通常 `/dev/pts/*` になるため、root を許可するなら ext4 側に `/etc/securetty` を用意して `pts/*` を許可する。

テンプレ（例：同時接続上限を 10 に固定）：

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

ゲストは tap で `br0` に接続し、LANに直結する。
これによりローカルPCやUbuntuから `192.168.0.202` へ直接到達できる。


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

- 0.1.3 は基準点として不変にし、作業コピー側でのみ変更する

## フェーズ 1: Rocky（ホスト）のブリッジ確認

観測点：

- `br0` が存在し、物理NICが参加している
- `br0` は `192.168.0.200/24` を持つ（例）

補足：br0 の作り方自体は環境依存のため、本計画では「既にある」前提に寄せる。

## フェーズ 2: QEMU を Rocky 上でCLI起動（常時稼働）

方針：virt-manager を使わず、引数を固定した QEMU コマンド（またはスクリプト）で起動する。

要点：

- `-enable-kvm`（可能なら）
- `-serial stdio`（ttyS0 観測）
- `-serial tcp:127.0.0.1:5555,server,nowait,telnet`（ttyS1 同時ログイン＝0.1.3の機能維持）
- ネット：tap を作って `br0` に接続し、`virtio-net` でゲストへ渡す

例（コマンドの骨子、実際のパスは作業コピーに合わせる）：

```bash
# tap0 を作って br0 に接続（例）
sudo ip tuntap add dev tap0 mode tap user "$USER"
sudo ip link set dev tap0 master br0
sudo ip link set dev tap0 up

# QEMU 起動（ログ採取は必要に応じて script 等で包む）
qemu-system-x86_64 \
  -enable-kvm -cpu host -m 1024 \
  -machine q35,accel=kvm \
  -nographic \
  -serial stdio \
  -serial tcp:127.0.0.1:5555,server,nowait,telnet \
  -drive file=./disk/disk.img,format=raw,if=virtio \
  -cdrom ./UmuOS-0.1.3-boot.iso \
  -boot order=d \
  -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
  -device virtio-net-pci,netdev=net0 \
  -monitor none
```

常時稼働の運用：

- 手軽：`tmux`/`screen` 内で起動し、サーバ上で保持
- 堅牢：systemd ユニット化（別紙/別フェーズで良い）

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
- `ip` が使えるなら `ip link/addr/route` で設定
- `ip` が無い場合は `ifconfig` / `route` へ置換（分岐点としてログに杭を残す）

観測点（ゲスト）：

- `ip addr show dev eth0`（または `ifconfig eth0`）
- `ip route`（または `route -n`）

## フェーズ 4: telnetd を常時起動（/bin/login）

推奨：フォアグラウンドで観測できる形から開始し、成立後にデーモン化。

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

基本フロー（例：UmuOSが受け、Ubuntuが送る）：

```bash
# UmuOS 側（telnetログイン後）
mkdir -p /tmp/in
cd /tmp/in
nc -l -p 12345 > payload.bin

# Ubuntu 側
nc 192.168.0.202 12345 < payload.bin
```

受入：

- UmuOS 側でファイルが受信でき、必要なら `chmod +x` して実行できる


# トラブルシュート（順序固定）

1) ttyS0 でログインできるか（できないなら telnet 以前）
2) ゲストに IP/route が入っているか（できないなら rcS/net 初期化）
3) telnetd が起動しているか（できないなら rcS/telnetd 起動）
4) LAN到達性（ローカルPC/Ubuntu → 192.168.0.202）
5) `login/shadow/securetty`（`root` だけ失敗するなら securetty）


# セキュリティ/運用メモ

- telnet は平文。今回は自宅LAN内を前提に利便性を優先する
- 必要なら将来 SSH へ置換（別フェーズ）
	- MODE=static：`ip link set up` / `ip addr add` / `ip route replace default`

		- `ip` が無い場合は `ifconfig` / `route` で同等の処理に置換する（計画上の分岐点として固定）

