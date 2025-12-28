# UmuOS ver0.1.1 詳細設計（改訂：再現性・観測性・失敗対策を仕様化）

この詳細設計は [UmuOSver011基本計画.md](../UmuOSver011基本計画.md) の受入基準を満たすための「作業手順＋仕様」のたたき台。

受入環境は **開発環境の QEMU** に限定する（virt-manager 等での起動可否は 0.1.1 の受入条件に含めない）。

実装の背景・根拠は同ディレクトリの [実装ノート](実装ノート.md) を参照する。

---

## 用語
- 開発マシン側：QEMU を実行し、ISO作成・disk.img編集・br0設定などを行う Ubuntu 24.04 LTS 環境
- UmuOS側：QEMU 上で起動するゲストOS（Linux kernel + initramfs + ext4 ルート）

## 0. ゴール（受入基準）
1. UEFI → GRUB → Linux kernel 6.18.1 → initramfs（自作init）→ 永続 ext4（disk.img）へ `switch_root` が成立する
2. 最終的な `/` は ext4（disk.img）で、再起動してもファイル・設定が保持される
3. `root` / `tama` ユーザーでログインできる（BusyBox init/getty/login を利用）
4. ネットワーク到達性（開発用途）は、次のいずれかで成立する
  - **推奨（安定）**：QEMU user networking（NAT）+ port forward により、開発マシンから telnet で接続し login できる
  - **任意（LAN直結が必要な場合のみ）**：静的IP（DHCPなし）：`192.168.0.204/24`、GW `192.168.0.1`、DNS `8.8.8.8` で起動し、同一LANから telnet で接続し login できる
6. `/logs/boot.log` が永続化され、再起動後も追記される

成功＝「QEMU で再現でき、上記がすべて満たされる」

---

## 1. 前回の失敗からの教訓（仕様として固定する）

### 1.1 失敗しやすい点（トップ5）
1) `root=UUID=...` と disk.img の UUID が一致していない

2) initramfs 環境で **UUID指定マウントが成立しない**
- BusyBox の `mount` だけでは `UUID=...` を自動解決できないことがある（udev なし、`/dev/disk/by-uuid` が無い等）

3) `/dev` が用意できておらず、ブロックデバイスが見えない
- `devtmpfs` が無い/マウントしていないと、`/dev/vda` などが存在せずマウント不能

4) 観測性が足りず「止まったように見える」
- `console=ttyS0` が無い、QEMU側でシリアルを見ていない

5) 起動方法やデバイスモデルでデバイス名が変わる
- `/dev/vda` 固定は壊れやすい。設計は UUID に寄せる

### 1.2 対策（0.1.1での決定事項）
- kernel cmdline は `root=UUID=<UUID>` を渡す（デバイス名依存を捨てる）
- initramfs の自作 init は「UUID → 実デバイス名」を **自前で解決**して mount する
  - つまり `mount UUID=...` には依存しない（udev不要・再現性重視）
- 観測性のため `console=tty0 console=ttyS0,115200n8` を固定し、QEMUはシリアル出力で起動ログを見る
- 本開発環境の QEMU は **ソフトウェアエミュレーション（`accel=tcg`）を前提**とし、KVM（ネストKVMや `/dev/kvm` の有無）は考慮対象外とする

---

## 2. 変数（ここだけ変えれば流用できる）
- 作業ルート：`~/umu/umu_project/UmuOSver011`
- Kernel：`6.18.1`
- ISO：`UmuOSver011-boot.iso`
- 永続ディスク：`disk/disk.img`
- initramfs：`initramfs/initrd.img-6.18.1`
- GRUB設定：`iso_root/boot/grub/grub.cfg`

---

## 3. 環境準備（最初に一度だけ）

### 3.1 必要パッケージ
```bash
sudo apt update
sudo apt install -y build-essential bc bison flex libssl-dev \
  libelf-dev libncurses-dev dwarves git wget \
  grub-efi-amd64-bin grub-common xorriso mtools \
  qemu-system-x86 ovmf \
  cpio gzip xz-utils busybox-static e2fsprogs musl-tools
```

