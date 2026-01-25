---
title: UmuOS-0.1.4 Base Stable 詳細設計書
date: 2026-01-25
base_design: "./UmuOS-0.1.4 Base Stable-基本設計書.md"
status: draft
---

# 1. 目的 / 位置づけ

本書は「UmuOS-0.1.4 Base Stable 基本設計書」で固定した方針・要件を、
実装可能なコマンド列・ファイル配置・検査手順に落とし込み、再現可能な構築手順を確定する。

基本方針（固定）：

- 0.1.4 は **0.1.3 相当（switch_root / ttyS0+ttyS1 同時ログイン / boot.log）を壊さず**、追加は「ゲストtelnetd（TCP/23）」のみに限定する。
- ビルドは Ubuntu 24.04、起動と受入（LANからtelnet）は RockyLinux 9.7 とする。
- Ubuntu / Rocky ともに恒久的なネットワーク設定変更は行わない。
	- Rocky 側で許容するネットワーク操作は **一時的な tap-umu の作成/削除のみ**（br0 は観測のみ）。

補足：本ファイル先頭の front matter（例：`status: draft`）は文書管理用のメタ情報であり、
OSのビルド/起動/受入の成否には影響しない。レビュー中は `draft` のままでよい。

# 2. 参照

- 基本設計書：`UmuOS-0.1.4 Base Stable/docs/UmuOS-0.1.4 Base Stable-基本設計書.md`
- 0.1.3 参考（手順テンプレ）：`UmuOS-0.1.3/docs/詳細設計書-0.1.3.md`

# 3. 固定パラメータ（この文書で固定する値）

## 3.1 作業ルート（固定）

- 作業ルート（Ubuntu）：`~/umu/umu_project/UmuOS-0.1.4 Base Stable/`
- 配置ルート（Rocky）：`/home/tama/UmuOS-0.1.4 Base Stable/`

## 3.2 バージョン（固定）

- Kernel version：`6.18.1`
- BusyBox version：`1.36.1`

## 3.3 ネットワーク（固定）

- Rocky（ホスト）：`192.168.0.200`
- Ubuntu（開発）：`192.168.0.201`
- UmuOS（ゲスト）：`192.168.0.202/24`、GW `192.168.0.1`
- L2：Rocky 側に `br0` が存在する（無ければ中止）
- TAP 名（固定）：`tap-umu`
- ttyS1 TCP シリアル（固定）：`127.0.0.1:5555`

## 3.4 成果物（固定名）

- ISO：`UmuOS-0.1.4-boot.iso`
- 永続ディスク：`disk/disk.img`
- initrd：`initrd.img-6.18.1`
- kernel：`vmlinuz-6.18.1`

## 3.5 rootfs UUID（固定）

GRUB の `root=UUID=...` と initramfs の `/init` が参照する UUID を固定する。

- `ROOT_UUID=9f5a1e4f-19b2-4d1f-9a6e-0d2a59e2a0d4`

# 4. 作業ディレクトリ

Ubuntu 側で以下を作る（無ければ作成）。

```bash
mkdir -p ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable
cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable

mkdir -p \
	kernel/build \
	initramfs/src initramfs/rootfs \
	iso_root/boot/grub \
	disk run logs docs
```

# 5. 事前準備（Ubuntu 24.04 LTS）

## 5.1 必要パッケージ

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

## 5.2 コマンド存在確認（観測点）

```bash
command -v grub-mkrescue
command -v mkfs.ext4
command -v tune2fs
command -v musl-gcc
command -v cpio
command -v gzip
```

# 6. external（入力ソースの正）配置

本プロジェクトでは、入力ソースは `external/` を正とし参照のみとする。

## 6.1 Linux 6.18.1

期待するディレクトリ：`external/linux-6.18.1-kernel/`

Ubuntu 側で `external/` が空の場合のみ取得する（例）：

```bash
cd ~/umu/umu_project

# 空かどうかは Makefile の有無で判定
if [ ! -f external/linux-6.18.1-kernel/Makefile ]; then
	rm -rf external/linux-6.18.1-kernel
	mkdir -p external
	wget -O /tmp/linux-6.18.1.tar.xz https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.1.tar.xz
	tar -xf /tmp/linux-6.18.1.tar.xz -C external
	mv external/linux-6.18.1 external/linux-6.18.1-kernel
fi
```

