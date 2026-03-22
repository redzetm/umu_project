RockyLinuxがブート中にswitch_rootしてUmuOSユーザーランドへ移行し、telnetで自宅からログインできるようにする：計画（完成形）

---

# 0.2系 方針案：Rocky pre-switch_root を維持し、ユーザーランドだけ UmuOS（0.2.1-dev 以降）

方針（ひとことで）：
- 「ブートまでの土台（kernel/initramfs/ドライバ/ディスク発見）は RockyLinux に任せる」
- 「`switch_root` 後の世界（`/sbin/init`→`inittab`→`rcS`→telnetd 等）は UmuOS が担当する」

これは “Rockyで起動するUmuOS” であり、0.2系の軸として筋が良い。

## この方式のメリット

### 1) ハードウェア/仮想環境互換が強い（VPSで刺さりやすい）

- VPS特有のストレージ/NIC/コンソールの癖を、Rockyの kernel+initramfs（dracut等）が吸収してくれる。
- UmuOSは「rootfsが見つからない」「NICが変な名前」などの沼に入りにくい。
- 特に “ディスク発見（/devが出るまでの流れ）” をRockyに委ねられるのが大きい。

### 2) セキュリティ更新・運用更新をRocky側に寄せられる

- kernelの脆弱性修正やドライバ回りの更新を、Rockyの更新フローに乗せやすい。
- UmuOS側は「ユーザーランド（研究対象）」に集中できる。

補足：UmuOSが研究用途でも、インターネットに置く以上 kernel の更新は重要。

### 3) 開発スピードが上がる（0.2系で攻めるべき場所が明確になる）

- kernel/config/initramfs のビルド地獄を避けられ、`rcS` や `/umu_bin` の改修が主戦場になる。
- 「再現性の高い土台（Rocky）」＋「変化させたい部分（UmuOS）」が分離できる。

### 4) 切り分けが簡単（責務境界が明確）

この方式は責務が綺麗に二分される：
- pre-switch_root（Rocky側）で壊れるなら：
	- そもそもブート・ディスク発見・initramfsの問題
- post-switch_root（UmuOS側）で壊れるなら：
	- `/sbin/init` / `inittab` / `rcS` / ネットワーク設定 / telnetd の問題

UmuOSの“学習”としても、どこからが自分の責務かが見えやすい。

### 5) ロールバックがやりやすい（VPSで致命傷になりにくい）

- GRUBのワンショット起動（`grub2-reboot`）と相性が良い。
- Rockyの通常エントリを残しておけば「次の再起動で復旧」が狙える。

### 6) 「telnetを使う」実験が成立しやすい

- UmuOSは最小ユーザーランドなので、ネットワーク周りを単純に保ちやすい（`rcS`で `ip link/addr/route` → `telnetd`）。
- 逆に複雑な仕組み（NetworkManager等）を避けられる。

注意：telnet自体は平文なので、到達制御（かごや側FWで送信元を自宅IPに限定等）とセットで運用する。

---

## NET公開の動機と、0.2系のセキュリティ運用方針（現実路線）

ここは「なぜインターネットに出す判断をしたか」を後から説明できるように残す。

### 動機（観測ベースの判断）

- LAN 内の UmuOS-0.1.7-base-stable に対して Nessus（脆弱性診断）を実施した。
- その結果、指摘の中心は **Telnet / FTP の露出部分**だった。
- 逆に言えば、露出面（attack surface）を Telnet/FTP に寄せている設計なので、
	「そこだけを意識して制御する」ことで、研究用途として成立する見込みがあると判断した。

この判断は「安全」という意味ではなく、
**どこを守れば研究として回るか（コスト配分）** を決めるためのもの。

### 方針（0.2系で採る運用スタイル）

1) 露出面を固定して、そこだけ重点的に見る
- 外部から到達できるサービスを最小にする（基本は Telnet/FTP のみ）
- 余計な常駐プロセスや管理面（Web UI 等）を増やさない

2) 到達制御を「OSの内側」ではなく「外側」で掛ける
- UmuOS は最小ユーザーランドで、一般的なディストリの防御層（firewalld等）を前提にしない。
- したがって、インターネット公開時は **かごや側のFW（上流）** で制御する。
	- 例：23/TCP と 21/TCP を「自宅の送信元IPだけ許可」
	- 可能なら診断用の送信元（Nessus走らせる場所）も限定

