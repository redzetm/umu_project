---
title: UmuOS-0.1.4-base-stable 詳細設計書
date: 2026-01-25
base_design: "./UmuOS-0.1.4-base-stable-基本設計書.md"
status: draft
---

# UmuOS-0.1.4-base-stable 詳細設計書（手順：0.1.3互換 + ゲストtelnetd）

この文書は、UmuOS-0.1.4-base-stable を「最初から再構築できる」状態にするための作業手順を、コマンド中心でまとめる。

## 重要方針（短縮）

- 0.1.4 は **0.1.3 相当（switch_root / ttyS0+ttyS1 同時ログイン / boot.log）を壊さない**。
- 0.1.4 の追加は **ゲスト telnetd（TCP/23）** のみ。
- ビルド：Ubuntu 24.04。起動・受入：RockyLinux 9.7。
- Ubuntu/Rocky ともに恒久NW変更はしない（Rocky は tap を都度作るだけ）。

---

## 0. 固定パラメータ（この文書で固定する値）

- 作業ルート（Ubuntu）：`~/umu/umu_project/UmuOS-0.1.4-base-stable`
- 配置ルート（Rocky）：`/root/UmuOS-0.1.4-base-stable`
- Kernel version：`6.18.1`
- BusyBox version：`1.36.1`

- ISO：`UmuOS-0.1.4-boot.iso`
- initrd：`initrd.img-6.18.1`
- kernel：`vmlinuz-6.18.1`
- 永続ディスク：`disk/disk.img`（ext4、4GiB、UUID固定）

- rootfs UUID（固定）：`9f5a1e4f-19b2-4d1f-9a6e-0d2a59e2a0d4`

- UmuOS（ゲスト）IP：`192.168.0.202/24`
- GW：`192.168.0.1`
- TAP 名：`tap-umu`
- ttyS1 TCP シリアル：`127.0.0.1:5555`

---

## 1. 事前準備（Ubuntu 24.04 LTS）

### 1.1 必要パッケージ

```bash
sudo apt update
sudo apt install -y \
build-essential bc bison flex libssl-dev libelf-dev libncurses-dev dwarves \
git wget rsync \
grub-efi-amd64-bin grub-common xorriso mtools \
cpio gzip xz-utils \
e2fsprogs \
musl-tools util-linux \
openssl telnet netcat-openbsd
```

### 1.2 コマンド存在確認（観測点）

```bash
command -v grub-mkrescue
command -v mkfs.ext4
command -v tune2fs
command -v musl-gcc
command -v cpio
command -v gzip
```

---

## 2. 作業ディレクトリ作成

```bash
mkdir -p ~/umu/umu_project/UmuOS-0.1.4-base-stable
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable

mkdir -p kernel/build \
initramfs/src initramfs/rootfs \
initramfs/busybox \
iso_root/boot/grub \
disk run logs docs
```

観測点：

```bash
ls -la
```

---

## 3. external の確認（入力ソース）

このプロジェクトでは入力ソースは `~/umu/umu_project/external/` を参照する。

```bash
test -f ~/umu/umu_project/external/linux-6.18.1-kernel/Makefile && echo "OK: linux-6.18.1-kernel" || echo "NG: linux-6.18.1-kernel"
test -f ~/umu/umu_project/external/busybox-1.36.1/Makefile && echo "OK: busybox-1.36.1" || echo "NG: busybox-1.36.1"
```

メモ：`test -f ...` は成功しても何も表示しない（結果は終了ステータス）ため、上のように `OK/NG` を表示させる。

- 両方 `OK` なら：取得は不要（次の章へ進む）
- `NG` が出たら：`~/umu/umu_project/external/` 配下に取得してから進む（取得方法は環境に合わせてよい）

---

## 4. Kernel（6.18.1）ビルド（out-of-tree）

out-of-tree の出力先は **作業ルート配下に固定**する。

### 4.1 defconfig

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable

make -C ~/umu/umu_project/external/linux-6.18.1-kernel \
mrproper

rm -rf ~/umu/umu_project/UmuOS-0.1.4-base-stable/kernel/build
mkdir -p ~/umu/umu_project/UmuOS-0.1.4-base-stable/kernel/build