## 6.2 BusyBox 1.36.1

期待するディレクトリ：`external/busybox-1.36.1/`

```bash
cd ~/umu/umu_project

if [ ! -f external/busybox-1.36.1/Makefile ]; then
	rm -rf external/busybox-1.36.1
	mkdir -p external
	wget -O /tmp/busybox-1.36.1.tar.bz2 https://busybox.net/downloads/busybox-1.36.1.tar.bz2
	tar -xf /tmp/busybox-1.36.1.tar.bz2 -C external
fi
```

# 7. Kernel（6.18.1）ビルド（out-of-tree）

## 7.1 `.config` の確定（本書で確定）

0.1.4 側に固定入力として配置する：

- `UmuOS-0.1.4 Base Stable/kernel/config-6.18.1`

生成ルール（固定）：

1. `defconfig` をベースにする
2. `scripts/config` で必要/禁止の差分を適用して `olddefconfig` で確定する
3. 確定した `.config` を `kernel/config-6.18.1` として保存する

必要/禁止（最低限、固定）：

- 必須（すべて `=y`）
	- `CONFIG_DEVTMPFS`
	- `CONFIG_DEVTMPFS_MOUNT`
	- `CONFIG_TMPFS`
	- `CONFIG_PROC_FS`
	- `CONFIG_SYSFS`
	- `CONFIG_BLK_DEV_INITRD`
	- `CONFIG_EXT4_FS`
	- `CONFIG_VIRTIO`
	- `CONFIG_VIRTIO_PCI`
	- `CONFIG_VIRTIO_BLK`
	- `CONFIG_VIRTIO_NET`
	- `CONFIG_NET`
	- `CONFIG_INET`
	- `CONFIG_UNIX98_PTYS`
	- `CONFIG_RD_GZIP`（initrd が gzip のため）
- 禁止（すべて `is not set`）
	- `CONFIG_IPV6`
	- `CONFIG_IP_PNP`

## 7.2 生成コマンド（Ubuntu）

```bash
cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable

KERNEL_SRC=~/umu/umu_project/external/linux-6.18.1-kernel
KERNEL_OUT=$PWD/kernel/build

mkdir -p "$KERNEL_OUT"

make -C "$KERNEL_SRC" O="$KERNEL_OUT" mrproper
make -C "$KERNEL_SRC" O="$KERNEL_OUT" defconfig

"$KERNEL_SRC"/scripts/config --file "$KERNEL_OUT"/.config \
	-e DEVTMPFS \
	-e DEVTMPFS_MOUNT \
	-e TMPFS \
	-e PROC_FS \
	-e SYSFS \
	-e BLK_DEV_INITRD \
	-e EXT4_FS \
	-e VIRTIO \
	-e VIRTIO_PCI \
	-e VIRTIO_BLK \
	-e VIRTIO_NET \
	-e NET \
	-e INET \
	-e UNIX98_PTYS \
	-e RD_GZIP \
	-d IPV6 \
	-d IP_PNP

make -C "$KERNEL_SRC" O="$KERNEL_OUT" olddefconfig

cp -f "$KERNEL_OUT"/.config kernel/config-6.18.1
```

観測点（期待値が `=y` / `is not set` になっていること）：

```bash
grep -E '^(CONFIG_DEVTMPFS=|CONFIG_DEVTMPFS_MOUNT=|CONFIG_TMPFS=|CONFIG_PROC_FS=|CONFIG_SYSFS=|CONFIG_BLK_DEV_INITRD=|CONFIG_EXT4_FS=|CONFIG_VIRTIO=|CONFIG_VIRTIO_PCI=|CONFIG_VIRTIO_BLK=|CONFIG_VIRTIO_NET=|CONFIG_NET=|CONFIG_INET=|CONFIG_UNIX98_PTYS=|CONFIG_RD_GZIP=)' kernel/config-6.18.1
grep -E '^(# CONFIG_IPV6 is not set|# CONFIG_IP_PNP is not set)$' kernel/config-6.18.1
```

## 7.3 ビルド

```bash
make -C "$KERNEL_SRC" O="$KERNEL_OUT" -j"$(nproc)"
```

