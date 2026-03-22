RockyLinuxがブート中にswitch_rootしてUmuOSユーザーランドへ移行し、telnetで自宅からログインできるようにする：計画（完成形）

狙い（完成形の定義）：
- RockyLinux を「カーネル＋initramfs」まで起動させる
- initramfs の `/init` が `switch_root` で rootfs を切り替える
- 切り替え先 rootfs は UmuOS（disk.img 由来）で、`/etc/inittab` → `rcS` が走る
- `rcS` によりネットワーク初期化と `telnetd` 起動が行われ、自宅から `telnet <VPSのグローバルIP> 23` で `login:` が出る

重要な前提（ここが設計の要）：
- UmuOS-0.1.7 の initramfs `/init`（`init.c`）は、`root=UUID=...` を手掛かりに `/dev/vd*` `/dev/sd*` `/dev/nvme*` の ext4 を探す実装になっている。
	- つまり「disk.img をファイルとして置いて loop で読む」だけでは、そのままでは見つからない（`/dev/loop*` が候補に入っていない）
- したがって成立パターンは2つ：
	1) **確実ルート（推奨）**：disk.img を“ブロックデバイス化”する（専用パーティション / LVM LV / 別ディスク）
	2) **ファイル運用ルート**：initramfs 側を改造して loop を候補に入れ、disk.img を loop デバイスとして使う

セキュリティ注意（必読）：
- telnet は平文。インターネット直出しは危険。
- この完成形をやるなら「かごや側のFW（上流のパケットフィルタ）」で **自宅の送信元IPだけ 23/TCP を許可** を前提にする。
	- UmuOS（BusyBox中心の最小ユーザーランド）では Rocky の `firewalld` のような防御層が基本ないため。

参照（設計の正）：
- UmuOS rcS（telnetd起動/ネットワーク初期化の考え方）：[UmuOS-0.1.7-base-stable/docs/UmuOS-0.1.7-base-stable-解説書.md](../UmuOS-0.1.7-base-stable/docs/UmuOS-0.1.7-base-stable-解説書.md)
- initramfs `/init`（switch_root 実装）：[UmuOS-0.1.7-base-stable/initramfs/src/init.c](../UmuOS-0.1.7-base-stable/initramfs/src/init.c)

---

# 0. ゴール定義（Doneの定義：完成形）

Done（成功）：
1) 再起動後、initramfsログに次が出る
	 - `[init] mount root ok (rw): ...`
	 - `[init] exec: /bin/switch_root /newroot /sbin/init`
2) switch_root 後、UmuOS rootfs の `rcS` が動き、ゲスト（=同一マシン）のネットワークが成立する
3) 自宅から `telnet <VPSのグローバルIP> 23` で `login:` が出てログインできる

Rollback（失敗時に戻せる）：
- Rocky の通常起動エントリを温存し、**一回だけ試す**（`grub2-reboot` 等）ことで、失敗しても次回通常起動に戻せる
- かごやの管理コンソール（シリアル/レスキュー）で復旧できる手段を事前に確保する

---

# 1. 成立パターンの選択（最初に決める）

## 1.1 推奨：disk.img をブロックデバイス化（パーティション/LV）

なぜ推奨か：
- 既存の `init.c` のまま成立させやすい（`/dev/vda3` のように見える）
- initramfs を最小変更で済ませられる

ざっくり手順（計画の骨）：
1) VPS のディスクに空き領域を確保し、UmuOS 用のパーティション（例：`/dev/vda3`）または LVM LV を作る
2) そのブロックデバイスを ext4 で初期化し、UUID を UmuOS が期待する値へ合わせる
3) UmuOS rootfs（disk.img の中身）をそこへ展開する（= `/sbin/init` `/etc/inittab` `/etc/init.d/rcS` が存在する状態）
4) Rocky の GRUB に「UmuOS起動用エントリ（kernelはRockyのもの）」を追加し、`root=UUID=<UmuOSのUUID>` で initramfs `/init` に switch_root させる

注意：disk.img を `dd` でパーティションへ“丸ごと書く”方式もあるが、運用/切り分けが難しくなりやすい。
（ただし「とにかく早く形にする」目的なら有効。)

## 1.2 代替：disk.img をファイル運用（loop で読む）