3) “攻撃に晒して学ぶ” を前提に、都度対応でバージョンを上げる
- 侵入を完全に防ぐより、「観測→修正→再デプロイ」を回して成熟させる方針。
- 0.2.1-dev 以降は「Rocky pre-switch_root 維持＋UmuOS userland」方式により、
	kernel更新や周辺互換の土台はRocky側に寄せつつ、UmuOS側の修正サイクルを高速にする。

4) 記録を残して、同じ事故を繰り返さない
- 露出ポート（何を開けたか）
- Nessusの指摘（どこが問題だったか）
- 対応内容（設定変更/コマンド差し替え/BusyBox設定等）
- 再スキャン結果

### リスク（あらかじめ明文化）

- Telnet/FTP は平文で、盗聴・改ざん・総当たりの対象になりやすい。
- “脆弱性がTelnet/FTPだけだった” は、その時点のスキャン結果に過ぎず、
	追加機能や設定変更で露出面は増える。

→ だからこそ、0.2系は「露出を最小に固定」「外側で到達制御」「都度対応で上げる」を運用の柱にする。

---

# 0.2.1-dev 準備：RockyLinux9.7 から参考用にコピーしておくファイル一覧

目的：
- 0.2系は「Rocky pre-switch_root を維持」する方針なので、Rocky側の“起動に効く設定”を手元に控えておく。
- 将来、UmuOS起動エントリが壊れた/挙動が変わったとき、差分をすぐ取れるようにする。

コピー先（このリポジトリ側）：
- `/home/tama/umu_project/UmuOS-0.2.1-dev/docs/RockyLinux9.7参考ファイル　ディレクトリ/`

注意：
- `/etc/NetworkManager/system-connections/*` などは権限が厳しい（600）ので、コピーは `sudo` 前提。
- 秘密情報（パスワード/鍵）が混じり得るファイルは、保存前にマスクするか、そもそも保存しない。

## A. ブート/GRUB（必須）

- `/etc/default/grub`
	- GRUBの基本パラメータ（kernel cmdlineの基礎）
- `/etc/grub.d/40_custom`
	- UmuOS起動用の `menuentry` を置く場所（運用の正）
- `/etc/grub2.cfg`（※通常はsymlink）
	- このVPSは **BIOS(Legacy) boot** だったので、実際に参照される grub.cfg の入口はこれ
- `/boot/grub2/grub.cfg`
	- 上の `/etc/grub2.cfg` が指す実体（今回の環境ではここが正）

（UEFI環境へ移した場合のための控え：）
- `/etc/grub2-efi.cfg`（※通常はsymlink）
	- UEFIで参照される grub.cfg の入口
- `/boot/efi/EFI/*/grub.cfg`（存在する場合）
	- UEFI側 grub.cfg の実体候補。`readlink -f /etc/grub2-efi.cfg` で実体パスを確認して保存する

補足（回答）：
- いまの実機確認結果：`test -d /sys/firmware/efi && echo UEFI || echo BIOS` → **BIOS(Legacy)**
- いまの実機確認結果：`readlink -f /etc/grub2.cfg` → **`/boot/grub2/grub.cfg`**
- よって、この環境で grub2-mkconfig の生成先として正しいのは基本的に **`/etc/grub2.cfg` の実体**（典型は `/boot/grub2/grub.cfg`）。

実測ログ：

```bash
[root@umuops etc]# readlink -f /etc/grub2.cfg
/boot/grub2/grub.cfg
[root@umuops etc]#
```

確認コマンド：

```bash
# UEFIかどうか
test -d /sys/firmware/efi && echo UEFI || echo BIOS

# BIOSで本当に使われるgrub.cfgの実体
readlink -f /etc/grub2.cfg 2>/dev/null || true

# （参考）UEFIで本当に使われるgrub.cfgの実体
readlink -f /etc/grub2-efi.cfg 2>/dev/null || true
```

## B. /boot（必須：どのkernel/initramfsで起動しているかの証拠）

- `/boot/vmlinuz-*`
- `/boot/initramfs-*.img`
- `/boot/loader/entries/*.conf`（ある場合：BLS運用）

目的：
- 「UmuOS起動エントリで、どの kernel と initramfs を使っていたか」を後で再現するため。

## C. dracut / initramfs生成（推奨）

- `/etc/dracut.conf`
- `/etc/dracut.conf.d/*.conf`

目的：
- 0.2系で “Rocky initramfs をそのまま使う” 場合でも、将来再生成が必要になる。
- そのときの差分確認（何が入っていたか）に効く。

## D. ストレージ/マウント（必須〜推奨：switch_root先に直結）

- `/etc/fstab`
	- Rocky側が通常起動するための基礎（壊すと戻れない）