## 7.4 ISO入力へ配置

```bash
cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable
cp -f kernel/build/arch/x86/boot/bzImage iso_root/boot/vmlinuz-6.18.1
cp -f kernel/config-6.18.1 iso_root/boot/config-6.18.1
file iso_root/boot/vmlinuz-6.18.1
```

# 8. BusyBox（1.36.1）ビルド（静的リンク）

## 8.1 `.config` の確定（本書で確定）

0.1.4 側に固定入力として配置する：

- `UmuOS-0.1.4 Base Stable/initramfs/busybox/config-1.36.1`

生成ルール（固定）：

1. `defconfig` をベースにする
2. `.config` を編集し、以下の必須項目を `=y` にする
3. `make olddefconfig` 相当で整形し、確定した `.config` を保存する

必須（固定、最低限）：

- `CONFIG_STATIC=y`
- `CONFIG_TELNETD=y`
- `CONFIG_LOGIN=y`
- `CONFIG_FEATURE_SECURETTY=y`
- `CONFIG_FEATURE_SHADOWPASSWDS=y`
- `CONFIG_NC=y`
- `CONFIG_IP=y`

補足（固定）：本書では BusyBox `ip` が `ip link/addr/route` を提供することを合格条件とし、ビルド後に必ず検査する。

## 8.2 ビルド手順（Ubuntu）

external は参照のみのため、作業用コピーを作る。

```bash
cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable

BUSYBOX_SRC=~/umu/umu_project/external/busybox-1.36.1
BUSYBOX_WORK=$PWD/initramfs/busybox/work

rm -rf "$BUSYBOX_WORK"
mkdir -p "$BUSYBOX_WORK"
rsync -a --delete "$BUSYBOX_SRC"/ "$BUSYBOX_WORK"/

cd "$BUSYBOX_WORK"
make distclean
make defconfig
```

`.config` を編集して必須項目を反映（この時点で `initramfs/busybox/config-1.36.1` として保存する）：

```bash
cd "$BUSYBOX_WORK"

# 既存行があれば置換、無ければ追記（単純だが再現性優先）
set_kv() {
	key="$1"; val="$2"
	if grep -q "^${key}=" .config; then
		sed -i "s/^${key}=.*/${key}=${val}/" .config
	else
		echo "${key}=${val}" >> .config
	fi
}

set_kv CONFIG_STATIC y
set_kv CONFIG_TELNETD y
set_kv CONFIG_LOGIN y
set_kv CONFIG_FEATURE_SECURETTY y
set_kv CONFIG_FEATURE_SHADOWPASSWDS y
set_kv CONFIG_NC y
set_kv CONFIG_IP y

make olddefconfig

cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable
mkdir -p initramfs/busybox
cp -f "$BUSYBOX_WORK"/.config initramfs/busybox/config-1.36.1
```

ビルド（静的リンク）：

```bash
cd "$BUSYBOX_WORK"
make -j"$(nproc)"
file busybox
```

検査（固定）：

```bash
./busybox ip link
./busybox ip addr
./busybox ip route
./busybox telnetd --help >/dev/null 2>&1 || true
./busybox login --help >/dev/null 2>&1 || true
./busybox nc -h 2>/dev/null || true
```

# 9. initramfs（initrd.img-6.18.1）生成

方針：0.1.3 と同じく、initramfs は `switch_root` を成立させるための最小機能に限定し、永続 rootfs（ext4）側で rcS を実行する。

## 9.1 initramfs rootfs（BusyBox と switch_root）

```bash
cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable

rm -rf initramfs/rootfs
mkdir -p initramfs/rootfs/{bin,sbin,etc,proc,sys,dev,dev/pts,run,newroot,tmp}

# BusyBox（8章でビルドしたもの）
cp -f initramfs/busybox/work/busybox initramfs/rootfs/bin/busybox
chmod 755 initramfs/rootfs/bin/busybox

# applet を生成
sudo chroot initramfs/rootfs /bin/busybox --install -s /bin
sudo chroot initramfs/rootfs /bin/busybox --install -s /sbin

# switch_root があること（最重要）
ls -l initramfs/rootfs/bin/switch_root
```

## 9.2 initramfs `/init`（C）