### 3.2 ディレクトリ作成
```bash
mkdir -p ~/umu/umu_project/UmuOSver011/{kernel,initramfs,iso_root/boot/grub,logs,disk,run}
```

### 3.3 ブリッジ（br0）準備（静的IP/telnet検証用）

位置づけ：この章は **必須の事前準備ではない**（ネットワーク検証が必要なときのみ）。

- まずは 4〜10.2（ネット無し）で「UEFI→GRUB→kernel→initramfs→ext4→switch_root」まで成立させる
- ブリッジ（LAN直結）による静的IP/telnet の検証に進む段階（10.4 を実行する直前）で、この 3.3 を実施する
- NAT（10.3）での検証だけなら、この 3.3 は不要

目的：QEMU の `-nic bridge,br=br0` を使い、UmuOS側を LAN と同一 L2 に接続する。

前提：開発環境は Ubuntu 24.04 LTS（ProxmoxVE 9.1 上の VM）。Proxmox 側はブリッジ `vmbr0` を使用している。

注意：この作業は開発マシン側（Ubuntu）のネットワーク構成を変更するため、SSH 作業中だと切断リスクがある。Proxmox のコンソール等、復旧できる経路を確保してから実施する。

1) Ubuntu 側の物理NIC名を確認（例：`ens18`）
```bash
ip -br link
ip route
```

このプロジェクトの開発環境例：NIC は `ens18`、開発マシン側IP は `192.168.0.201/24`。

2) netplan で `br0` を作成（開発マシン側も静的IPの例）

```bash
sudo tee /etc/netplan/01-br0.yaml >/dev/null <<'EOF'
network:
  version: 2
  renderer: networkd
  ethernets:
    ens18:
      dhcp4: no
  bridges:
    br0:
      interfaces: [ens18]
      dhcp4: no
      addresses: [192.168.0.201/24]
      routes:
        - to: default
          via: 192.168.0.1
      nameservers:
        addresses: [8.8.8.8]
      parameters:
        stp: false
        forward-delay: 0
EOF

sudo netplan apply
```

開発マシン側も静的IPで固定する（DHCPは使わない）。`addresses` / `routes` / `nameservers` は環境に合わせて調整する。

3) `br0` ができたことを確認
```bash
ip -br link show br0
ip addr show br0
bridge link
```

補足：QEMU ブリッジ接続が権限で失敗する場合
- 症状例：`-nic bridge,br=br0` で `Operation not permitted` や bridge helper 関連エラー
- 切り分け：まずは `sudo qemu-system-x86_64 ...` で起動できるか確認する
- 恒久対応（推奨）：`qemu-bridge-helper` に `br0` を許可する

```bash
sudo install -d -m 755 /etc/qemu
echo 'allow br0' | sudo tee /etc/qemu/bridge.conf
sudo chmod 644 /etc/qemu/bridge.conf
```

4) Proxmox 側の注意（必要時のみ）
- Proxmox の VM の NIC が `vmbr0` に接続されていること
- Proxmox の firewall を有効化している場合、ブリッジ配下で複数MACが流れる構成（ネストブリッジ）が止められることがある。
  その場合は firewall 設定を見直す（まずは検証のために無効化して切り分ける）。

### 3.3.1 障害時メモ（Proxmox の `vmbr0` がハングした疑い）

想定：開発マシン（Ubuntu VM）で br0 を作った／QEMU を `-nic bridge,br=br0` で起動した後に、Proxmox 側の `vmbr0` 配下で通信断や高負荷が発生した。

最優先：**SSH だけで復旧しようとしない**。必ず Proxmox のローカルコンソール（datacenter/ノードのコンソール、IPMI、物理コンソール）を確保してから実施する。

#### 1) まず採取（原因追跡用）
Proxmox ホスト上で、状態を残す（復旧の前に最低限だけでも）。

