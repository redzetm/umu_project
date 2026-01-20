---
title: 0001 telnet機能（BusyBox telnetd）実装の計画
date: 2026-01-21
base: UmuOS-0.1.3
---

# 目的

UmuOS-0.1.3 をベースに「ゲストOS上で BusyBox `telnetd` を起動し、ホストから TCP でログインできる」機能を追加する。

重要：0.1.3 の文書で言う `telnet` は「ttyS1 を TCP シリアル公開しホストが接続する方式」であり、**ゲストtelnetdではない**。本計画は新たに **ゲストtelnetd** を導入する。

# 背景（既存ドキュメントからの前提）

- 0.1.1 系の失敗学：telnet は「ネット×デーモン×login×shadow」が絡み、混線すると切り分けが破綻しやすい。よって **ベース（ブート→switch_root→シリアルログイン）を固定してから**最後に導入する。
- 0.1.2/0.1.3 の基本方針：動いたら成功ではなく「再現手順と受入条件が残って成功」。
- 0.1.3 の成立条件：ttyS0/ttyS1 による同時ログインと、最小ログ（ホスト側シリアル保存 + ゲスト側 `/logs/boot.log`）。

# スコープ

## やること（今回）

- QEMU に NIC（virtio-net）を追加し、NAT + hostfwd で **ホスト `127.0.0.1:2223` → ゲスト `:23`** をつなぐ
- ゲスト（ext4 rootfs）でネットワークを最小初期化（IP/route/DNS）
- BusyBox `telnetd` を起動し、`/bin/login` でパスワードログインできる
- 観測点（ログの杭）と切り分け順序を計画に固定する

## やらないこと（今回）

- ブリッジ（LAN直結）運用（影響範囲が広く、0.1.1の教訓的に最後）
- SSH導入
- セキュアな遠隔運用（telnetは平文のため、原則ローカルhostfwdのみ）
- su の成立（0.1.3で未成立。telnetd導入と混ぜない）

# 成果物（この計画で増える/変わるもの）

基本は UmuOS-0.1.3 をベースにするが、混線防止のため **作業用コピー**を推奨：

- 推奨：`UmuOS-0.1.3` を `UmuOS-0.1.4`（または `UmuOS-0.1.3-telnetd`）として複製し、0.1.3 を不変の基準点として残す

変更が入る見込み（作業コピー側）：

- `disk/disk.img`（ext4 rootfs：rcSと設定ファイル追加）
- `umuOSstart.sh`（QEMU に NIC + hostfwd を追加）
- （必要なら）`kernel/linux-6.18.1/.config`（virtio-net を built-in 化）

# 重要な設計判断（先に固定する）

## 1) “telnet” の意味を2つに分ける（混乱防止）

- **ttyS1 telnet（既存・0.1.3）**：ホストが QEMU の TCPシリアルへ `telnet 127.0.0.1 5555` で接続する。ネットワーク不要。
- **guest telnetd（今回・新規）**：ホストが `telnet 127.0.0.1 2223` で接続し、QEMU の NAT/hostfwd を介してゲストの `telnetd` に到達する。ネットワーク必須。

この2つは別機能なので、計画・ログ・ポート番号・用語を分離して扱う。

## 2) ネットワークは NAT + hostfwd（影響範囲を閉じる）

0.1.1 の教訓（ブリッジで基盤に波及しうる）に従い、まずは NAT（user networking）で成立させる。

- ホスト到達点：`127.0.0.1:2223`
- ゲスト到達点：`eth0` が `10.0.2.15/24`、GW `10.0.2.2`（QEMU user networking の定番）

## 3) カーネルの “virtio-net” は built-in を原則とする

UmuOSのrootfsは最小で、モジュールロードを前提にしない（initramfs/ext4側に modules を入れていない）。
そのため NIC を追加するなら、最低でも以下を **`=y`** で持つことを前提にする。

- `CONFIG_NET=y`
- `CONFIG_INET=y`
- `CONFIG_VIRTIO_NET=y`
- `CONFIG_VIRTIO_PCI=y`

（現状の0.1.3は virtio-blk しか必須確認していないため、ここが追加の落とし穴）

## 4) root の telnet ログインは “最初は許可しない”

telnetは平文で危険なので、まずは `tama` でログインできれば成功とする。
rootが必要なら ttyS0/ttyS1 を使う（観測主経路の維持）。

# 実装計画（フェーズ分割：1回1変数）

## Phase A: 0.1.3 のベースが成立していることを再確認

目的：telnetd導入前に「戻れる状態」を確定する。

- [ ] `UmuOS-0.1.3-boot.iso` + `disk/disk.img` で起動できる
- [ ] `switch_root` が成立する（ttyS0ログに明確な杭が出る）
- [ ] ttyS0 で `root` / `tama` がパスワードログインできる
- [ ] ttyS1（TCPシリアル）でもログインできる（同時ログイン）
- [ ] `/logs/boot.log` が追記される

ここが崩れたら Phase B 以降に進まない。

## Phase B: BusyBox に telnetd が含まれているか確認

目的：不足機能（busybox applet）が原因でハマるのを避ける。

ゲスト上（ttyS0でログインして）で確認：