成立させる条件：
- initramfs `/init` が、どこかのファイルシステムを先にマウントして disk.img ファイルへアクセスできること
- disk.img を loop デバイスへ関連付けし、その loop デバイスを ext4 として mount できること
- `init.c` の候補デバイスに `/dev/loop*` を追加（=コード修正＆initramfs再生成）が必要

このルートは「やることが増える」ので、最初は 1.1 を推奨。

---

# 2. ネットワーク設計（telnet成功条件の核）

telnet 成功には、UmuOS 側が次を満たす必要がある：
- NIC 名が `eth0` で揃う（`net.ifnames=0 biosdevname=0` を kernel cmdline に入れる）
- `/etc/umu/network.conf` が VPS の実ネットワークと一致（IP/プレフィックス/GW/DNS）
- `rcS` が `telnetd -p 23 -l /bin/login` を起動

実務的な決め方：
1) Rocky通常起動で `ip a` / `ip r` を控える（グローバルIP、デフォルトGW、NIC名）
2) その値を UmuOS の `/etc/umu/network.conf` に焼く
3) かごや側FWで 23/TCP を自宅IPのみに制限

---

# 3. 安全な切替手順（ブートローダ改変で詰まないため）

原則：
- **デフォルトはRockyのまま**
- 起動テストは **1回だけそのエントリで起動**（ワンショット）

手順案：
1) GRUBへ UmuOS 起動エントリを追加（40_custom等）
2) `grub2-mkconfig` で反映（UEFI/BIOSで出力先が違うので注意）
3) `grub2-reboot '<エントリ名>'` で次回だけ UmuOSエントリを選ぶ
4) 再起動
5) 失敗したら管理コンソールで Rocky エントリへ戻す

---

# 4. 旧案（参考）：Rocky上でQEMU起動してUmuOSへ入るプラン

以下は「RockyをホストとしてQEMUでUmuOSを起動する」案。
完成形（Rockyがswitch_rootしてUmuOS化）とは別物なので、参考として残す。

# 0. ゴール定義（Doneの定義）

最小の Done（MVP）：
1) Rocky から QEMU で UmuOS を起動できる
2) initramfs が `switch_root` に到達し、ext4 の永続 rootfs へ移行できる
3) 自宅PCから Rocky に SSH し、その経路で UmuOS にログインできる

追加の Done（発展）：
4) 自宅PCから UmuOS の telnetd に到達できる（※ネットワーク設計が必要）

重要：telnet をインターネットへ直出しはしない（研究用途でも危険）。
「自宅→Rocky(SSH)→(トンネル)→ゲストtelnet」の形に寄せる。

---

# 1. 事前調査（Rocky側の制約を確定する）

目的：あとでハマるポイント（KVM不可/ブリッジ不可/ファイアウォール）を先に確定する。

## 1.1 Rockyに入って確認

自宅PCから（例）：

```bash
ssh <user>@<rocky_public_ip_or_fqdn>
```

Rocky上で：

```bash
uname -a
cat /etc/os-release

# KVMが使えるか（無いと -enable-kvm で死ぬ）
ls -l /dev/kvm || true

# 仮想化支援フラグ（KVM不可の切り分け）
egrep -m1 'vmx|svm' /proc/cpuinfo || true

# 受信FW（必要ポートだけ開ける方針を決める）
sudo firewall-cmd --state 2>/dev/null || true
sudo firewall-cmd --list-all 2>/dev/null || true
```

判断：
- `/dev/kvm` が無い/使えない場合 → 「TCGで起動する」プランBを使う（後述）
- `br0` のようなブリッジを作れるか不明 → まず `NET_MODE=none` で到達性を作る

---

# 2. Rocky側の準備

## 2.1 必要パッケージ

最低限：QEMU本体 + ログ用 `script` + `ip`。

例：

```bash
sudo dnf -y install qemu-kvm qemu-img iproute util-linux
```

（環境によりパッケージ名は多少差がある。入らなければ `dnf search qemu-system` で確認。）

## 2.2 3点セット配置

推奨配置：`/root/umuos017/` のような専用ディレクトリ。

```bash
sudo mkdir -p /root/umuos017
sudo chown root:root /root/umuos017
sudo chmod 700 /root/umuos017
```

ローカルから転送（例）：