make -C ~/umu/umu_project/external/linux-6.18.1-kernel \
O=~/umu/umu_project/UmuOS-0.1.4-base-stable/kernel/build defconfig
```

メモ：もし `defconfig` で `The source tree is not clean, please run 'make mrproper'` が出たら、上の `mrproper` を **Oなし**で実行できているかを確認する（`O=... mrproper` だけだと解消しないことがある）。

### 4.2 必須設定の確認（観測点）

まずは確認する（足りない場合のみ次項で補う）。

```bash
grep -E '^(CONFIG_EXT4_FS=|CONFIG_DEVTMPFS=|CONFIG_DEVTMPFS_MOUNT=|CONFIG_BLK_DEV_INITRD=|CONFIG_VIRTIO=|CONFIG_VIRTIO_PCI=|CONFIG_VIRTIO_BLK=|CONFIG_VIRTIO_NET=|CONFIG_NET=|CONFIG_INET=|CONFIG_SERIAL_8250=|CONFIG_SERIAL_8250_CONSOLE=|CONFIG_DEVPTS_FS=|CONFIG_UNIX98_PTYS=|CONFIG_RD_GZIP=)' \
~/umu/umu_project/UmuOS-0.1.4-base-stable/kernel/build/.config
```

期待（最低限）：上記が `=y`。

### 4.3 足りない場合のみ scripts/config で `=y` に揃える

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable

~/umu/umu_project/external/linux-6.18.1-kernel/scripts/config \
--file ~/umu/umu_project/UmuOS-0.1.4-base-stable/kernel/build/.config \
-e DEVTMPFS \
-e DEVTMPFS_MOUNT \
-e BLK_DEV_INITRD \
-e EXT4_FS \
-e VIRTIO \
-e VIRTIO_PCI \
-e VIRTIO_BLK \
-e VIRTIO_NET \
-e NET \
-e INET \
-e SERIAL_8250 \
-e SERIAL_8250_CONSOLE \
-e DEVPTS_FS \
-e UNIX98_PTYS \
-e RD_GZIP

make -C ~/umu/umu_project/external/linux-6.18.1-kernel \
O=~/umu/umu_project/UmuOS-0.1.4-base-stable/kernel/build olddefconfig
```

### 4.4 ビルド

```bash
make -C ~/umu/umu_project/external/linux-6.18.1-kernel \
O=~/umu/umu_project/UmuOS-0.1.4-base-stable/kernel/build -j"$(nproc)"
```

### 4.5 ISO入力へ配置

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable
cp -f kernel/build/arch/x86/boot/bzImage iso_root/boot/vmlinuz-6.18.1
cp -f kernel/build/.config iso_root/boot/config-6.18.1
file iso_root/boot/vmlinuz-6.18.1
```

---

## 5. BusyBox（1.36.1）ビルド（静的リンク）

external は参照のみ。作業用コピーを作ってビルドする。

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable

rm -rf initramfs/busybox/work
mkdir -p initramfs/busybox/work
rsync -a --delete ~/umu/umu_project/external/busybox-1.36.1/ initramfs/busybox/work/

cd initramfs/busybox/work
make distclean
make defconfig
```

### 5.1 設定（手動でOK：menuconfig）

この段階の粒度では、BusyBox の `.config` は「手で設定して保存」でよい。

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable/initramfs/busybox/work
make menuconfig
```

必須（最低限）：

- `CONFIG_STATIC=y`
- `CONFIG_INIT=y`
- `CONFIG_FEATURE_USE_INITTAB=y`
- `CONFIG_GETTY=y`
- `CONFIG_SWITCH_ROOT=y`
- `CONFIG_TELNETD=y`
- `CONFIG_FEATURE_TELNETD_STANDALONE=y`
- `CONFIG_LOGIN=y`
- `CONFIG_IP=y`
- `CONFIG_NC=y`

注意（ビルドが通らない場合の典型）：

- Ubuntu のヘッダ環境によっては `tc`（`CONFIG_TC`）が `TCA_CBQ_MAX` 等の未定義でコンパイルエラーになることがある。
- このプロジェクトでは `tc` は必須ではないため、（エラーが出たら）`Networking Utilities` → `tc (8.3 kb)` を `N` にして回避する。
- 0.1.4-base-stable の Ubuntu 24.04 実測でも、`CONFIG_TC=y` だと `networking/tc.c` で停止し、`CONFIG_TC=n` で `busybox` が生成された。

menuconfig 上の場所（BusyBox 1.36.1 / `Config.in` 由来）：

- `CONFIG_STATIC`：`Settings` → `Build Options` → `Build static binary (no shared libs)`
- `CONFIG_INIT`：`Init Utilities` → `init (10 kb)`
- `CONFIG_FEATURE_USE_INITTAB`：`Init Utilities` → `Support reading an inittab file`
- `CONFIG_GETTY`：`Login/Password Management Utilities` → `getty (10 kb)`
- `CONFIG_LOGIN`：`Login/Password Management Utilities` → `login (24 kb)`
- `CONFIG_SWITCH_ROOT`：`Linux System Utilities` → `switch_root (5.5 kb)`
- `CONFIG_TELNETD`：`Networking Utilities` → `telnetd (12 kb)`
- `CONFIG_FEATURE_TELNETD_STANDALONE`：`Networking Utilities` → `telnetd` 配下 → `Support standalone telnetd (not inetd only)`
- `CONFIG_IP`：`Networking Utilities` → `ip (35 kb)`
- `CONFIG_NC`：`Networking Utilities` → `nc (11 kb)`
- `CONFIG_TC`：`Networking Utilities` → `tc (8.3 kb)`（※必須ではない。ビルドが通らない場合は `N` 推奨）

探し方（ブラックボックス化しない）：

- `make menuconfig` 中に `/`（検索）を押して、シンボル名で検索する（例：`STATIC` / `TELNETD` / `SWITCH_ROOT`）。
- だいたい `CONFIG_` は付けない（`STATIC` のように入力する）。

保存後：

```bash
# BusyBox には kernel のような "olddefconfig" ターゲットが無い。
# 既存 .config を元に、追加/新規項目の整合を取るのは oldconfig。
make oldconfig