0.1.3 の `init.c` を 0.1.4 側へコピーして使用する（内容は root=UUID を読んで ext4 を探す）。

```bash
cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable
cp -f ~/umu/umu_project/UmuOS-0.1.3/initramfs/src/init.c initramfs/src/init.c

musl-gcc -static -O2 -Wall -Wextra -o initramfs/rootfs/init initramfs/src/init.c
chmod 755 initramfs/rootfs/init
file initramfs/rootfs/init
```

## 9.3 initrd 作成

```bash
cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable/initramfs

rm -f ../initrd.filelist0 ../initrd.cpio ../initrd.cpio.list ../initrd.img-6.18.1

cd rootfs
find . -print0 | sed -z 's#^\./##' > ../initrd.filelist0

cd ..
cpio --null -ov --format=newc < initrd.filelist0 > initrd.cpio
cpio -t < initrd.cpio > initrd.cpio.list
grep -E '^(init|bin/switch_root)$' initrd.cpio.list
gzip -9 -c initrd.cpio > initrd.img-6.18.1

cp -f initrd.img-6.18.1 ../iso_root/boot/initrd.img-6.18.1
```

# 10. 永続ディスク（disk/disk.img）作成とrootfs投入

## 10.1 作成（ext4、固定UUID）

```bash
cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable/disk
rm -f disk.img
truncate -s 4G disk.img
mkfs.ext4 -F -U 9f5a1e4f-19b2-4d1f-9a6e-0d2a59e2a0d4 disk.img
sudo blkid -p -o value -s UUID disk.img
```

## 10.2 マウント（loop）

```bash
sudo mkdir -p /mnt/umuos014
sudo mount -o loop ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable/disk/disk.img /mnt/umuos014
findmnt /mnt/umuos014
```

## 10.3 最小rootfs（0.1.3互換 + telnet追加）

```bash
sudo mkdir -p /mnt/umuos014/{bin,sbin,etc,proc,sys,dev,dev/pts,run,home,root,tmp,logs,etc/init.d,etc/umu}
```

### 10.3.1 BusyBox 配置（ext4側）

```bash
sudo cp -f ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable/initramfs/busybox/work/busybox /mnt/umuos014/bin/busybox
sudo chown root:root /mnt/umuos014/bin/busybox
sudo chmod 755 /mnt/umuos014/bin/busybox

sudo chroot /mnt/umuos014 /bin/busybox --install -s /bin
sudo chroot /mnt/umuos014 /bin/busybox --install -s /sbin
sudo ln -sf /bin/busybox /mnt/umuos014/sbin/init
sudo ls -l /mnt/umuos014/sbin/init
```

### 10.3.2 inittab（ttyS0/ttyS1維持、固定）

```bash
sudo tee /mnt/umuos014/etc/inittab >/dev/null <<'EOF'
::sysinit:/etc/init.d/rcS

ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
ttyS1::respawn:/sbin/getty -L 115200 ttyS1 vt100

::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a
EOF
```

### 10.3.3 /etc/umu/network.conf（固定）

```bash
sudo tee /mnt/umuos014/etc/umu/network.conf >/dev/null <<'EOF'
IFNAME=eth0
MODE=static
IP=192.168.0.202/24
GW=192.168.0.1
DNS=192.168.0.1
EOF
```

### 10.3.4 /etc/securetty（固定）

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

### 10.3.5 rcS（①mount ②boot.log ③network ④telnetd）

