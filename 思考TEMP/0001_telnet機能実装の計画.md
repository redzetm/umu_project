---
title: 0001 telnet機能（BusyBox telnetd）実装の計画
date: 2026-01-21
base: UmuOS-0.1.3
---

# 前提環境

- 仮想環境：RockyLinux 9.7 の仮想マシンマネージャ（QEMU/KVM, virt-manager）。ゲスト（UmuOS）はブリッジ接続。
- ネットワークセグメント：`192.168.0.0/24`、デフォルトGW：`192.168.0.1`
- 開発環境（任意）：ホスト上の Ubuntu 24.04 LTS（仮想マシン）
- ローカルPC：MiniPC から TeraTerm で各マシンにアクセス（基本はSSH。UmuOSにはtelnetで接続）
- 固定IP：
	- ホスト（RockyLinux 9.7）：`192.168.0.200`
	- 開発用 Ubuntu 24.04 LTS：`192.168.0.201`
	- ゲスト（UmuOS 0.1.x）：`192.168.0.202`
- ゲスト（UmuOS 0.1.x）は `UmuOS-0.1.x-boot.iso` と `disk.img` を virt-manager に読み込ませて起動

# 目的

UmuOS-0.1.3 をベースに「ゲスト（UmuOS）上で BusyBox `telnetd` を起動し、同一セグメント上のローカルPC（MiniPC）から telnet でログインできる」
機能を追加する。

重要：0.1.3 の文書で言う `telnet` は「ttyS1 を TCP シリアル公開し接続する方式（QEMUオプション依存）」であり、**ゲスト（UmuOS）のtelnetdではない**。
本計画は新たに **ゲスト（UmuOS）のtelnetd** を導入する。

# 用語

- ホスト：RockyLinux 9.7 の仮想マシンマネージャ（virt-manager）側
- ゲスト：UmuOS 側
- ローカルPC（MiniPC）：TeraTermで telnet 接続する端末

# 背景（既存ドキュメントからの前提）

- 0.1.1 系の失敗：telnet は「ネット×デーモン×login×shadow」が絡み、混線すると切り分けが破綻しやすい。よって **ベース（ブート→switch_root→シリアルログイン）を固定してから**最後に導入する。
- 0.1.2/0.1.3 の基本方針：動いたら成功ではなく「再現手順と受入条件が残って成功」。
- 0.1.3 の成立条件：ttyS0（必要ならttyS1）での観測と、最小ログ（ゲスト（UmuOS）側 `/logs/boot.log`）。

# スコープ

## やること（今回）

- virt-manager のネットワークを **ブリッジ接続**にし、ローカルPC（MiniPC）からゲスト（UmuOS）へ直接到達できるようにする
- ゲスト（UmuOS / ext4 rootfs）でネットワークを最小初期化（固定IP：`192.168.0.202/24`）
- BusyBox `telnetd` を起動し、`/bin/login` でパスワードログインできる
- 観測点（ログの杭）と切り分け順序を計画に固定する

## やらないこと（今回）

- NAT + hostfwd 前提の運用（ローカルに閉じたい場合は別フェーズで追加）
- SSH導入
- インターネット越しの遠隔運用（telnetは平文のため不採用）
- su の成立（0.1.3で未成立。telnetd導入と混ぜない）

# 成果物（この計画で増える/変わるもの）

基本は UmuOS-0.1.3 をベースにするが、混線防止のため **作業用コピー**を推奨：

- 推奨：`UmuOS-0.1.3` を `UmuOS-0.1.4`（または `UmuOS-0.1.3-telnetd`）として複製し、0.1.3 を不変の基準点として残す

変更が入る見込み（作業コピー側）：

- `disk/disk.img`（ext4 rootfs：rcSと設定ファイル追加）
- （任意）virt-manager のゲストNIC設定（ブリッジ接続への切替）
- （必要なら）`kernel/linux-6.18.1/.config`（virtio-net を built-in 化）

# 重要な設計判断（先に固定する）

## 1) “telnet” の意味を2つに分ける（混乱防止）

- **ttyS1 telnet（既存・0.1.3、任意）**：QEMUオプションで ttyS1 をTCP公開している場合に、ホストから接続する方式。ネットワーク不要。
- **guest telnetd（今回・新規）**：ローカルPC（MiniPC）が `192.168.0.202:23` に接続し、ゲスト（UmuOS）の `telnetd` に到達する。ブリッジ接続が前提。

この2つは別機能なので、計画・ログ・ポート番号・用語を分離して扱う。

## 2) ネットワークはブリッジ接続（ローカルPCから直接到達）

