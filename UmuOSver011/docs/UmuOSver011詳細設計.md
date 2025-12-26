# UmuOS var0.1.1 詳細設計（改訂：再現性・観測性・失敗対策を仕様化）

この詳細設計は [UmuOSver011基本計画.md](../UmuOSver011基本計画.md) の受入基準を満たすための「作業手順＋仕様」のたたき台。

受入環境は **開発環境の QEMU** に限定する（virt-manager 等での起動可否は 0.1.1 の受入条件に含めない）。

実装の背景・根拠は同ディレクトリの [実装ノート](実装ノート.md) を参照する。

---

## 0. ゴール（受入基準）
1. UEFI → GRUB → Linux kernel 6.18.1 → initramfs（自作init）→ 永続 ext4（disk.img）へ `switch_root` が成立する
2. 最終的な `/` は ext4（disk.img）で、再起動してもファイル・設定が保持される
3. `root` / `tama` ユーザーでログインできる（BusyBox init/getty/login を利用）
4. 静的IP（DHCPなし）：`192.168.0.204/24`、GW `192.168.0.1`、DNS `8.8.8.8`
5. telnet（開発用途・必要時のみ）で接続し、loginできる
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
  cpio gzip xz-utils busybox-static e2fsprogs
```

### 3.2 ディレクトリ作成
```bash
mkdir -p ~/umu/umu_project/UmuOSver011/{kernel,initramfs,iso_root/boot/grub,logs,disk,run}
```

---

## 4. 永続ディスク（ext4の/）: disk.img

### 4.1 作成
まずは単純化のため「パーティション無し（ディスク全体が ext4）」で進める。
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

BusyBox applet（initramfs側）：
```bash
cd ~/umu/umu_project/UmuOSver011/initramfs/rootfs/bin
./busybox --install -s .
```

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

注意：この仕様により、initramfs段階では `mount UUID=...` に依存しない（前回の失敗を潰す）。

ビルド例：
```bash
cd ~/umu/umu_project/UmuOSver011/initramfs
mkdir -p src
# src/init.c を用意したら（別途実装）
gcc -static -Os -s -o rootfs/init src/init.c
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
cd /mnt/umuos011/bin
sudo ./busybox --install -s .
```

### 7.3 ユーザー/認証
`/etc/passwd`：/etcは755なのでsudo vim 。。。しないと書き込めないので注意
```text
root:x:0:0:root:/root:/bin/sh
tama:x:1000:1000:tama:/home/tama:/bin/sh
```

`/etc/shadow` は環境ごとに作成する（ハッシュを設計書に直書きしない）。
/etcは755なのでsudo vim 。。。しないと書き込めないので注意
ホストで生成例（SHA-512）：
```bash
openssl passwd -6
```

権限：
```bash
sudo chown root:root /mnt/umuos011/etc/passwd /mnt/umuos011/etc/shadow
sudo chmod 644 /mnt/umuos011/etc/passwd
sudo chmod 600 /mnt/umuos011/etc/shadow
```

### 7.4 BusyBox init（inittab/rcS）
`/etc/inittab`（最小）：　/etcは755なのでsudo vim 。。。しないと書き込めないので注意
```sh
::sysinit:/etc/init.d/rcS

ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
tty1::respawn:/sbin/getty 0 tty1 linux

::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a -r
```

`/etc/init.d/rcS`（最小：ログ + FS + ネット + 任意telnet）：　/etcは755なのでsudo vim 。。。しないと書き込めないので注意
```sh
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
```

権限：
```bash
sudo chown root:root /mnt/umuos011/etc/inittab /mnt/umuos011/etc/init.d/rcS
sudo chmod 644 /mnt/umuos011/etc/inittab
sudo chmod 755 /mnt/umuos011/etc/init.d/rcS
```

### 7.5 ネットワーク設定ファイル（ext4側）
`/etc/umu/network.conf`：
```text
IFNAME=eth0
IP=192.168.0.204/24
GW=192.168.0.1
DNS=8.8.8.8
TELNET_ENABLE=0
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

OVMFパスが分からない場合：
```bash
dpkg -L ovmf | grep -E 'OVMF_(CODE|VARS).*fd$'
```

例（存在するパスに合わせて調整）：
```bash
cp -n /usr/share/OVMF/OVMF_VARS_4M.fd ~/umu/umu_project/UmuOSver011/run/OVMF_VARS_umuos011.fd
```

### 10.2 QEMU（ネット無し・観測性最大）
```bash
cd ~/umu/umu_project/UmuOSver011

qemu-system-x86_64 -m 2048 -smp 2 -machine q35,accel=kvm -cpu host \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=run/OVMF_VARS_umuos011.fd \
  -cdrom UmuOSver011-boot.iso -boot d \
  -drive file=disk/disk.img,if=virtio,format=raw \
  -nic none \
  -nographic \
  -serial mon:stdio
```

観測ポイント：
- GRUBメニューが出る
- kernel log が `ttyS0` に流れる
- initramfs の自作 init が UUID を解決して mount → switch_root できる

### 10.3 QEMU（ブリッジ：静的IP/telnet確認）
```bash
cd ~/umu/umu_project/UmuOSver011

qemu-system-x86_64 -m 2048 -smp 2 -machine q35,accel=kvm -cpu host \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=run/OVMF_VARS_umuos011.fd \
  -cdrom UmuOSver011-boot.iso -boot d \
  -drive file=disk/disk.img,if=virtio,format=raw \
  -nic bridge,br=br0,model=virtio-net-pci \
  -serial mon:stdio
```

`br0` は環境のブリッジ名に合わせて変更する。

注意：静的IP（192.168.0.204/24）を成立させるため、user networking（NAT）ではなく L2 ブリッジ接続を前提とする。

---

## 11. 動作確認（受入基準の確認手順）

この章は [UmuOSver011基本計画.md](../UmuOSver011基本計画.md) の「成功条件（受入基準）」と同じ観点で確認する。

### 11.1 永続化
1. `/home/tama/test.txt` を作成
2. 再起動
3. ファイルが残っている

### 11.2 ログ
- `/logs/boot.log` が存在し、再起動後も追記される

### 11.3 ネットワーク（静的IP）
- `ip addr` / `ip route` で `192.168.0.204/24` と default route が入っている

### 11.4 telnet
- `TELNET_ENABLE=1` の時のみ `telnet 192.168.0.204` で接続でき、loginできる

---

## 12. トラブルシュート（症状 → まず確認 → 対処）

### 12.1 GRUBが出ない
- まず確認：ISO作成が成功しているか、UEFI起動になっているか
- 対処：`grub-mkrescue` の依存（`xorriso` 等）導入を確認

### 12.2 kernel は起動するが root をマウントできない
- まず確認：disk.img の UUID と `root=UUID=...` の一致
- まず確認：initramfsで `/dev` が見えているか（devtmpfsをマウントしているか）
- 対処：自作 init が UUID→デバイス解決できるよう、候補デバイスの範囲と ext4 UUID 読み取りを見直す

### 12.3 switch_root 後に何も起きない
- まず確認：ext4側に `/sbin/init` があるか（symlink含む）
- まず確認：`/etc/inittab` と `/etc/init.d/rcS` があり、実行権限があるか

---

## 13. 0.1.1 でのスコープ（やらないこと）
- systemd導入
- initramfs での複雑なユーザーランド（udev等）
- BusyBox からの全面脱却（0.2以降）　
```