- `/etc/crypttab`（使っている場合）
- `/etc/lvm/lvm.conf`（LVMを使っている場合）

目的：
- UmuOS rootfs を「別パーティション/LV」に置く方式の設計判断に効く。

## E. ネットワーク（推奨：UmuOS側へ値を持ち込む材料）

- `/etc/hostname`
- `/etc/hosts`
- `/etc/resolv.conf`（※NetworkManager管理の場合がある点に注意）
- `/etc/NetworkManager/NetworkManager.conf`
- `/etc/NetworkManager/system-connections/*.nmconnection`（機密が混じる可能性があるので要注意）
- `/etc/sysconfig/network-scripts/`（存在する場合）

目的：
- UmuOS の `/etc/umu/network.conf`（static IP/GW/DNS）を作るときの根拠として控える。

## F. Firewall / SELinux（推奨：telnet/ftp公開時の外側制御）

- `/etc/firewalld/firewalld.conf`
- `/etc/firewalld/zones/*.xml`
- `/etc/firewalld/services/*.xml`
- `/etc/selinux/config`

目的：
- 0.2系は「外側（かごやFW）で制御」が主だが、Rocky側で何をしていたかを記録しておく。

## G. 任意（役に立つことがある）

- `/etc/sysctl.conf`
- `/etc/sysctl.d/*.conf`
- `/etc/security/limits.conf`
- `/etc/security/limits.d/*`

---

## 調査メモ：かごやVPSのコンソールは tty0 / ttyS0 のどれか？（BIOS環境）

背景：
- かごやのWEBコンソールが「シリアル（ttyS0）」なのか「VGA（tty0）」なのかで、
	ブートログや `switch_root` 失敗時の観測性が大きく変わる。

今回の実測（Rocky通常起動時）：
- `/proc/cmdline` に `console=...` が無い（=現状は ttyS0 をカーネルコンソールとして使っていない）

実測ログ：

```bash
[root@umuops etc]# cat /proc/cmdline
BOOT_IMAGE=(hd0,msdos1)/vmlinuz-5.14.0-611.36.1.el9_7.x86_64 root=/dev/mapper/rl-root ro resume=/dev/mapper/rl-swap rd.lvm.lv=rl/root rd.lvm.lv=rl/swap rhgb quiet crashkernel=1G-2G:192M,2G-64G:256M,64G-:512M
[root@umuops etc]#
```

- `cat /sys/class/tty/console/active` は `tty0`（=かごやのWEBコンソールで見えているのは tty0 側）

実測ログ：

```bash
[root@umuops etc]# cat /sys/class/tty/console/active
tty0
[root@umuops etc]#
```
- `dmesg` に `ttyS0 ... 16550A` が存在し、virtio-vga/fbcon が primary と出ている
	- つまり「ttyS0 は存在するが、カーネルコンソールとしては使っていない」状態

### 1) 起動方式と、いま有効なカーネルコンソールを確認

```bash
test -d /sys/firmware/efi && echo UEFI || echo BIOS
cat /proc/cmdline
cat /sys/class/tty/console/active
```

### 2) ttyS0 が存在するか確認

```bash
ls -l /dev/ttyS0 /dev/ttyS1 2>/dev/null || true
dmesg | egrep -i 'ttyS0|ttyS1|hvc0|virtio|serial' | head -n 80
```

### 3) ttyS0 へカーネルログを出す（console= を追加）

目的：`switch_root` 前後のログを、VGAだけでなくシリアルにも出す。

Rocky（BIOS/Legaacy boot）では `grubby` が安全。

```bash
sudo grubby --update-kernel=ALL --args="console=tty0 console=ttyS0,115200n8"

# 観測のため、デバッグ中は quiet/rhgb を外すのを推奨（任意）
sudo grubby --update-kernel=ALL --remove-args="rhgb quiet"
```

再起動後の確認：

```bash
cat /proc/cmdline
cat /sys/class/tty/console/active
```

期待：
- `/proc/cmdline` に `console=ttyS0,115200n8` が入る
- `/sys/class/tty/console/active` が `tty0 ttyS0` のように増える

### 4) かごやWEBコンソールが ttyS0 と対応しているかの判定

Rocky上で：

```bash
echo "SERIAL TEST $(date)" | sudo tee /dev/ttyS0
```

これがWEBコンソール画面に出れば「WEBコンソール＝ttyS0」。
出なければ「WEBコンソール＝tty0（virtio-vga/fbcon側）」の可能性が高い。

---

## 調査メモ：NIC名が ens3 の件（UmuOS側の eth0 前提と衝突する）