今回の要件は「ローカルPC（MiniPC） → ゲスト（UmuOS）へ直接telnet（ブリッジ接続）」なので、virt-manager のNICをブリッジに接続し、ゲストが同一セグメントのIPを持つ前提で進める。

- 到達点：ローカルPC（MiniPC） → ゲスト（UmuOS） `192.168.0.202:23`
- ゲスト（UmuOS）のIP付与：固定IP（`192.168.0.202/24`）

注意：telnet は平文でLANに露出する。自宅NWがセキュアでも、必要なら「telnetを許可する端末/時間帯を絞る」「隔離セグメントを作る」等の運用を先に決める。

## 3) カーネルの “virtio-net” は built-in を原則とする

UmuOSのrootfsは最小で、モジュールロードを前提にしない（initramfs/ext4側に modules を入れていない）。
そのため NIC を追加するなら、最低でも以下を **`=y`** で持つことを前提にする。

- `CONFIG_NET=y`
- `CONFIG_INET=y`
- `CONFIG_VIRTIO_NET=y`
- `CONFIG_VIRTIO_PCI=y`

（現状の0.1.3は virtio-blk しか必須確認していないため、ここが追加の落とし穴）

## 4) root の telnet ログインは許可する（自宅LAN前提）

本環境は「自宅LAN内」「ルータでインバウンドを許可していない」前提のため、運用上の利便性を優先して telnetd 経由の root ログインを許可する。

運用方針：観測の利便性を優先し、telnetd は常時起動（常にtelnetで接続できる状態）とする。パスワードも「観測を優先して簡易運用」とする。

# 実装計画（フェーズ分割：1回1変数）

## フェーズ A: 0.1.3 のベースが成立していることを再確認

目的：telnetd導入前に「戻れる状態」を確定する。

- [ ] `UmuOS-0.1.3-boot.iso` + `disk/disk.img` で起動できる
- [ ] `switch_root` が成立する（ttyS0ログに明確な杭が出る）
- [ ] ttyS0 で `root` / `tama` がパスワードログインできる
- [ ] （任意）ttyS1（TCPシリアル）でもログインできる（QEMUオプション依存）
- [ ] `/logs/boot.log` が追記される

ここが崩れたら フェーズ B 以降に進まない。

## フェーズ B: BusyBox に telnetd が含まれているか確認

目的：不足機能（busybox applet）が原因でハマるのを避ける。

ゲスト上（ttyS0でログインして）で確認：

- [ ] `busybox | grep -E '^telnetd$'`（または `busybox telnetd --help`）
- [ ] `busybox | grep -E '^login$'`（`-l /bin/login` で必要）

もし無い場合の方針（どちらか）：

1) busybox-static の別ビルド（CONFIG_TELNETD/CONFIG_LOGINを有効）を用意して ext4 へ入れる
2) いったん telnetd を諦め、`nc` 等の別経路でネット疎通だけを先に確立（telnetdは次段）

## フェーズ C: virt-manager のNICをブリッジ接続にする

目的：ローカルPC（MiniPC） → ゲスト（UmuOS）への到達経路を作る。

やること（virt-manager）：

- ゲスト（UmuOS）のNICを「ブリッジ（例：`br0`）」に接続する

観測：

- ホスト（RockyLinux 9.7 の仮想マシンマネージャ）でブリッジが存在し、物理NICが参加していること
- ゲスト（UmuOS）側で `eth0` が見えて Link が上がること

## フェーズ D: ゲストのネットワーク初期化（ext4 rcS を拡張）

目的：telnetdより先に「IPが付く」「デフォルトルートが入る」を確定する。

### 追加する設定ファイル

ext4 側に `/etc/umu/network.conf` を追加し、rcSが読む。

推奨（ブリッジ）：

- `IFNAME=eth0`
- `MODE=static`
- `IP=192.168.0.202/24`
- `GW=192.168.0.1`
- `DNS=192.168.0.1`
- `TELNETD_ENABLE=1`（常時起動）

### rcS の変更方針

0.1.3 の rcS には既に `/proc /sys /dev /dev/pts` の mount と `/logs/boot.log` 追記がある。
ここに以下を追加する。

1) ネット設定
	- MODE=static：`ip link set up` / `ip addr add` / `ip route replace default`
	- MODE=dhcp：今回は固定IP前提（必要になったら別フェーズで `udhcpc` を検討）
2) ログ出力（成功/失敗を `/logs/boot.log` と `/dev/console` に短く残す）

注意点（0.1.1の教訓を踏襲）：

- IF の存在確認は `/sys/class/net/${IFNAME}` を見る（BusyBox ip の戻り値が信用できない場合がある）
- 失敗してもブートを止めない（観測優先）