# （非対話で進めたい場合のみ）新規質問を全部デフォルトで進める：
# - `make oldconfig` 自体は必要（整合を取る工程）
# - ただし、回答を手で選ぶか/全部デフォルトで流すかは任意
# yes "" | make oldconfig
cp -f .config ~/umu/umu_project/UmuOS-0.1.4-base-stable/initramfs/busybox/config-1.36.1
```

### 5.2 ビルドと簡易検査

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable/initramfs/busybox/work
make -j"$(nproc)"
file busybox

# 期待値："statically linked" が出る（CONFIG_STATIC=y）

./busybox ip link
./busybox ip addr
./busybox ip route

# applet が「入っている」確認（ここで NG なら設定が不足している）
./busybox --list | grep -E '^(ip|telnetd|login|nc)$' || echo NG

# 実行できる確認（失敗を握りつぶさない。rc=0 が期待値）
./busybox telnetd --help >/dev/null 2>&1; echo "telnetd_rc=$?"
./busybox login --help >/dev/null 2>&1; echo "login_rc=$?"
./busybox nc -h >/dev/null 2>&1; echo "nc_rc=$?"
```

---

## 6. initramfs（initrd.img-6.18.1）生成

0.1.3 と同様に、initramfs は `switch_root` を成立させるための最小構成とする。

### 6.1 initramfs rootfs 作成（BusyBox + applet）

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable

rm -rf initramfs/rootfs
mkdir -p initramfs/rootfs/{bin,sbin,etc,proc,sys,dev,dev/pts,run,newroot,tmp}

cp -f initramfs/busybox/work/busybox initramfs/rootfs/bin/busybox
chmod 755 initramfs/rootfs/bin/busybox

sudo chroot initramfs/rootfs /bin/busybox --install -s /bin
sudo chroot initramfs/rootfs /bin/busybox --install -s /sbin

ls -l initramfs/rootfs/bin/switch_root
ls -l initramfs/rootfs/sbin/getty
ls -l initramfs/rootfs/sbin/telnetd
ls -l initramfs/rootfs/bin/login
```

### 6.2 initramfs `/init`（0.1.3 を流用）

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable
cp -f ~/umu/umu_project/UmuOS-0.1.3/initramfs/src/init.c initramfs/src/init.c

musl-gcc -static -O2 -Wall -Wextra -o initramfs/rootfs/init initramfs/src/init.c
chmod 755 initramfs/rootfs/init
file initramfs/rootfs/init
```

### 6.3 initrd 作成（cpio+gzip）

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable/initramfs

rm -f initrd.filelist0 initrd.cpio initrd.cpio.list initrd.img-6.18.1

# NOTE: 成果物（initrd.*）は initramfs/ 配下に固定する。
# filelist も initramfs/ に生成する（../ 参照による混乱を防ぐ）。
find rootfs -mindepth 1 -printf '%P\0' > initrd.filelist0

# NOTE: これはファイルへ書き込むだけなので、画面には何も出ない（無音が正常）。
# 期待値：`echo $?` が 0。
# ここで ^C した場合は filelist が途中なので作り直す。

