UmuOS0.1.7-base-stableをかごやに立てているRockyLinuxから起動させて、switch_rootして、UmuOSに自宅からアクセスできるようにする：計画

この文書は「やること順」を固定し、観測点（成功判定）と切り分け手順を同時に持つための計画書。
最短ルートは **ゲストにネットワークを持たせなくても**（= `NET_MODE=none`）
「自宅 → Rocky(SSH) → QEMUのttyS1(SSHポートフォワード) → UmuOSログイン」で到達できる。

前提：
- かごや上の RockyLinux で QEMU が動く（KVMが使えなくても最悪TCGで動かせる）
- UmuOS-0.1.7-base-stable の「3点セット」を Rocky 側へ置ける
	- ISO: `UmuOS-0.1.7-base-stable-boot.iso`
	- 永続ディスク: `disk.img`
	- 起動スクリプト: `UmuOS-0.1.7-base-stable_start.sh`

参照（設計の正）：
- UmuOS側の起動手順と観測点：`UmuOS-0.1.7-base-stable/docs/UmuOS-0.1.7-base-stable-解説書.md`
- switch_root の実装（initramfsの /init）：`UmuOS-0.1.7-base-stable/initramfs/src/init.c`
- 起動スクリプト（ttyS1転送/NET_MODE）：`UmuOS-0.1.7-base-stable/UmuOS-0.1.7-base-stable_start.sh`

---

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