- [ ] `busybox | grep -E '^telnetd$'`（または `busybox telnetd --help`）
- [ ] `busybox | grep -E '^login$'`（`-l /bin/login` で必要）

もし無い場合の方針（どちらか）：

1) busybox-static の別ビルド（CONFIG_TELNETD/CONFIG_LOGINを有効）を用意して ext4 へ入れる
2) いったん telnetd を諦め、`nc` 等の別経路でネット疎通だけを先に確立（telnetdは次段）

## Phase C: QEMU 起動スクリプトに NIC + hostfwd を追加

目的：ホスト→ゲストへの到達経路を「閉じた形」で作る。

`umuOSstart.sh` に以下を追加する（ポートは衝突しにくい 2223 を採用）：

- `-nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:2223-:23`

観測：ホストで `ss -ltn` し、`127.0.0.1:2223` が LISTEN していることを確認する。

※0.1.3の ttyS1 用ポート（5555）とは別。

## Phase D: ゲストのネットワーク初期化（ext4 rcS を拡張）

目的：telnetdより先に「IPが付く」「デフォルトルートが入る」を確定する。

### 追加する設定ファイル

ext4 側に `/etc/umu/network.conf` を追加し、rcSが読む。

推奨（NAT）：

- `IFNAME=eth0`
- `IP=10.0.2.15/24`
- `GW=10.0.2.2`
- `DNS=8.8.8.8`
- `TELNETD_ENABLE=0`（Phase Eまで0のまま）

### rcS の変更方針

0.1.3 の rcS には既に `/proc /sys /dev /dev/pts` の mount と `/logs/boot.log` 追記がある。
ここに以下を追加する。

1) ネット設定（`ip link set up` / `ip addr add` / `ip route replace default`）
2) ログ出力（成功/失敗を `/logs/boot.log` と `/dev/console` に短く残す）

注意点（0.1.1の教訓を踏襲）：

- IF の存在確認は `/sys/class/net/${IFNAME}` を見る（BusyBox ip の戻り値が信用できない場合がある）
- 失敗してもブートを止めない（観測優先）

### 受入（Phase D）

ゲストで以下ができる：

- [ ] `ip addr show dev eth0` に `10.0.2.15/24` が出る
- [ ] `ip route` に `default via 10.0.2.2 dev eth0` が出る

## Phase E: BusyBox telnetd 起動（最小）

目的：ネットワーク確定後に telnetd を有効化し、切り分けを明確にする。

### 起動方法（rcSから）

`TELNETD_ENABLE=1` のときのみ起動。

推奨起動例（フォアグラウンドでログを取りたい場合）：

- `telnetd -F -p 23 -l /bin/login`

デーモン化したい場合：

- `telnetd -p 23 -l /bin/login`

`-l /bin/login` を使う理由：

- 0.1.1の知見通り「telnetdは単体ではなく login/shadow とセット」で成立させる

### 受入（Phase E）

ホストで：

- [ ] `telnet 127.0.0.1 2223` が接続できる
- [ ] `login:` が出る
- [ ] `tama` でログインできる（パスワードが通る）

ゲストで：

- [ ] `ps | grep telnetd`（または `netstat -tnlp` 相当があれば確認）
- [ ] `/logs/boot.log` に `[telnetd] started` 等の杭が残る

## Phase F: トラブルシュート用の固定手順（計画に埋め込む）

telnetが失敗したとき、0.1.1の失敗（混線）を避けるため **順序を固定**する。

1) シリアルでログインできるか（root/tama）→できないならtelnet以前の問題
2) ネットワークが上がっているか（IP/route）→できないなら virtio-net or rcS の問題
3) telnetdが起動しているか（ps / ログ）→起動していないなら rcS の条件分岐/コマンドの問題
4) hostfwdが入っているか（ホストでLISTEN）→QEMU起動オプションの問題
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

## 5) QEMU 起動スクリプトを拡張

- `umuOSstart.sh` に `-nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:2223-:23` を追加
- （任意）ポート衝突検知（`ss -ltn`）で 2223 が使用中なら停止

# 受入基準（この機能の合格条件）

- ベース（0.1.3相当）が壊れていない
	- `switch_root` 成立
	- ttyS0ログイン（root/tama）成立
	- ttyS1同時ログイン成立
	- `/logs/boot.log` 追記
- 追加機能（telnetd）が成立
	- ゲスト `eth0` に `10.0.2.15/24` が付く
	- `default via 10.0.2.2` が入る
	- ホストで `telnet 127.0.0.1 2223` → `login:` → `tama` ログイン成功

# セキュリティ/運用メモ

- telnetは平文。基本は hostfwd によりホスト `127.0.0.1` に閉じる。
- ブリッジでLANに露出させない（必要になったら別フェーズで設計・受入を分離）。

# 次の拡張（将来案）

- `telnetd` のログを /logs/boot.log へ整形して追記（接続回数、失敗理由の杭）
- 0.1.4 以降での `su` 成立（telnetdとは独立に切り分け）
- もしネットワークを恒常化するなら、busybox udhcpc 等の導入も検討（ただし “固定IP” との設計整合が必要）