# NOTE: cpio は rootfs を基準に実行する（initramfs で実行すると etc/tmp を見失って stat エラーになる）。
cd rootfs
cpio --null -ov --format=newc < ../initrd.filelist0 > ../initrd.cpio

cd ..
cpio -t < initrd.cpio > initrd.cpio.list
grep -E '^(init|bin/switch_root)$' initrd.cpio.list
gzip -9 -c initrd.cpio > initrd.img-6.18.1

cp -f initrd.img-6.18.1 ../iso_root/boot/initrd.img-6.18.1
```

---

## 7. 永続ディスク（disk/disk.img）作成と rootfs 投入

### 7.1 作成（ext4、固定UUID）

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable/disk
rm -f disk.img
truncate -s 4G disk.img
mkfs.ext4 -F -U 9f5a1e4f-19b2-4d1f-9a6e-0d2a59e2a0d4 disk.img
sudo blkid -p -o value -s UUID disk.img
```

### 7.2 マウント（loop）

```bash
sudo mkdir -p /mnt/umuos014
sudo mount -o loop ~/umu/umu_project/UmuOS-0.1.4-base-stable/disk/disk.img /mnt/umuos014
findmnt /mnt/umuos014
```

### 7.3 最小 rootfs

```bash
sudo mkdir -p /mnt/umuos014/{bin,sbin,etc,proc,sys,dev,dev/pts,run,var,var/run,home,root,tmp,logs,etc/init.d,etc/umu}
```

#### 7.3.1 BusyBox 配置（ext4側）

```bash
sudo cp -f ~/umu/umu_project/UmuOS-0.1.4-base-stable/initramfs/busybox/work/busybox /mnt/umuos014/bin/busybox
sudo chown root:root /mnt/umuos014/bin/busybox
sudo chmod 755 /mnt/umuos014/bin/busybox

sudo chroot /mnt/umuos014 /bin/busybox --install -s /bin
sudo chroot /mnt/umuos014 /bin/busybox --install -s /sbin
sudo ln -sf /bin/busybox /mnt/umuos014/sbin/init
sudo ls -l /mnt/umuos014/sbin/init
```

#### 7.3.2 inittab（ttyS0/ttyS1）

```bash
sudo tee /mnt/umuos014/etc/inittab >/dev/null <<'EOF'
::sysinit:/etc/init.d/rcS

ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
ttyS1::respawn:/sbin/getty -L 115200 ttyS1 vt100

::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a
EOF
```

#### 7.3.3 network.conf（固定）

```bash
sudo tee /mnt/umuos014/etc/umu/network.conf >/dev/null <<'EOF'
IFNAME=eth0
MODE=static
IP=192.168.0.202/24
GW=192.168.0.1
DNS=192.168.0.1
EOF
```

#### 7.3.4 securetty（固定）

```bash
sudo tee /mnt/umuos014/etc/securetty >/dev/null <<'EOF'
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
EOF
```

#### 7.3.5 rcS（mount / boot.log / network / telnetd）

```bash
sudo tee /mnt/umuos014/etc/init.d/rcS >/dev/null <<'EOF'
#!/bin/sh

export PATH=/sbin:/bin

mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mount -t devpts devpts /dev/pts 2>/dev/null || true

mkdir -p /logs /var/run 2>/dev/null || true
: > /var/run/utmp 2>/dev/null || true

# 永続ログ
(
UPTIME_S="$(cut -d' ' -f1 /proc/uptime 2>/dev/null || echo '?')"
BOOT_ID="$(cat /proc/sys/kernel/random/boot_id 2>/dev/null || echo '?')"
{
echo ""
echo "===== boot begin (boot_id=${BOOT_ID} uptime_s=${UPTIME_S}) ====="
echo "[cmdline]"; cat /proc/cmdline 2>/dev/null || true
echo "[mount]"; mount 2>/dev/null || true
echo "[ip link]"; ip link 2>/dev/null || true
echo "[ip addr]"; ip addr 2>/dev/null || true
echo "[ip route]"; ip route 2>/dev/null || true
echo "===== boot end ====="
} >> /logs/boot.log
) 2>/dev/null || true

# ネットワーク初期化
(
CONF=/etc/umu/network.conf
[ -f "$CONF" ] && . "$CONF"
IFNAME="${IFNAME:-eth0}"
if [ "${MODE:-}" = "static" ] && [ -n "${IP:-}" ] && [ -n "${GW:-}" ]; then
ip link set dev "$IFNAME" up 2>/dev/null || true
ip addr add "$IP" dev "$IFNAME" 2>/dev/null || true
ip route replace default via "$GW" dev "$IFNAME" 2>/dev/null || true
fi
) 2>/dev/null || true

# telnetd（standalone）
( telnetd -p 23 -l /bin/login ) 2>/dev/null || true

echo "[rcS] rcS done" > /dev/console 2>/dev/null || true
EOF

sudo chmod 755 /mnt/umuos014/etc/init.d/rcS
```