```bash
date
ip -br link
ip -br addr
ip route
bridge link
ip -s link show vmbr0
dmesg -T | tail -n 200
```

#### 2) 影響を止める（ループ/ストーム疑いのとき）
- 直前に追加/変更した VM（特にブリッジやL2を喋る VM）を一旦停止する（Ubuntu開発VM、テスト中のゲストなど）
- Proxmox の firewall が絡む疑いがある場合は、切り分けとして一時停止する

```bash
systemctl stop pve-firewall
```

#### 3) 最小の復旧操作（順番固定）
ifupdown2 を前提に、まずは設定適用のやり直しを試す。

```bash
ifreload -a
```

改善しない場合（※この操作でネットワーク断が起きうるため、必ずコンソールから実施）：

```bash
ifdown vmbr0
ifup vmbr0
```

#### 4) 再発防止（まずは安全側）
- L2 ループを疑う場合：Proxmox 側の `vmbr0` / Ubuntu 側の `br0` で STP を有効化するのを検討する（検証環境でのみ）
- 検証の基本は「ネット無しで受入（4〜10.2）→NAT（10.3）→（必要時のみ）ブリッジ（10.4）」を厳守する
- br0/ブリッジが不安定なら、QEMU は user-mode NAT（ポートフォワード）で代替し、`vmbr0` を触らない

---

## 4. 永続ディスク（ext4の/）: disk.img

### 4.1 作成
まずは単純化のため「パーティション無し（ディスク全体が ext4）」で進める。

補足：パーティション無し＝`mount -o loop disk.img ...` でOK（`fdisk` は不要）。
```bash
cd ~/umu/umu_project/UmuOSver011/disk
truncate -s 20G disk.img
mkfs.ext4 -F disk.img
```

### 4.2 UUID取得（最重要）
```bash
cd ~/umu/umu_project/UmuOSver011/disk
sudo blkid -p -o value -s UUID disk.img
```

失敗する場合：
```bash
sudo dumpe2fs -h disk.img | grep -E '^Filesystem UUID:'
```

この UUID を後で `grub.cfg` の `root=UUID=...` に必ず反映する。

---

## 5. kernel（6.18.1）

### 5.1 取得
```bash
cd ~/umu/umu_project/UmuOSver011/kernel
wget -nc https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.1.tar.xz
tar -xf linux-6.18.1.tar.xz
cd linux-6.18.1
```

### 5.2 設定とビルド
0.1.1はまず defconfig で進め、起動できない場合のみ config を見直す（追加の章に切り分け）。
```bash
make mrproper
make defconfig
make -j"$(nproc)"
```

#### Kernel config 必須チェック（initramfs使用時）

initramfs から ext4 を mount するため、最低限以下が built-in（`=y`）になっていることを確認する。

- `CONFIG_EXT4_FS=y`
- `CONFIG_VIRTIO=y`
- `CONFIG_VIRTIO_PCI=y`
- `CONFIG_VIRTIO_BLK=y`
- `CONFIG_DEVTMPFS=y`
- `CONFIG_DEVTMPFS_MOUNT=y`
- `CONFIG_BLK_DEV_INITRD=y`
- `CONFIG_RD_GZIP=y`（initramfs を gzip 圧縮している場合）

確認（例）：
```bash
grep -E '^(CONFIG_EXT4_FS|CONFIG_VIRTIO|CONFIG_VIRTIO_PCI|CONFIG_VIRTIO_BLK|CONFIG_DEVTMPFS|CONFIG_DEVTMPFS_MOUNT|CONFIG_BLK_DEV_INITRD|CONFIG_RD_GZIP)=' .config
```

もし `=m` や未設定なら、必要最小限で `make menuconfig` で `=y` に変更して再ビルドする。

### 5.3 ISO側へ配置
```bash
cp arch/x86/boot/bzImage ~/umu/umu_project/UmuOSver011/iso_root/boot/vmlinuz-6.18.1
cp .config ~/umu/umu_project/UmuOSver011/iso_root/boot/config-6.18.1
```