```bash
scp UmuOS-0.1.7-base-stable-boot.iso <user>@<rocky>:/tmp/
scp disk.img <user>@<rocky>:/tmp/
scp UmuOS-0.1.7-base-stable_start.sh <user>@<rocky>:/tmp/

ssh <user>@<rocky>
sudo mv /tmp/UmuOS-0.1.7-base-stable-boot.iso /root/umuos017/
sudo mv /tmp/disk.img /root/umuos017/
sudo mv /tmp/UmuOS-0.1.7-base-stable_start.sh /root/umuos017/
sudo chmod +x /root/umuos017/UmuOS-0.1.7-base-stable_start.sh
```

観測点：

```bash
ls -l /root/umuos017
```

---

# 3. 起動（最短：ゲストNWなしでOK）

## 3.1 QEMU起動（NET_MODE=none）

まずは「ネットワークを切ってでも switch_root できる」状態を作る。

```bash
cd /root/umuos017
sudo NET_MODE=none TTYS1_PORT=5555 ./UmuOS-0.1.7-base-stable_start.sh
```

観測点（ホスト側）：
- `host_qemu.console_*.log` が生成される
- そのログに initramfs の `[init] ...` が残る

観測点（ゲスト側のログメッセージ）：
- initramfs が次を出す：
	- `[init] mount root ok (rw): ...`
	- `[init] exec: /bin/switch_root /newroot /sbin/init`

ここまで出れば「switch_root 自体」は成立している可能性が高い。

## 3.2 switch_root後の成立確認（ログイン後）

ログインできたら、最低限これを確認（ext4へ移った証拠を取る）：

```sh
mount
cat /proc/cmdline
ls -l /logs/boot.log || true
tail -n 80 /logs/boot.log || true
```

成功の典型：
- `mount` に `/dev/vda on / type ext4` が出る
- `/logs/boot.log` が存在し、起動情報が追記されている

注意：`NET_MODE=none` だと `eth0` が出ないので、NTP/telnetd/ftpd は（起動しても）外から到達できない。
ここは「切り分け用の段階」なのでOK。

---

# 4. 自宅からのアクセス（最短ルート）

ここで言う「アクセス」は「UmuOSにログインして操作できる」こと。
最短ルートは **シリアル（ttyS1）を SSH トンネルで運ぶ**。

理由：
- 起動スクリプトは `ttyS1` を `tcp:127.0.0.1:5555,telnet` でホストへ出している
- つまり Rocky のローカルからは `telnet 127.0.0.1 5555` で繋がる
- 127.0.0.1 バインドなので、外部へは直接公開されない（良い）

## 4.1 RockyへSSHしてローカル接続（手動）

自宅 → Rocky：

```bash
ssh <user>@<rocky>
```

Rocky上で UmuOS の ttyS1 へ：

```bash
telnet 127.0.0.1 5555
```

## 4.2 自宅PCでSSHポートフォワード（推奨）

自宅PCで：

```bash
ssh -L 5555:127.0.0.1:5555 <user>@<rocky>
```

別ターミナルで自宅PCから：

```bash
telnet 127.0.0.1 5555
```

観測点：
- Enter を数回押すと `login:` が出る（表示タイミング差のことがある）

この経路が出来ると「自宅から操作可能」が達成できる。

---

# 5. （発展）自宅から“ネットワーク経由”でUmuOSへ入る

結論：
- 直telnet公開はしない
- まず「Rocky上で待ち受け → SSHトンネルで自宅へ運ぶ」形にする

ここは 2パターンある。

## パターンA：QEMU user-net + hostfwd（VPSで一番現実的）

狙い：ホスト側は特権NW設定（br0等）なしで、ゲストtelnet(23)をホストの任意ポートへ転送する。

要点：
- QEMUの user-net はゲストに `10.0.2.0/24` のNAT環境を提供する（標準は `10.0.2.15`, GWは `10.0.2.2`）
- UmuOS は `rcS` が `/etc/umu/network.conf` を読み、static IP を打つ設計
	- よって `network.conf` を `10.0.2.15/24` へ寄せるのが手堅い

### A-1. disk.img の network.conf を修正

Rocky上で disk.img をループマウントして書き換える：

```bash
sudo mkdir -p /mnt/umuos017
sudo mount -o loop /root/umuos017/disk.img /mnt/umuos017

sudo sed -n '1,120p' /mnt/umuos017/etc/umu/network.conf
```

編集案（例）：