```bash
sudo tee /mnt/umuos014/etc/init.d/rcS >/dev/null <<'EOF'
#!/bin/sh

# 失敗しても止めない（ttyS0/ttyS1 を優先して観測可能にする）
mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mount -t devpts devpts /dev/pts 2>/dev/null || true

mkdir -p /logs 2>/dev/null || true

# ② 永続ログ（/logs/boot.log）
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
) 2>/dev/null || (
	echo "[rcS] WARN: boot.log write failed" > /dev/console 2>/dev/null || true
)

# ③ ネットワーク初期化（/etc/umu/network.conf を読む）
(
	CONF=/etc/umu/network.conf
	if [ -f "$CONF" ]; then
		# shellcheck disable=SC1090
		. "$CONF"
	else
		echo "[rcS] WARN: missing $CONF" > /dev/console 2>/dev/null || true
	fi

	IFNAME="${IFNAME:-eth0}"
	if [ "${MODE:-}" = "static" ] && [ -n "${IP:-}" ] && [ -n "${GW:-}" ]; then
		ip link set dev "$IFNAME" up 2>/dev/null || true
		ip addr add "$IP" dev "$IFNAME" 2>/dev/null || true
		ip route replace default via "$GW" dev "$IFNAME" 2>/dev/null || true
	else
		echo "[rcS] WARN: network.conf invalid (MODE/IP/GW)" > /dev/console 2>/dev/null || true
	fi
) 2>/dev/null || true

# ④ telnetd 起動（standalone）
(
	# /bin/login 経由。root ログインは /etc/securetty で制御する。
	telnetd -p 23 -l /bin/login
) 2>/dev/null || (
	echo "[rcS] WARN: telnetd start failed" > /dev/console 2>/dev/null || true
)

echo "[rcS] rcS done" > /dev/console 2>/dev/null || true
EOF

sudo chmod 755 /mnt/umuos014/etc/init.d/rcS
```

### 10.3.6 ユーザー（root / tama）

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

echo "# パスワードハッシュを2回生成（root / tama）"
openssl passwd -6
openssl passwd -6

ここは **いつも通り手動で** ハッシュを生成し、生成した値をその場で `/etc/shadow` に反映する。

```bash
# root 用ハッシュ（対話入力）
HASH_ROOT="$(openssl passwd -6)"

# tama 用ハッシュ（対話入力）
HASH_TAMA="$(openssl passwd -6)"

# 生成結果を目視確認（漏えいに注意。ログに残さない運用が望ましい）
echo "HASH_ROOT=${HASH_ROOT}"
echo "HASH_TAMA=${HASH_TAMA}"

# /etc/shadow を生成（ハッシュ内の '$' を壊さないため、変数展開で埋め込む）
sudo sh -c '
	printf "%s\n" \
		"root:${HASH_ROOT}:20000:0:99999:7:::" \
		"tama:${HASH_TAMA}:20000:0:99999:7:::" \
		> /mnt/umuos014/etc/shadow
'
```

sudo chown root:root /mnt/umuos014/etc/shadow
sudo chmod 600 /mnt/umuos014/etc/shadow

sudo mkdir -p /mnt/umuos014/root
sudo mkdir -p /mnt/umuos014/home/tama
sudo chown 1000:1000 /mnt/umuos014/home/tama
```

## 10.4 アンマウント

```bash
sync
sudo umount /mnt/umuos014
```

# 11. ISO（UmuOS-0.1.4-boot.iso）生成

## 11.1 grub.cfg

`net.ifnames=0 biosdevname=0` を付与して NIC 名を `eth0` に固定する。

```bash
cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable

cat > iso_root/boot/grub/grub.cfg <<'EOF'
set timeout=20
set default=0

serial --unit=0 --speed=115200 --word=8 --parity=no --stop=1
terminal_input serial console
terminal_output serial console