### 5.4 検証
```bash
file ~/umu/umu_project/UmuOSver011/iso_root/boot/vmlinuz-6.18.1
```
出力想定：vmlinuz-6.18.1: Linux kernel x86 boot executable bzImage, version 6.18.1 (...)

---

## 6. initramfs（移行専用）

0.1.1の initramfs は「永続 ext4 を `/` として使う」ための移行専用。
ログイン・ネット初期化・telnetは switch_root 後（ext4側の `/sbin/init`）で行う。
永続ログ（`/logs/boot.log`）は ext4 を `/` としてマウント後に書く（initramfs 単体での永続化は狙わない）。

### 6.1 ルートFS骨格
```bash
cd ~/umu/umu_project/UmuOSver011/initramfs
mkdir -p rootfs/{bin,sbin,etc,proc,sys,dev,dev/pts,run,newroot}
cp /bin/busybox rootfs/bin/
sudo chown root:root rootfs/bin/busybox
sudo chmod 755 rootfs/bin/busybox
```

BusyBox は `busybox-static` 前提（initramfs 内で動的リンクだと動かないため）。

開発マシン側で確認：
```bash
file /bin/busybox
```

BusyBox applet（initramfs側）：
```bash
cd ~/umu/umu_project/UmuOSver011/initramfs/rootfs/bin
sudo ln -sf busybox switch_root
```

注意：`busybox --install -s .` をそのまま実行すると、環境によっては「ホスト側の絶対パス」を指す symlink が大量に作られ、initramfs 起動時に `ENOENT` で壊れることがある。

0.1.1 の initramfs で最低限必要なのは `/bin/switch_root` のため、まずは上記の相対リンクで確実に用意する。

### 6.2 自作 init（C言語）仕様（ここが0.1.1の中核）

要件（最小）：
1. `devtmpfs`/`proc`/`sysfs`/`devpts` をマウントする（`/dev` が無いとデバイス探索できない）
2. `/proc/cmdline` から `root=UUID=...` を取得する
3. **UUID → ブロックデバイスを自前で探索**する（udev不要）
   - 候補：`/dev/vd*`, `/dev/sd*`, `/dev/nvme*n*` 等
   - ext4 スーパーブロックから UUID を読んで一致するデバイスを選ぶ
4. 一致したデバイスを `mount -t ext4 -o rw <device> /newroot` でマウントする（リトライ付き）
5. `/newroot/logs/boot.log` に簡易ログを書ける範囲で追記する
6. `switch_root /newroot /sbin/init`

実装仕様（つまずき防止として必須）：
- 候補デバイスは最低限 `vd*` / `sd*` / `nvme*n*` を走査し、走査したデバイス名をシリアルへ出す
- `/proc/cmdline` の `root=UUID=...` は 36文字（ハイフンあり）を想定し、16バイトへ正しく変換して比較する
- 一致した場合は「見つけたデバイス名」と「そのデバイスから読めたUUID」をシリアルへ出す
- 失敗時も、最後に「見つからなかったUUID」と「走査した候補」をシリアルへ出して停止する
- `switch_root` は PATH に依存せず、`/bin/switch_root` をフルパスで `exec` する

注意：この仕様により、initramfs段階では `mount UUID=...` に依存しない（前回の失敗を潰す）。

ビルド例：
```bash
cd ~/umu/umu_project/UmuOSver011/initramfs
mkdir -p src
# src/init.c を用意したら（別途実装）
musl-gcc -static -Os -s -o rootfs/init src/init.c
sudo chown root:root rootfs/init
sudo chmod 755 rootfs/init
```

### 6.3 initramfs 作成
```bash
cd ~/umu/umu_project/UmuOSver011/initramfs/rootfs
find . -print0 | sudo cpio --null -o -H newc | gzip > ../initrd.img-6.18.1
cp ../initrd.img-6.18.1 ~/umu/umu_project/UmuOSver011/iso_root/boot/
```