```conf
IFNAME=eth0
MODE=static
IP=10.0.2.15/24
GW=10.0.2.2
DNS=8.8.8.8
```

反映：

```bash
sudo tee /mnt/umuos017/etc/umu/network.conf >/dev/null <<'EOF'
IFNAME=eth0
MODE=static
IP=10.0.2.15/24
GW=10.0.2.2
DNS=8.8.8.8
EOF

sudo umount /mnt/umuos017
```

### A-2. QEMU起動（手動コマンド）

現状の起動スクリプトは `NET_MODE=tap|none` なので、user-net を使う場合は qemu を手で起動する。

概念（例：ホスト2323→ゲスト23(telnet)）：

```bash
cd /root/umuos017
sudo qemu-system-x86_64 \
	-m 1024 \
	-nographic \
	-serial stdio \
	-serial tcp:127.0.0.1:5555,server,nowait,telnet \
	-drive file=./disk.img,format=raw,if=virtio \
	-cdrom ./UmuOS-0.1.7-base-stable-boot.iso \
	-boot order=d \
	-netdev user,id=net0,hostfwd=tcp:127.0.0.1:2323-:23 \
	-device virtio-net-pci,netdev=net0
```

※KVMが使えるなら `-enable-kvm -cpu host` を足す。使えないなら付けない。

### A-3. 自宅からの到達（SSHトンネル）

自宅PCで：

```bash
ssh -L 2323:127.0.0.1:2323 <user>@<rocky>
```

自宅PCから：

```bash
telnet 127.0.0.1 2323
```

観測点：
- `login:` が出れば勝ち

## パターンB：TAP+bridge（できるなら一番“それっぽい”）

狙い：ゲストが「VPSの外部ネットワーク」に直結したように見せる。
ただし VPS の提供仕様により、L2ブリッジやTAPが禁止されていることがある。

成立条件：
- Rocky側で `br0` を作れる
- `tap-umu` を `br0` にアタッチできる
- ゲストに static で “到達可能なIP/GW” を与えられる（かごやのネットワーク仕様次第）

このパターンは「プロバイダ都合の沼」が深いので、まずはパターンAを先にやる。

---

# 6. KVMが使えない場合（プランB：TCG）

症状：
- 起動時に `Could not access KVM kernel module` など

対策：
- 起動スクリプトを使わず、手動qemuで `-enable-kvm` を外して起動する
- 代わりに `-accel tcg`（または省略）で動かす

例：

```bash
sudo qemu-system-x86_64 -accel tcg -m 1024 -nographic ...
```

注意：TCGは遅い。だが「switch_root観測」や「ログイン成立」目的なら十分なことが多い。

---

# 7. トラブルシュート（優先順位付き）

## 7.1 switch_root に到達しない

確認（ホストの `host_qemu.console_*.log`）：
- `[init] want root UUID:` が出ていないか
- `[init] retry ... (device not found yet)` が延々続いていないか

原因候補：
- grub.cfg の `root=UUID=...` と disk.img の UUID が一致していない
- disk.img が起動スクリプトの期待位置にない（スクリプトは同ディレクトリの `disk.img` を参照）

## 7.2 ログインプロンプトが出ない（ttyS1）

切り分け：
- `telnet 127.0.0.1 5555` で繋がっているか
- Enter を数回押しても `login:` が出ないか

原因候補：
- QEMUが落ちている（Rocky側ターミナル/ログ確認）
- `TTYS1_PORT` が競合している（別プロセスが使用中）

## 7.3 ネットワークだけ成立しない

前提：
- `NET_MODE=none` では eth0 が出ないので当然失敗

切り分け（ゲスト内）：

```sh
ip a
ip r
cat /etc/umu/network.conf
cat /etc/resolv.conf
ps w | grep telnetd | grep -v grep || true
```

原因候補：
- ゲストIP/GWがホストの提供ネットワークと合っていない
- hostfwd/iptables/SELinux（ただしSSHトンネル方式なら最小化できる）

---

# 8. 実行順（チェックリスト）

1) Rockyで `/dev/kvm` とFW状態を確認
2) RockyにQEMUと必要ツールを入れる
3) 3点セットを `/root/umuos017/` に置く
4) `NET_MODE=none` で起動して switch_root を観測
5) 自宅→Rocky(SSH)→ttyS1 でログイン成立
6) （必要なら）user-net + hostfwd で telnet 経由ログインを追加