menuentry "UmuOS-0.1.4 Base Stable" {
		insmod all_video
		insmod gfxterm
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

## 11.2 ISO生成

```bash
cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable
grub-mkrescue -o UmuOS-0.1.4-boot.iso iso_root
```

# 12. run/（起動I/F）生成

方針：起動コマンドはスクリプトに埋めず、`run/qemu.cmdline.txt` を **正** として固定する。

注記（固定）：RockyLinux 9.7 では `qemu-system-x86_64` が `/usr/bin` に提供されない場合があるため、
本手順では Rocky 側での起動コマンド実体を `/usr/libexec/qemu-kvm` に固定する。

```bash
cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable
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

# 13. Rocky（起動・受入）手順

## 13.1 Rocky 事前準備（設定は最小、恒久NW変更なし）

方針：基本設計の制約により、Rocky 側で行ってよい恒久変更は **パッケージ導入のみ** とし、
ネットワーク設定（IP/ブリッジ作成/接続変更など）の恒久変更は行わない。

以降のコマンドは Rocky 上で実行する。

### 13.1.1 必要パッケージ

```bash
# 以降、root ログイン前提（sudo は使わない）
dnf -y install \
	qemu-kvm qemu-img \
	tmux \
	util-linux iproute bridge-utils

test -x /usr/libexec/qemu-kvm
command -v script
command -v tmux
```

補足：`tmux` は「常時稼働を tmux 内で行う」という基本設計の前提に直結するため、本手順の必須パッケージとして導入する（恒久変更だが許容範囲）。

### 13.1.2 KVM 利用可否（観測点）

```bash
ls -l /dev/kvm
lsmod | grep -E '^(kvm|kvm_intel|kvm_amd)( |$)' || true

ls -l /dev/net/tun
```

`/dev/kvm` が無い場合は、基本設計（KVM必須）により中止する。

## 13.1.3 QEMU コマンド確認（観測点）

`run/qemu.cmdline.txt` は RockyLinux 9.7 上で `/usr/libexec/qemu-kvm` を呼び出す前提で固定する。
（`qemu-system-x86_64` は bash-completion のみがヒットする等、期待どおりに提供されないことがある。）

```bash
test -x /usr/libexec/qemu-kvm || true

# まだ無い場合は導入する
dnf -y install qemu-kvm qemu-img

test -x /usr/libexec/qemu-kvm
/usr/libexec/qemu-kvm --version

# 導入済みパッケージ確認（観測点）
rpm -q qemu-kvm qemu-img || true
```

補足：`lsmod | grep -E '...\b'` のように `\\b` を書くと、正規表現の単語境界ではなく「\b という文字列」を探してしまい、誤って未検出になることがある。本書ではその罠を避けるため `( |$)` を用いる。

## 13.2 配置（Ubuntu→Rocky へ成果物転送も含む）

Rocky 側の配置ルート（固定）：`/home/tama/UmuOS-0.1.4 Base Stable/`

### 13.2.1 Rocky 側ディレクトリ作成

```bash
mkdir -p "/home/tama/UmuOS-0.1.4 Base Stable"/{disk,run,logs}
ls -la "/home/tama/UmuOS-0.1.4 Base Stable"
```

### 13.2.2 Ubuntu 側で成果物をまとめる（例）

Ubuntu 側で実行する（例）。Rocky へ運ぶ方法は環境に合わせてよい（scp/rsync/USB等）。

```bash
cd ~/umu/umu_project/UmuOS-0.1.4\ Base\ Stable

tar -C . -czf /tmp/umuos-0.1.4-base-stable.artifacts.tgz \
	UmuOS-0.1.4-boot.iso \
	disk/disk.img \
	run/qemu.cmdline.txt

ls -lh /tmp/umuos-0.1.4-base-stable.artifacts.tgz
```

参考（scp 例、IP は環境に合わせて置換）：

```bash
# Ubuntu → Rocky
scp /tmp/umuos-0.1.4-base-stable.artifacts.tgz root@192.168.0.200:/tmp/
```

### 13.2.3 Rocky 側で展開

Rocky 側に `/tmp/umuos-0.1.4-base-stable.artifacts.tgz` が到着している前提。

```bash
cd "/home/tama/UmuOS-0.1.4 Base Stable"
tar -xzf /tmp/umuos-0.1.4-base-stable.artifacts.tgz

ls -lh UmuOS-0.1.4-boot.iso disk/disk.img run/qemu.cmdline.txt
```

## 13.3 起動前チェック（設定変更なし）

Rocky 側に以下を配置する（固定名）：

- `UmuOS-0.1.4-boot.iso`
- `disk/disk.img`
- `run/qemu.cmdline.txt`

```bash
ip link show br0
bridge link show
ip addr show dev br0
```

`br0` が無い場合は中止。

基本設計の制約により、`br0` を新規作成する手順（恒久NW変更）は本書には含めない。

### 13.3.1 RockyLinux 9.7 のネットワーク実測値（tama@hsv）

以下は本環境の **実測値**（`ip a` 出力）であり、以降の受入手順はこの状態を前提とする。

- `br0` が `192.168.0.200/24` を持つ
- 物理NIC `enp0s25` が `br0` の slave（`master br0`）になっている
- `virbr0`（libvirt のデフォルトNATブリッジ）は存在してもよいが、本手順では使用しない
- `vnet0`（libvirt 管理の TAP）は存在してもよいが、本手順では使用しない（UmuOS は `tap-umu` を都度作成して使う）

```text
[tama@hsv ~]$ ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN group default qlen 1000
	link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
	inet 127.0.0.1/8 scope host lo
	   valid_lft forever preferred_lft forever
	inet6 ::1/128 scope host
	   valid_lft forever preferred_lft forever
2: enp0s25: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel master br0 state UP group default qlen 1000
	link/ether 90:1b:0e:19:f1:27 brd ff:ff:ff:ff:ff:ff
3: enp2s0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc fq_codel state DOWN group default qlen 1000
	link/ether 90:1b:0e:0d:61:c7 brd ff:ff:ff:ff:ff:ff
4: br0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP group default qlen 1000
	link/ether 90:1b:0e:19:f1:27 brd ff:ff:ff:ff:ff:ff
	inet 192.168.0.200/24 brd 192.168.0.255 scope global noprefixroute br0
	   valid_lft forever preferred_lft forever
	inet6 fe80::f823:4e50:bb42:866a/64 scope link noprefixroute
	   valid_lft forever preferred_lft forever
5: virbr0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc noqueue state DOWN group default qlen 1000
	link/ether 52:54:00:f6:6d:d2 brd ff:ff:ff:ff:ff:ff
	inet 192.168.122.1/24 brd 192.168.122.255 scope global virbr0
	   valid_lft forever preferred_lft forever
6: vnet0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue master br0 state UNKNOWN group default qlen 1000
	link/ether fe:54:00:8e:0a:cd brd ff:ff:ff:ff:ff:ff
	inet6 fe80::fc54:ff:fe8e:acd/64 scope link
	   valid_lft forever preferred_lft forever
```

### 13.3.2 RockyLinux 9.7 の追加実測値（tama@hsv）

以下も本環境の **実測値**である（設定変更はしていない。`tmux` のみパッケージ導入を実施）。

```text
[root@hsv tama]# ls -l /dev/kvm
crw-rw-rw- 1 root kvm 10, 232  1月 22 05:45 /dev/kvm

[root@hsv tama]# ls -l /dev/net/tun
crw-rw-rw- 1 root root 10, 200  1月 19 06:19 /dev/net/tun

[root@hsv tama]# lsmod | grep -E '^(kvm|kvm_intel|kvm_amd)( |$)' || true
kvm_intel             536576  6
kvm                  1417216  5 kvm_intel

[root@hsv tama]# systemctl is-active firewalld || true
active

[root@hsv tama]# nft list ruleset | head
table inet firewalld { # progname firewalld
	flags owner,persist

	ct helper helper-tftp-udp {
		type "tftp" protocol udp
		l3proto inet
	}

	chain mangle_PREROUTING {
		type filter hook prerouting priority mangle + 10; policy accept;

[root@hsv tama]# lsmod | grep br_netfilter || true

[root@hsv tama]# sysctl net.bridge.bridge-nf-call-iptables net.bridge.bridge-nf-call-ip6tables net.bridge.bridge-nf-call-arptables || true
sysctl: cannot stat /proc/sys/net/bridge/bridge-nf-call-iptables: そのようなファイルやディレクトリはありません
sysctl: cannot stat /proc/sys/net/bridge/bridge-nf-call-ip6tables: そのようなファイルやディレクトリはありません
sysctl: cannot stat /proc/sys/net/bridge/bridge-nf-call-arptables: そのようなファイルやディレクトリはありません

[root@hsv tama]# systemctl is-active libvirtd || true
inactive

[root@hsv tama]# getenforce
Disabled

[root@hsv tama]# firewall-cmd --state
running

[root@hsv tama]# firewall-cmd --get-active-zones
libvirt
	interfaces: virbr0
public
	interfaces: br0 enp0s25

[root@hsv tama]# bridge link show
2: enp0s25: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 master br0 state forwarding priority 32 cost 100
6: vnet0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 master br0 state forwarding priority 32 cost 100

[root@hsv tama]# ip link show vnet0
6: vnet0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue master br0 state UNKNOWN mode DEFAULT group default qlen 1000
	link/ether fe:54:00:8e:0a:cd brd ff:ff:ff:ff:ff:ff

[root@hsv tama]# ethtool enp0s25 | grep -E 'Link detected' || true
	Link detected: yes

[root@hsv tama]# dnf -y install tmux
(略) tmux-3.2a-5.el9.x86_64 installed

[root@hsv tama]# qemu-system-x86_64 --version
bash: qemu-system-x86_64: コマンドが見つかりませんでした...

[root@hsv tama]# dnf -y install qemu-kvm qemu-img
パッケージ qemu-kvm-17:9.1.0-29.el9_7.3.x86_64 は既にインストールされています。
パッケージ qemu-img-17:9.1.0-29.el9_7.3.x86_64 は既にインストールされています。

[root@hsv tama]# rpm -q qemu-kvm qemu-img
qemu-kvm-9.1.0-29.el9_7.3.x86_64
qemu-img-9.1.0-29.el9_7.3.x86_64

[root@hsv tama]# ls -l /usr/libexec/qemu-kvm
-rwxr-xr-x. 1 root root 29593888 12月 19 05:37 /usr/libexec/qemu-kvm

[root@hsv tama]# /usr/libexec/qemu-kvm --version
QEMU emulator version 9.1.0 (qemu-kvm-9.1.0-29.el9_7.3)

# 補足：本環境では qemu-kvm の実体は /usr/libexec/qemu-kvm であり、qemu-system-x86_64 / qemu-kvm が PATH 上に存在しない。
# そのため run/qemu.cmdline.txt は /usr/libexec/qemu-kvm を呼び出す前提で固定する。
```

## 13.4 起動（tap-umu を都度作成→終了時に削除）

1回の起動で実行する操作（固定）：

以下は **root** で実行する。

```bash
cd "/home/tama/UmuOS-0.1.4 Base Stable"

# QEMU コマンドが存在すること（起動前の最重要チェック）
test -x /usr/libexec/qemu-kvm

# 起動前クリーンアップ
ip link set dev tap-umu down || true
ip link del dev tap-umu || true

# tap-umu 作成→br0 へ接続→up
ip tuntap add dev tap-umu mode tap
ip link set dev tap-umu master br0
ip link set dev tap-umu up
```

ログ採取（固定）：

```bash
# 作業ディレクトリ：/home/tama/UmuOS-0.1.4 Base Stable/
mkdir -p logs

if [ -f logs/host_qemu.console.log ]; then
	mv -f logs/host_qemu.console.log logs/host_qemu.console.log.prev
fi

script -q -f -c "$(cat run/qemu.cmdline.txt)" logs/host_qemu.console.log
```

KVM が使えているかの判定（固定、観測点）：

```bash
grep -iE 'failed to initialize kvm|kvm' logs/host_qemu.console.log | head -n 50 || true
```

`failed to initialize KVM` 等が出る場合は、KVM 前提が崩れているため中止して切り分ける。

## 13.5 停止後クリーンアップ（必須）

以下は **root** で実行する。

```bash
ip link set dev tap-umu down || true
ip link del dev tap-umu || true
```

# 14. 受入（合格条件）

## 14.1 0.1.3互換（維持）

- `switch_root` が成立する（ttyS0ログに `exec: /bin/switch_root /newroot /sbin/init` が出る）
- ttyS0 で `root` / `tama` ログイン成立
- ttyS1（TCPシリアル）でも同時ログイン成立
- ゲスト `/logs/boot.log` が追記される

## 14.2 追加（telnetd）

- ゲスト `eth0` に `192.168.0.202/24` が入る
- `default via 192.168.0.1` が入る
- ローカルPC（TeraTerm）から `192.168.0.202:23` に接続し `root` / `tama` ログイン成立

## 14.3 追加（nc転送）

固定フロー（ゲスト受信、Ubuntu送信、ポート12345）：

```bash
# UmuOS（telnetログイン後）
mkdir -p /tmp/in
cd /tmp/in
nc -l -p 12345 > payload.bin

# Ubuntu
nc 192.168.0.202 12345 < payload.bin
```

# 15. トラブルシュート（切り分け順）

1. ttyS0 でログインできるか（できないなら telnet 以前）
2. ゲストに IP/route が入っているか（rcS のネット初期化）
3. telnetd が起動しているか（rcS の telnetd 起動）
4. LAN 到達性（ローカルPC/Ubuntu → 192.168.0.202）
5. `login`/`shadow`/`securetty`（rootだけ失敗は securetty を最優先）