検証：
```bash
ls -lh ~/umu/umu_project/UmuOSver011/iso_root/boot/initrd.img-6.18.1
```

任意（推奨）：initrd の中身（最低限 `/init` と `/bin/switch_root`）を軽く確認する。

`lsinitramfs` が使える場合（`initramfs-tools` 由来）：
```bash
lsinitramfs ~/umu/umu_project/UmuOSver011/initramfs/initrd.img-6.18.1 | head -n 30
```

`lsinitramfs` が無い場合（代替：gzip+cpio で一覧）：
```bash
cd ~/umu/umu_project/UmuOSver011/initramfs
zcat initrd.img-6.18.1 | cpio -t | head -n 30
```

追加確認（推奨）：`/bin/switch_root` が「相対リンク（`-> busybox`）」になっていること。
```bash
cd ~/umu/umu_project/UmuOSver011/initramfs
zcat initrd.img-6.18.1 | cpio -itv bin/switch_root
```

---

## 7. ext4ルート（disk.img）へ rootfs を作成（switch_root後）

目的：switch_root後に動く「本物の `/`」を disk.img に用意する。

### 7.1 マウント
```bash
sudo mkdir -p /mnt/umuos011
sudo mount -o loop ~/umu/umu_project/UmuOSver011/disk/disk.img /mnt/umuos011
```

### 7.2 最小rootfs（必須）
```bash
sudo mkdir -p /mnt/umuos011/{bin,sbin,etc,proc,sys,dev,dev/pts,run,root,home/tama,logs,etc/init.d,etc/umu}
sudo cp /bin/busybox /mnt/umuos011/bin/
sudo chown root:root /mnt/umuos011/bin/busybox
sudo chmod 755 /mnt/umuos011/bin/busybox

sudo ln -sf /bin/busybox /mnt/umuos011/sbin/init
```

BusyBox applet（ext4側）：
```bash
# 重要：ホスト側（/mnt/umuos011/...）でそのまま --install すると、
# symlink が「/mnt/umuos011/bin/busybox」のような絶対パスになり、UmuOS起動時に壊れる。
# chroot して、UmuOS 側のパス（/bin/busybox）基準で symlink を作る。

sudo chroot /mnt/umuos011 /bin/busybox --install -s /bin
sudo chroot /mnt/umuos011 /bin/busybox --install -s /sbin

# 例：/bin/sh -> /bin/busybox になっていること
sudo ls -l /mnt/umuos011/bin/sh
```

### 7.3 ユーザー/認証
`/etc/passwd` を作成：
```bash
sudo tee /mnt/umuos011/etc/passwd >/dev/null <<'EOF'
root:x:0:0:root:/root:/bin/sh
tama:x:1000:1000:tama:/home/tama:/bin/sh
EOF
```

`/etc/shadow` は環境ごとに作成する（ハッシュを設計書に直書きしない）。

開発マシン側でパスワードハッシュを生成（SHA-512）：
```bash
openssl passwd -6
```

1) `root` 用ハッシュを1回生成して控える

2) `tama` 用ハッシュを1回生成して控える

3) `/mnt/umuos011/etc/shadow` を作成（`<ROOT_HASH>` / `<TAMA_HASH>` を置換）
```bash
sudo tee /mnt/umuos011/etc/shadow >/dev/null <<'EOF'
root:<ROOT_HASH>:0:0:99999:7:::
tama:<TAMA_HASH>:0:0:99999:7:::
EOF
```

権限：
```bash
sudo chown root:root /mnt/umuos011/etc/passwd /mnt/umuos011/etc/shadow
sudo chmod 644 /mnt/umuos011/etc/passwd
sudo chmod 600 /mnt/umuos011/etc/shadow
```

`/home/tama` の所有者（推奨）：
```bash
sudo chown -R 1000:1000 /mnt/umuos011/home/tama
```