#### 7.3.6 ユーザー（root / tama）

```bash
sudo tee /mnt/umuos014/etc/passwd >/dev/null <<'EOF'
root:x:0:0:root:/root:/bin/sh
tama:x:1000:1000:tama:/home/tama:/bin/sh
EOF

sudo tee /mnt/umuos014/etc/group >/dev/null <<'EOF'
root:x:0:
users:x:100:
tama:x:1000:
EOF

sudo mkdir -p /mnt/umuos014/root
sudo mkdir -p /mnt/umuos014/home/tama
sudo chown 1000:1000 /mnt/umuos014/home/tama
```

パスワードは手動でハッシュ生成して `/etc/shadow` に貼る。

```bash
openssl passwd -6
openssl passwd -6
```

```bash
sudo tee /mnt/umuos014/etc/shadow >/dev/null <<'EOF'
root:<rootの$6$...を貼る>:20000:0:99999:7:::
tama:<tamaの$6$...を貼る>:20000:0:99999:7:::
EOF

sudo chown root:root /mnt/umuos014/etc/shadow
sudo chmod 600 /mnt/umuos014/etc/shadow
```

### 7.4 アンマウント

```bash
sync
sudo umount /mnt/umuos014
```

---

## 8. ISO（UmuOS-0.1.4-boot.iso）生成

### 8.1 grub.cfg

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable

cat > iso_root/boot/grub/grub.cfg <<'EOF'
set timeout=20
set default=0

serial --unit=0 --speed=115200 --word=8 --parity=no --stop=1
terminal_input serial console
terminal_output serial console

menuentry "UmuOS-0.1.4-base-stable" {
insmod gzio

linux /boot/vmlinuz-6.18.1 \
root=UUID=9f5a1e4f-19b2-4d1f-9a6e-0d2a59e2a0d4 \
rw \
console=tty0 console=ttyS0,115200n8 \
loglevel=7 \
panic=-1 \
net.ifnames=0 biosdevname=0

initrd /boot/initrd.img-6.18.1
}
EOF
```

### 8.2 ISO生成

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable
grub-mkrescue -o UmuOS-0.1.4-boot.iso iso_root
ls -lh UmuOS-0.1.4-boot.iso
```

---

## 9. run/（Rocky 側の起動コマンド）

```bash
cd ~/umu/umu_project/UmuOS-0.1.4-base-stable
mkdir -p run

cat > run/qemu.cmdline.txt <<'EOF'
/usr/libexec/qemu-kvm \
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
-monitor none
EOF

sed -n '1,120p' run/qemu.cmdline.txt
```

---

## 10. Rocky（起動・受入）

### 10.1 必要パッケージ（Rocky）

```bash
dnf -y install qemu-kvm qemu-img tmux util-linux iproute
test -x /usr/libexec/qemu-kvm

# NOTE: Rocky 9系では bridge-utils（brctl）が標準リポジトリに無いことがある。
# この手順は iproute（ip/bridge）だけで足りる。
```

### 10.2 配置（Ubuntu → Rocky）

成果物は 7点（ISO / disk.img / qemu.cmdline / スクリプト4本）を Rocky に置ければよい。

配置先（Rocky）：`/root/UmuOS-0.1.4-base-stable/`

7ファイルのコピー先（Ubuntu → Rocky）：

- `/home/tama/umu/umu_project/UmuOS-0.1.4-base-stable/UmuOS-0.1.4-boot.iso` → `/root/UmuOS-0.1.4-base-stable/UmuOS-0.1.4-boot.iso`
- `/home/tama/umu/umu_project/UmuOS-0.1.4-base-stable/disk/disk.img` → `/root/UmuOS-0.1.4-base-stable/disk/disk.img`
- `/home/tama/umu/umu_project/UmuOS-0.1.4-base-stable/run/qemu.cmdline.txt` → `/root/UmuOS-0.1.4-base-stable/run/qemu.cmdline.txt`
- `/home/tama/umu/umu_project/UmuOS-0.1.4-base-stable/umuOSstart.sh` → `/root/UmuOS-0.1.4-base-stable/umuOSstart.sh`
- `/home/tama/umu/umu_project/UmuOS-0.1.4-base-stable/connect_ttyS1.sh` → `/root/UmuOS-0.1.4-base-stable/connect_ttyS1.sh`
- `/home/tama/umu/umu_project/UmuOS-0.1.4-base-stable/run/tap_up.sh` → `/root/UmuOS-0.1.4-base-stable/run/tap_up.sh`
- `/home/tama/umu/umu_project/UmuOS-0.1.4-base-stable/run/tap_down.sh` → `/root/UmuOS-0.1.4-base-stable/run/tap_down.sh`