### 受入（Phase D）

ゲスト（UmuOS）で以下ができる：

- [ ] `ip addr show dev eth0` に `192.168.0.202/24` が出る
- [ ] `ip route` に `default via 192.168.0.1 dev eth0` が出る

## フェーズ E: BusyBox telnetd 起動（最小）

目的：ネットワーク確定後に telnetd を有効化し、切り分けを明確にする。

### 起動方法（rcSから）

運用方針として telnetd は常時起動とする（`TELNETD_ENABLE=1`）。

推奨起動例（フォアグラウンドでログを取りたい場合）：

- `telnetd -F -p 23 -l /bin/login`

デーモン化したい場合：

- `telnetd -p 23 -l /bin/login`

`-l /bin/login` を使う理由：

- 0.1.1の知見通り「telnetdは単体ではなく login/shadow とセット」で成立させる

### 受入（Phase E）

ローカルPC（MiniPC）で：

- [ ] TeraTerm（または `telnet`）で `192.168.0.202:23` に接続できる
- [ ] `login:` が出る
- [ ] `root` または `tama` でログインできる（パスワードが通る）

ゲスト（UmuOS）で：

- [ ] `ps | grep telnetd`（または `netstat -tnlp` 相当があれば確認）
- [ ] `/logs/boot.log` に `[telnetd] started` 等の杭が残る

## フェーズ F: トラブルシュート用の固定手順（計画に埋め込む）

telnetが失敗したとき、0.1.1の失敗（混線）を避けるため **順序を固定**する。

1) シリアルでログインできるか（root/tama）→できないならtelnet以前の問題
2) ネットワークが上がっているか（IP/route）→できないなら virtio-net or rcS の問題
3) telnetdが起動しているか（ps / ログ）→起動していないなら rcS の条件分岐/コマンドの問題
4) L2/L3 到達性があるか（ローカルPC（MiniPC）→ゲストのARP/経路）→ブリッジ設定/IP設定の問題
	- 例：ローカルPC（MiniPC）→ `192.168.0.202` に ping が通るか（ICMPを許可するかは運用次第）
5) login/shadowが正しいか（shadow権限600、プレースホルダ未置換）→認証の問題

# 実装タスク（具体）

## 1) 作業用コピーを作る（推奨）

- `UmuOS-0.1.3` をコピーして作業する（0.1.3を壊さない）

## 2) kernel config を確認し、virtio-net を built-in 化

作業コピーの `kernel/linux-6.18.1/.config` を確認し、足りなければ `scripts/config` で有効化して再ビルド。

最低限の観測（例）：

- `grep -E '^(CONFIG_NET=|CONFIG_INET=|CONFIG_VIRTIO_NET=|CONFIG_VIRTIO_PCI=)' .config`

## 3) ext4 rootfs にネット設定ファイルを追加

- `disk/disk.img` を loop mount
- `/etc/umu/network.conf` を作成
- 権限は `600`（将来ブリッジ等で秘密情報を置く可能性を考慮）

## 4) ext4 rcS を拡張

- 0.1.3の rcS に、ネット初期化と telnetd 起動（フラグ制御）を追加
- 失敗しても止めない（`|| true`）

## 5) virt-manager のネットワーク設定（ブリッジ接続）

- ゲスト（UmuOS）のNICをブリッジに接続する（ホスト側のブリッジ作成が必要なら先に行う）

# 受入基準（この機能の合格条件）

- ベース（0.1.3相当）が壊れていない
	- `switch_root` 成立
	- ttyS0ログイン（root/tama）成立
	- （任意）ttyS1同時ログイン成立（QEMUオプション依存）
	- `/logs/boot.log` 追記
- 追加機能（telnetd）が成立
	- ゲスト（UmuOS） `eth0` に `192.168.0.202/24` が付く
	- `default via 192.168.0.1` が入る
	- ローカルPC（MiniPC）で `192.168.0.202:23` へ接続 → `login:` → `root` または `tama` ログイン成功

# セキュリティ/運用メモ

- telnetは平文だが、本計画は「自宅LAN内での運用」「ルータ設定を信頼する」前提で、観測の利便性（常時アクセス可能）を優先する。
- それでも気になった場合の逃げ道として、将来的にSSHへ置換する（別フェーズ）。

# 次の拡張（将来案）

- `telnetd` のログを /logs/boot.log へ整形して追記（接続回数、失敗理由の杭）
- 0.1.4 以降での `su` 成立（telnetdとは独立に切り分け）
- もしネットワークを恒常化するなら、busybox udhcpc 等の導入も検討（ただし “固定IP” との設計整合が必要）