### 7.4 BusyBox init（inittab/rcS）
`/etc/inittab` を作成（最小）：
```bash
sudo tee /mnt/umuos011/etc/inittab >/dev/null <<'EOF'
::sysinit:/etc/init.d/rcS

ttyS0::respawn:/bin/getty -L 115200 ttyS0 vt100

::ctrlaltdel:/bin/reboot
::shutdown:/bin/umount -a -r
EOF
```

方針：ver0.1.1 は `-nographic` 前提とし、ログインおよび受入テストの入口はシリアルコンソール（`ttyS0`）に固定する。

補足：BusyBox `login` の挙動差（rootログインが弾かれる場合）

環境/設定により、`/etc/securetty` が無いと root のログインが拒否されることがある。
その場合は以下を追加する（`ttyS0` のみ許可）：

```bash
sudo tee /mnt/umuos011/etc/securetty >/dev/null <<'EOF'
ttyS0
EOF
sudo chown root:root /mnt/umuos011/etc/securetty
sudo chmod 644 /mnt/umuos011/etc/securetty
```

`/etc/init.d/rcS` を作成（最小：ログ + FS + ネット + 任意telnet）：
```bash
sudo tee /mnt/umuos011/etc/init.d/rcS >/dev/null <<'EOF'
#!/bin/sh

PATH=/sbin:/bin

log() {
  echo "$1" >> /logs/boot.log
}

mkdir -p /logs
log "[rcS] boot: $(date)"

mkdir -p /proc /sys /dev /dev/pts
grep -q " /proc " /proc/mounts 2>/dev/null || mount -t proc proc /proc
grep -q " /sys "  /proc/mounts 2>/dev/null || mount -t sysfs sys /sys
grep -q " /dev "  /proc/mounts 2>/dev/null || mount -t devtmpfs dev /dev
grep -q " /dev/pts " /proc/mounts 2>/dev/null || mount -t devpts devpts /dev/pts

CONF=/etc/umu/network.conf
IFNAME=eth0
TELNET_ENABLE=0

if [ -f "$CONF" ]; then
  . "$CONF"
fi

if [ -n "$IP" ] && [ -n "$GW" ]; then
  ip link set "$IFNAME" up
  ip addr show dev "$IFNAME" | grep -Fq "inet $IP" || ip addr add "$IP" dev "$IFNAME"
  ip route replace default via "$GW" dev "$IFNAME"
  [ -n "$DNS" ] && echo "nameserver $DNS" > /etc/resolv.conf
  log "[rcS] net: IF=$IFNAME IP=$IP GW=$GW DNS=$DNS"
else
  log "[rcS] net: skip (IP/GW not set)"
fi

if [ "$TELNET_ENABLE" = "1" ]; then
  telnetd -l /bin/login
  log "[rcS] telnetd started"
fi
EOF
```

権限：
```bash
sudo chown root:root /mnt/umuos011/etc/inittab /mnt/umuos011/etc/init.d/rcS
sudo chmod 644 /mnt/umuos011/etc/inittab
sudo chmod 755 /mnt/umuos011/etc/init.d/rcS
```

### 7.5 ネットワーク設定ファイル（ext4側）
`/etc/umu/network.conf` を作成：

このファイルは「NAT（推奨）」と「ブリッジ（LAN直結）」で内容が変わる。

#### 7.5.1 NAT（推奨：安定運用）
QEMU user networking（NAT）のデフォルトサブネット（`10.0.2.0/24`）に合わせる。

```bash
sudo tee /mnt/umuos011/etc/umu/network.conf >/dev/null <<'EOF'
IFNAME=eth0
IP=10.0.2.15/24
GW=10.0.2.2
DNS=8.8.8.8
TELNET_ENABLE=0
EOF
```

#### 7.5.2 ブリッジ（LAN直結が必要な場合のみ）
同一LANに `192.168.0.204/24` を出す。