各ファイルの役割（短縮）：

- `UmuOS-0.1.4-boot.iso`：起動用ISO（GRUB + kernel + initrd）
- `disk/disk.img`：永続ディスク（ext4、rootfs入り、UUID固定）
- `run/qemu.cmdline.txt`：Rocky側での QEMU 起動コマンド（tap / ttyS0 / ttyS1 など）
- `umuOSstart.sh`：QEMU起動（ttyS0ログを `logs/` に保存）
- `connect_ttyS1.sh`：ttyS1（TCP:5555）に接続してログを `logs/` に保存
- `run/tap_up.sh`：tap 作成（`tap-umu` を作って `br0` へ接続）
- `run/tap_down.sh`：tap 削除（`tap-umu` を消す）

Rocky 側（最初に1回）：

```bash
sudo mkdir -p /root/UmuOS-0.1.4-base-stable/disk \
              /root/UmuOS-0.1.4-base-stable/run \
              /root/UmuOS-0.1.4-base-stable/logs
```

Rocky 側（配置確認）：

```bash
cd /root/UmuOS-0.1.4-base-stable
ls -lh UmuOS-0.1.4-boot.iso disk/disk.img run/qemu.cmdline.txt umuOSstart.sh connect_ttyS1.sh run/tap_up.sh run/tap_down.sh

# 念のため（実行権限を付与）
chmod +x umuOSstart.sh connect_ttyS1.sh run/tap_up.sh run/tap_down.sh

# disk.img は QEMU が read/write する（root で起動する前提なら 600 で十分）
chown root:root disk/disk.img
chmod 600 disk/disk.img
```

### 10.3 起動（tap-umu を都度作成→削除）

以下は Rocky の root で実行：

```bash
cd /root/UmuOS-0.1.4-base-stable

# 起動モード（ホスト側）
# - 既定（Rocky想定）: --net tap（br0 + tap-umu を使う）
# - 切り分け（Ubuntu等）: --net none（ネット無しで起動だけ行う）
# 使い方は ./umuOSstart.sh --help

# 起動（必要なら tap-umu を作って br0 に接続してから起動する。ttyS0 のログは logs/ に保存される）
./umuOSstart.sh

# NOTE: br0/tap を用意できない環境では、起動だけならネット無しでよい（ネットワーク機能は使えない）。
# ./umuOSstart.sh --net none
# （同等）NET_MODE=none ./umuOSstart.sh
```

停止後：

```bash
cd /root/UmuOS-0.1.4-base-stable

# tap を消してクリーンにしたい場合のみ
sudo ./run/tap_down.sh tap-umu
```

ttyS1（TCPシリアル）へ接続（別ターミナル）：

```bash
cd /root/UmuOS-0.1.4-base-stable
./connect_ttyS1.sh 5555
```

---

## 11. 受入（合格条件）

### 11.1 0.1.3互換

- ttyS0 でログインできる（root/tama）
- ttyS1（TCPシリアル）でもログインできる（同時ログイン）
- ゲスト `/logs/boot.log` が追記される

### 11.2 追加（telnetd）

- ゲスト `eth0` に `192.168.0.202/24` が入る
- `default via 192.168.0.1` が入る
- LAN から `192.168.0.202:23` に接続できる（root/tama）

### 11.3 追加（nc転送）

```bash
# UmuOS（telnetログイン後）
mkdir -p /tmp/in
cd /tmp/in
nc -l -p 12345 > payload.bin

# Ubuntu
nc 192.168.0.202 12345 < payload.bin
```

---

## 12. トラブルシュート（短縮）

1. まず ttyS0 でログインできるか（できないなら telnet 以前）
2. `ip addr` / `ip route` が入っているか（rcS の network.conf）
3. `ps` で telnetd が動いているか
4. LAN 到達性（Ubuntu → 192.168.0.202）
5. root だけ失敗するなら `/etc/securetty` を最優先