今回の実測：
- `dmesg` で `virtio_net ... ens3: renamed from eth0` が出ている

対策：
- UmuOS側（`/etc/umu/network.conf`）が `IFNAME=eth0` 前提なら、
	UmuOS起動用の kernel cmdline に `net.ifnames=0 biosdevname=0` を必ず入れて NIC名を `eth0` に固定する。

---

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
2) `grub2-mkconfig` で反映（このVPSは **BIOS(Legacy)** なので、`/etc/grub2.cfg` の実体へ出力する）
3) `grub2-reboot '<エントリ名>'` で次回だけ UmuOSエントリを選ぶ
4) 再起動
5) 失敗したら管理コンソールで Rocky エントリへ戻す

---

# 4. 実装手順（推奨：disk.img をブロックデバイス化）

この節は「最短で成功させる」ための具体手順。

## 4.1 事前に必ず用意する（詰んだ時の復旧経路）

- かごや側の管理コンソール（シリアル/レスキュー/ISOブート等）でログインできることを確認
- Rocky の通常ブートが残ることを確認（GRUBのデフォルトは変えない）

今回の確認結果（回答）：
- かごや管理コンソールでログインできるか（GRUB操作・ログ確認ができるか）：**yes**
- レスキューモード/ISO起動が使えるか：**yes**

理由：ネットワーク設定をミスると、telnetは当然繋がらず、SSHも消える（Rockyユーザーランドを起動しないため）。

## 4.2 UmuOS rootfs 用のブロックデバイスを用意

推奨：VPSに「追加ディスク」をアタッチして、それを UmuOS rootfs に使う。
（ルートディスクのパーティション再分割は事故率が上がる）

Rocky通常起動の状態で：

```bash
sudo lsblk -f
```

以降は例として追加ディスクが `/dev/vdb` として見える想定。

### 4.2.1 パーティション作成（例）

```bash
sudo parted /dev/vdb --script mklabel gpt
sudo parted /dev/vdb --script mkpart umuos ext4 1MiB 100%
sudo partprobe /dev/vdb
sudo lsblk -f /dev/vdb
```

### 4.2.2 ext4作成（UUID固定）

UmuOSの `init.c` は `root=UUID=...` を読むので、ここで UUID を固定する。

```bash
U_UUID='d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15'   # UmuOS-0.1.7-base-stable が想定しているUUID例
sudo mkfs.ext4 -F -U "$U_UUID" /dev/vdb1
sudo blkid /dev/vdb1
```

## 4.3 disk.img から rootfs を展開（コピー）

disk.img は ext4 ファイルシステムイメージなので、Rocky上では loop でマウントして中身を取り出せる。

```bash
sudo mkdir -p /mnt/umu_img /mnt/umu_root

# disk.img の場所は環境に合わせる（例：/root/disk.img）
sudo mount -o loop /root/disk.img /mnt/umu_img
sudo mount /dev/vdb1 /mnt/umu_root

sudo cp -a /mnt/umu_img/. /mnt/umu_root/
sync

sudo umount /mnt/umu_root
sudo umount /mnt/umu_img
```

観測点（UmuOS rootfs 側に最低限があるか）：

```bash
sudo mount /dev/vdb1 /mnt/umu_root
ls -l /mnt/umu_root/sbin/init /mnt/umu_root/etc/inittab /mnt/umu_root/etc/init.d/rcS
sudo umount /mnt/umu_root
```

## 4.4 UmuOS rootfs 側のネットワーク設定（telnet成功条件）

Rocky通常起動で、現在のグローバルIPとデフォルトGWを控える：

```bash
ip a
ip r
```

その値を UmuOS の `/etc/umu/network.conf` に反映（例）：

```bash
sudo mount /dev/vdb1 /mnt/umu_root
sudo tee /mnt/umu_root/etc/umu/network.conf >/dev/null <<'EOF'
IFNAME=eth0
MODE=static
IP=<VPSのグローバルIP>/<prefix>
GW=<デフォルトGW>
DNS=8.8.8.8
EOF

sudo umount /mnt/umu_root
```

注意：VPSによっては“静的”ではなく、DHCPやクラウド側の仕組みが必要な場合がある。
その場合は `rcS` のDHCP対応（`udhcpc`）が必要になるので、ここで止めて「方式の再設計」に戻す。

## 4.5 initramfs（UmuOSの /init）を Rocky の /boot に配置

Rockyの kernel はそのまま使い、initramfs だけ UmuOS のものを使う。

配置例（ファイル名は自由）：