```bash
sudo tee /mnt/umuos011/etc/umu/network.conf >/dev/null <<'EOF'
IFNAME=eth0
IP=192.168.0.204/24
GW=192.168.0.1
DNS=8.8.8.8
TELNET_ENABLE=0
EOF
```

権限：
```bash
sudo chown root:root /mnt/umuos011/etc/umu /mnt/umuos011/etc/umu/network.conf
sudo chmod 755 /mnt/umuos011/etc/umu
sudo chmod 600 /mnt/umuos011/etc/umu/network.conf
```

### 7.6 アンマウント
```bash
sudo umount /mnt/umuos011
```

---

## 8. GRUB設定（ISO側）

作成先：`~/umu/umu_project/UmuOSver011/iso_root/boot/grub/grub.cfg`

ポイント：
- `root=UUID=...` は **initramfs の自作 init が読む**ための情報
- `console=tty0 console=ttyS0,115200n8` を固定（QEMUのシリアル出力で観測する）

`<DISK_UUID>` を 4.2 の UUID に置換：
```cfg
set timeout=20
set default=0

menuentry "UmuOS 0.1.1 kernel 6.18.1" {
  linux /boot/vmlinuz-6.18.1 ro root=UUID=<DISK_UUID> rootfstype=ext4 rootwait console=tty0 console=ttyS0,115200n8
  initrd /boot/initrd.img-6.18.1
}

menuentry "UmuOS 0.1.1 rescue (single)" {
  linux /boot/vmlinuz-6.18.1 ro single root=UUID=<DISK_UUID> rootfstype=ext4 rootwait console=tty0 console=ttyS0,115200n8
  initrd /boot/initrd.img-6.18.1
}
```

確認（推奨）：
```bash
grep -n 'root=UUID=' ~/umu/umu_project/UmuOSver011/iso_root/boot/grub/grub.cfg
sudo blkid -p -o value -s UUID ~/umu/umu_project/UmuOSver011/disk/disk.img
```

---

## 9. ISO作成
```bash
cd ~/umu/umu_project/UmuOSver011
grub-mkrescue -o UmuOSver011-boot.iso iso_root
ls -lh UmuOSver011-boot.iso
```

---

## 10. 起動テスト（まずはネット無しで成功させる）

### 10.1 OVMF VARS をプロジェクト内に固定（推奨）
`/tmp` は掃除されやすく、挙動が変わる原因になるのでプロジェクト内に置く。

このファイルが無いと、10.2 のQEMUコマンド（`-drive ...file=run/OVMF_VARS_umuos011.fd`）が起動直後に失敗する。

OVMFパスが分からない場合：
```bash
dpkg -L ovmf | grep -E 'OVMF_(CODE|VARS).*fd$'
```

例（存在するパスに合わせて調整）：
```bash
mkdir -p ~/umu/umu_project/UmuOSver011/run
cp --update=none /usr/share/OVMF/OVMF_VARS_4M.fd ~/umu/umu_project/UmuOSver011/run/OVMF_VARS_umuos011.fd
```

確認（推奨）：
```bash
ls -lh ~/umu/umu_project/UmuOSver011/run/OVMF_VARS_umuos011.fd
```

### 10.1.1 起動前チェックリスト（ここまでで「現状まで再現できた」を判定）

以下がすべて揃っていれば、10.2 に進んでよい。

1) 成果物の存在確認：
```bash
cd ~/umu/umu_project/UmuOSver011
ls -lh UmuOSver011-boot.iso \
  iso_root/boot/vmlinuz-6.18.1 \
  iso_root/boot/initrd.img-6.18.1 \
  iso_root/boot/grub/grub.cfg \
  run/OVMF_VARS_umuos011.fd \
  disk/disk.img
```

2) `root=UUID=...` と disk.img UUID の一致確認：
```bash
cd ~/umu/umu_project/UmuOSver011
grep -n 'root=UUID=' iso_root/boot/grub/grub.cfg
sudo blkid -p -o value -s UUID disk/disk.img
```