```bash
sudo cp /root/initrd.img-umuos017 /boot/initrd.img-umuos017
sudo ls -l /boot/initrd.img-umuos017
```

ここでいう `initrd.img-umuos017` は、UmuOS-0.1.7-base-stable の initramfs 成果物（`/init` を含む）を指す。
（手元で作ってscpするか、isoから取り出す。中に `/bin/switch_root` が必要。）

## 4.6 GRUB へ UmuOS 起動エントリを追加（ワンショット前提）

まず Rocky の kernel ファイル名を確認：

```bash
ls -l /boot/vmlinuz-*
```

次に `/etc/grub.d/40_custom` へ追記する（例、vmlinuz名は環境で置換）：

```bash
sudo tee -a /etc/grub.d/40_custom >/dev/null <<'EOF'

menuentry 'UmuOS (switch_root to ext4 UUID)' {
	linux /vmlinuz-<ROCKY_KERNEL_VERSION> \
		root=UUID=d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15 \
		rw \
		console=tty0 console=ttyS0,115200n8 \
		loglevel=7 \
		panic=-1 \
		net.ifnames=0 biosdevname=0
	initrd /initrd.img-umuos017
}
EOF
```

BIOS(Legacy) boot では、**`/etc/grub2.cfg` の実体パス**へ出力する。

```bash
# まず「正しい生成先」を確認（環境依存の差を吸収する）
readlink -f /etc/grub2.cfg

# そのパスへ grub.cfg を生成
sudo grub2-mkconfig -o "$(readlink -f /etc/grub2.cfg)"

# 追加したmenuentryが入ったか軽く確認
sudo grep -n "menuentry 'UmuOS (switch_root to ext4 UUID)'" "$(readlink -f /etc/grub2.cfg)" || true
```

補足：BIOSの場合、`/etc/grub2.cfg` の実体は典型的に `/boot/grub2/grub.cfg`。
UEFIへ移行した環境では `/etc/grub2-efi.cfg` の実体へ出力する。

## 4.7 次回だけ UmuOS エントリで起動（失敗しても戻れる）

```bash
sudo grub2-reboot 'UmuOS (switch_root to ext4 UUID)'
sudo reboot
```

## 4.8 成功確認（自宅からtelnet）

かごや側FWで 23/TCP を「自宅の送信元IPのみ許可」したうえで：

```bash
telnet <VPSのグローバルIP> 23
```

`login:` が出て、ログインできれば成功。

---

# 5. ファイル運用ルート（disk.img をそのまま使いたい場合：要改造）

このルートは「disk.imgをファイルとして置く」前提。
現状の `init.c` は `/dev/loop*` を走査しないため、少なくとも次が必要：

1) `init.c` の候補に `loop` を追加（例：`is_candidate_dev_name()` に `loop` を含める）
2) initramfs 内で `disk.img` を loop デバイスへ関連付け（`losetup` or `mount -o loop`）
3) その loop デバイスの UUID を `root=UUID=...` と一致させる、または `root=` の意味を拡張する

さらに「disk.imgファイルが置かれている元のFS」を initramfs で先にマウントする必要がある。
（例：`imgdev=UUID=<Rockyの/パーティションUUID>` と `imgpath=/path/to/disk.img` のような追加I/Fを設計する）

---

# 6. トラブルシュート（完成形向け）

## 6.1 telnetで `login:` が出ない

典型原因：ネットワークが上がっていない。
- かごや側FWが閉じている
- UmuOS の `/etc/umu/network.conf` が現実のIP/GWとズレている
- NIC名が `eth0` になっていない（`net.ifnames=0 biosdevname=0` が効いていない）

切り分け（管理コンソールで入れた場合）：

```sh
ip a
ip r
ps w | grep telnetd | grep -v grep || true
cat /etc/umu/network.conf
```

## 6.2 initramfs で root が見つからない（switch_rootに到達しない）

原因候補：
- `root=UUID=...` のUUIDが、実際のブロックデバイスUUIDと一致していない
- ブロックデバイス名が `vd* / sd* / nvme*` 以外（特殊環境）

対策：
- Rocky通常起動で `blkid` し、UUIDを再確認
- UmuOS rootfs 用デバイスを一般的な形（例：/dev/vdb1）にする

## 6.3 ロールバックできない

対策（設計）：
- `grub2-reboot` のワンショット運用を守る
- 管理コンソール/レスキュー経路を最初に確保

---

# 付録A. 旧案（参考）：Rocky上でQEMU起動してUmuOSへ入るプラン

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