### 10.2 QEMU（ネット無し・観測性最大）
```bash
cd ~/umu/umu_project/UmuOSver011

qemu-system-x86_64 -m 2048 -smp 2 -machine q35,accel=tcg -cpu max \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=run/OVMF_VARS_umuos011.fd \
  -cdrom UmuOSver011-boot.iso -boot d \
  -drive file=disk/disk.img,if=virtio,format=raw \
  -nic none \
  -nographic \
  -serial stdio \
  -monitor none
```

観測ポイント：
- GRUBメニューが出る
- kernel log が `ttyS0` に流れる
- initramfs の自作 init が UUID を解決して mount → switch_root できる

ログイン/操作の入口：シリアルコンソール（`ttyS0`）。`-nographic` のため VGA 出力は前提にしない。

### 10.3 QEMU（NAT：推奨・安定運用）
```bash
cd ~/umu/umu_project/UmuOSver011

qemu-system-x86_64 -m 2048 -smp 2 -machine q35,accel=tcg -cpu max \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=run/OVMF_VARS_umuos011.fd \
  -cdrom UmuOSver011-boot.iso -boot d \
  -drive file=disk/disk.img,if=virtio,format=raw \
  -nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:2223-:23 \
  -nographic \
  -serial stdio \
  -monitor none
```

このモードでは、UmuOS は NAT 配下（例：`10.0.2.15/24`）で動作する。

telnet 接続（開発マシン上）：
```bash
telnet 127.0.0.1 2223
```

（運用案②）外部PCからアクセスしたい場合は「外部PC → UbuntuへSSH → Ubuntu上で telnet 127.0.0.1 2223」とする（telnet をLANに露出させない）。

### 10.4 QEMU（ブリッジ：LAN直結が必要な場合のみ）

注意：静的IP（`192.168.0.204/24`）を成立させるため、user networking（NAT）ではなく L2 ブリッジ接続を前提とする。

```bash
cd ~/umu/umu_project/UmuOSver011

qemu-system-x86_64 -m 2048 -smp 2 -machine q35,accel=tcg -cpu max \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=run/OVMF_VARS_umuos011.fd \
  -cdrom UmuOSver011-boot.iso -boot d \
  -drive file=disk/disk.img,if=virtio,format=raw \
  -nic bridge,br=br0,model=virtio-net-pci \
  -nographic \
  -serial stdio \
  -monitor none
```

`br0` は環境のブリッジ名に合わせて変更する。

---

## 11. 動作確認（受入基準の確認手順）

この章は [UmuOSver011基本計画.md](../UmuOSver011基本計画.md) の「成功条件（受入基準）」と同じ観点で確認する。

### 11.1 永続化
1. `/home/tama/test.txt` を作成
2. 再起動
3. ファイルが残っている

### 11.2 ログ
- `/logs/boot.log` が存在し、再起動後も追記される

### 11.3 ネットワーク（NAT/ブリッジ）
- NAT（推奨）：`ip addr` / `ip route` で `10.0.2.15/24` と `default via 10.0.2.2` が入っている
- ブリッジ（LAN直結）：`ip addr` / `ip route` で `192.168.0.204/24` と default route が入っている

### 11.4 telnet
- `TELNET_ENABLE=1` の時のみ接続でき、loginできる
  - NAT（推奨）：開発マシン上で `telnet 127.0.0.1 2223`
  - ブリッジ（LAN直結）：同一LANから `telnet 192.168.0.204`

telnet 接続の切り分け順序（固定）：
1) UmuOS側の状態：UmuOS側で `ip addr` / `ip route` を確認
2) NAT の場合：QEMU の `hostfwd` 設定があることを確認し、開発マシン上で `telnet 127.0.0.1 2223`
3) ブリッジの場合：L3疎通（ICMP）`ping 192.168.0.204` → L2疎通（ARP）→ telnet `192.168.0.204`

---

202512270902

