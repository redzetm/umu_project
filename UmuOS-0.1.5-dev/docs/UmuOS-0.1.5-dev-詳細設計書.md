---
title: UmuOS-0.1.5-dev 詳細設計書
date: 2026-02-11
base_design: "./UmuOS-0.1.5-dev-基本設計書.md"
status: rewrite-for-manual
---

# UmuOS-0.1.5-dev 詳細設計書（TeraTerm コピペ手順書 / 変数なし）

この文書は、UmuOS-0.1.5-dev を「手元で手動構築」するための **コマンド貼り付け手順**のみを記載する。
手順の追跡性を優先し、`export ...` のような“前提変数ブロック”は使用しない。

設計方針：

- 0.1.4-base-stable 相当の起動（kernel + initramfs + switch_root + ext4 rootfs）を前提にする。
- 0.1.5-dev の追加機能（DNS/JST/NTP、`/umu_bin`、`ll`、自作 `su`、`ftpd`）は **disk.img（永続 rootfs）側**に統合する。
- TeraTerm でのコピペを想定し、**対話操作（`menuconfig`）を使わず**に進める。
- 途中の確認（観測点）は必須にしない（異常時の切り分けとして最後にまとめる）。

---

## 0. 固定値（読むだけ / コマンドに直接埋め込む）

- 作業ルート（Ubuntu）：`/home/tama/umu/umu_project/UmuOS-0.1.5-dev`
- Kernel source：`/home/tama/umu/umu_project/external/linux-6.18.1-kernel`
- BusyBox source：`/home/tama/umu/umu_project/external/busybox-1.36.1`

- Kernel version：`6.18.1`
- BusyBox version：`1.36.1`

- ISO：`UmuOS-0.1.5-boot.iso`
- initrd：`initrd.img-6.18.1`
- kernel：`vmlinuz-6.18.1`

- 永続ディスク：`disk/disk.img`（ext4、4GiB、UUID固定）
- rootfs UUID：`d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15`

- ゲストIP：`192.168.0.202/24`
- GW：`192.168.0.1`
- DNS：`8.8.8.8`、`8.8.4.4`

- タイムゾーン：`/etc/TZ` に `JST-9`
- NTP サーバ：`time.google.com`

- ttyS1 TCP シリアル（Rocky側）：`127.0.0.1:5555`
- TAP 名（Rocky側）：`tap-umu`

---

## 1. Ubuntu 事前準備（パッケージ）

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

# 自作 su の静的リンクで必要になる可能性（入らなくても後で対処できる）
sudo apt install -y libcrypt-dev || true
sudo apt install -y libxcrypt-dev || true
```

---

## 2. 作業ディレクトリ（初期化）

```bash
mkdir -p /home/tama/umu/umu_project/UmuOS-0.1.5-dev
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

mkdir -p kernel/build \
		 initramfs/src initramfs/rootfs \
		 initramfs/busybox \
		 iso_root/boot/grub \
		 disk run logs work

test -f /home/tama/umu/umu_project/external/linux-6.18.1-kernel/Makefile
test -f /home/tama/umu/umu_project/external/busybox-1.36.1/Makefile
```

---

## 3. Kernel（out-of-tree）

貼り付け後、ビルドが終わるまで中断しない（中断すると成果物が出ない）。

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

make -C /home/tama/umu/umu_project/external/linux-6.18.1-kernel mrproper

rm -rf /home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build
mkdir -p /home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build

make -C /home/tama/umu/umu_project/external/linux-6.18.1-kernel \
  O=/home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build defconfig

/home/tama/umu/umu_project/external/linux-6.18.1-kernel/scripts/config \
  --file /home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build/.config \
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

make -C /home/tama/umu/umu_project/external/linux-6.18.1-kernel \
  O=/home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build olddefconfig

# ビルドログは固定名で上書き（追いかけやすさ優先）
( make -C /home/tama/umu/umu_project/external/linux-6.18.1-kernel \
    O=/home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build -j4 bzImage \
  ) > /home/tama/umu/umu_project/UmuOS-0.1.5-dev/logs/kernel_build_bzImage.log 2>&1

mkdir -p /home/tama/umu/umu_project/UmuOS-0.1.5-dev/iso_root/boot
cp -f /home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build/arch/x86/boot/bzImage \
  /home/tama/umu/umu_project/UmuOS-0.1.5-dev/iso_root/boot/vmlinuz-6.18.1
cp -f /home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build/.config \
  /home/tama/umu/umu_project/UmuOS-0.1.5-dev/iso_root/boot/config-6.18.1
```

---

## 4. BusyBox（静的リンク、対話なし）

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

rm -rf /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work
mkdir -p /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work
rsync -a --delete /home/tama/umu/umu_project/external/busybox-1.36.1/ \
  /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work/

cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work
make distclean
make defconfig

# 追跡性を優先し、.config の末尾に追記して上書きする（最後の指定が勝つ）
cat >> .config <<'EOF'
CONFIG_STATIC=y
CONFIG_INIT=y
CONFIG_FEATURE_USE_INITTAB=y
CONFIG_GETTY=y
CONFIG_SWITCH_ROOT=y
CONFIG_TELNETD=y
CONFIG_FEATURE_TELNETD_STANDALONE=y
CONFIG_LOGIN=y
CONFIG_IP=y
CONFIG_NC=y

CONFIG_WGET=y
CONFIG_PING=y
CONFIG_NSLOOKUP=y
CONFIG_NTPD=y
CONFIG_TCPSVD=y
CONFIG_FTPD=y
EOF

yes "" | make oldconfig
cp -f .config /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/config-1.36.1

( make -j4 ) > /home/tama/umu/umu_project/UmuOS-0.1.5-dev/logs/busybox_build.log 2>&1
```

---

## 5. initramfs（initrd.img-6.18.1）

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

rm -rf /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/rootfs
mkdir -p /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/rootfs/{bin,sbin,etc,proc,sys,dev,dev/pts,run,newroot,tmp}

cp -f /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work/busybox \
	/home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/rootfs/bin/busybox
chmod 755 /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/rootfs/bin/busybox

sudo chroot /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/rootfs /bin/busybox --install -s /bin
sudo chroot /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/rootfs /bin/busybox --install -s /sbin

cp -f /home/tama/umu/umu_project/UmuOS-0.1.4-base-stable/initramfs/src/init.c \
	/home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/src/init.c
musl-gcc -static -O2 -Wall -Wextra \
	-o /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/rootfs/init \
	/home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/src/init.c
chmod 755 /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/rootfs/init

cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs
rm -f initrd.filelist0 initrd.cpio initrd.cpio.list initrd.img-6.18.1
find rootfs -mindepth 1 -printf '%P\0' > initrd.filelist0
cd rootfs
cpio --null -ov --format=newc < ../initrd.filelist0 > ../initrd.cpio
cd ..
gzip -9 -c initrd.cpio > initrd.img-6.18.1

mkdir -p /home/tama/umu/umu_project/UmuOS-0.1.5-dev/iso_root/boot
cp -f initrd.img-6.18.1 /home/tama/umu/umu_project/UmuOS-0.1.5-dev/iso_root/boot/initrd.img-6.18.1
```

---

## 6. disk.img（永続 rootfs）

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

rm -f /home/tama/umu/umu_project/UmuOS-0.1.5-dev/disk/disk.img
truncate -s 4G /home/tama/umu/umu_project/UmuOS-0.1.5-dev/disk/disk.img
mkfs.ext4 -F -U d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15 /home/tama/umu/umu_project/UmuOS-0.1.5-dev/disk/disk.img

sudo mkdir -p /mnt/umuos015
sudo mount -o loop /home/tama/umu/umu_project/UmuOS-0.1.5-dev/disk/disk.img /mnt/umuos015

sudo mkdir -p /mnt/umuos015/{bin,sbin,etc,proc,sys,dev,dev/pts,run,var,var/run,home,root,tmp,logs,etc/init.d,etc/umu,umu_bin}

sudo cp -f /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work/busybox /mnt/umuos015/bin/busybox
sudo chown root:root /mnt/umuos015/bin/busybox
sudo chmod 755 /mnt/umuos015/bin/busybox
sudo chroot /mnt/umuos015 /bin/busybox --install -s /bin
sudo chroot /mnt/umuos015 /bin/busybox --install -s /sbin
sudo ln -sf /bin/busybox /mnt/umuos015/sbin/init

sudo tee /mnt/umuos015/etc/inittab >/dev/null <<'EOF'
::sysinit:/etc/init.d/rcS

ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
ttyS1::respawn:/sbin/getty -L 115200 ttyS1 vt100

::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a
EOF

sudo tee /mnt/umuos015/etc/umu/network.conf >/dev/null <<'EOF'
IFNAME=eth0
MODE=static
IP=192.168.0.202/24
GW=192.168.0.1
DNS=8.8.8.8
EOF

sudo tee /mnt/umuos015/etc/resolv.conf >/dev/null <<'EOF'
nameserver 8.8.8.8
nameserver 8.8.4.4
EOF

sudo tee /mnt/umuos015/etc/TZ >/dev/null <<'EOF'
JST-9
EOF

sudo tee /mnt/umuos015/etc/securetty >/dev/null <<'EOF'
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

sudo chown root:root /mnt/umuos015/umu_bin
sudo chmod 0755 /mnt/umuos015/umu_bin

sudo tee /mnt/umuos015/umu_bin/ll >/dev/null <<'EOF'
#!/bin/sh
exec ls -lF "$@"
EOF
sudo chown root:root /mnt/umuos015/umu_bin/ll
sudo chmod 0755 /mnt/umuos015/umu_bin/ll

sudo tee /mnt/umuos015/umu_bin/ftpd_start >/dev/null <<'EOF'
#!/bin/sh

mkdir -p /run

if [ -f /run/ftpd.pid ] && kill -0 "$(cat /run/ftpd.pid)" 2>/dev/null; then
	exit 0
fi

busybox tcpsvd -vE 0.0.0.0 21 busybox ftpd /tmp &
echo $! > /run/ftpd.pid
EOF
sudo chown root:root /mnt/umuos015/umu_bin/ftpd_start
sudo chmod 0755 /mnt/umuos015/umu_bin/ftpd_start

sudo tee /mnt/umuos015/umu_bin/ftpd_stop >/dev/null <<'EOF'
#!/bin/sh

if [ -f /run/ftpd.pid ]; then
	kill "$(cat /run/ftpd.pid)" 2>/dev/null || true
	rm -f /run/ftpd.pid
fi
EOF
sudo chown root:root /mnt/umuos015/umu_bin/ftpd_stop
sudo chmod 0755 /mnt/umuos015/umu_bin/ftpd_stop

sudo tee /mnt/umuos015/umu_bin/ntp_sync >/dev/null <<'EOF'
#!/bin/sh
ping -c 1 8.8.8.8 >/dev/null 2>&1 || exit 1
ntpd -n -q -p time.google.com >/dev/null 2>&1 && exit 0
ntpd -n -p time.google.com
EOF
sudo chown root:root /mnt/umuos015/umu_bin/ntp_sync
sudo chmod 0755 /mnt/umuos015/umu_bin/ntp_sync

sudo tee /mnt/umuos015/etc/init.d/rcS >/dev/null <<'EOF'
#!/bin/sh

export PATH=/umu_bin:/sbin:/bin

mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mount -t devpts devpts /dev/pts 2>/dev/null || true

mkdir -p /logs /run /var/run /umu_bin 2>/dev/null || true
: > /var/run/utmp 2>/dev/null || true

chown root:root /umu_bin 2>/dev/null || true
chmod 0755 /umu_bin 2>/dev/null || true

(
	{
		echo ""
		echo "===== boot begin ====="
		echo "[cmdline]"; cat /proc/cmdline 2>/dev/null || true
		echo "[mount]"; mount 2>/dev/null || true
		echo "===== boot end ====="
	} >> /logs/boot.log
) 2>/dev/null || true

# ネットワーク初期化（/etc/umu/network.conf をI/Fとする）
(
	CONF=/etc/umu/network.conf
	IFNAME="$(grep -m1 '^IFNAME=' "$CONF" 2>/dev/null | cut -d= -f2)"
	MODE="$(grep -m1 '^MODE=' "$CONF" 2>/dev/null | cut -d= -f2)"
	IP="$(grep -m1 '^IP=' "$CONF" 2>/dev/null | cut -d= -f2)"
	GW="$(grep -m1 '^GW=' "$CONF" 2>/dev/null | cut -d= -f2)"

	[ -n "$IFNAME" ] || IFNAME=eth0
	[ "$MODE" = "static" ] || exit 0
	[ -n "$IP" ] || exit 0
	[ -n "$GW" ] || exit 0

	ip link set dev "$IFNAME" up 2>/dev/null || true
	ip addr add "$IP" dev "$IFNAME" 2>/dev/null || true
	ip route replace default via "$GW" dev "$IFNAME" 2>/dev/null || true
) 2>/dev/null || true

( telnetd -p 23 -l /bin/login ) 2>/dev/null || true
( /umu_bin/ftpd_start ) 2>/dev/null || true

echo "[rcS] rcS done" > /dev/console 2>/dev/null || true
EOF
sudo chmod 755 /mnt/umuos015/etc/init.d/rcS

sudo tee /mnt/umuos015/etc/passwd >/dev/null <<'EOF'
root:x:0:0:root:/root:/bin/sh
tama:x:1000:1000:tama:/home/tama:/bin/sh
EOF

sudo tee /mnt/umuos015/etc/group >/dev/null <<'EOF'
root:x:0:
users:x:100:
tama:x:1000:
EOF

sudo mkdir -p /mnt/umuos015/root
sudo mkdir -p /mnt/umuos015/home/tama
sudo chown 1000:1000 /mnt/umuos015/home/tama

echo "[manual] next: generate password hashes, then paste into /mnt/umuos015/etc/shadow"
```

### 6.1 パスワード（手で貼る）

このブロックは「ハッシュを作るだけ」。出力を次の `shadow` ブロックに貼る。

```bash
openssl passwd -6
openssl passwd -6
```

`<...>` を置換して貼る。

```bash
sudo tee /mnt/umuos015/etc/shadow >/dev/null <<'EOF'
root:<rootの$6$...を貼る>:20000:0:99999:7:::
tama:<tamaの$6$...を貼る>:20000:0:99999:7:::
EOF

sudo chown root:root /mnt/umuos015/etc/shadow
sudo chmod 600 /mnt/umuos015/etc/shadow
```

---

## 7. 自作 su（/umu_bin/su）

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

cat > /home/tama/umu/umu_project/UmuOS-0.1.5-dev/work/umu_su.c <<'EOF'
#define _GNU_SOURCE

#include <crypt.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int read_shadow_hash_root(char *out, size_t out_len) {
	FILE *fp = fopen("/etc/shadow", "r");
	if (!fp) {
		perror("fopen(/etc/shadow)");
		return -1;
	}

	char line[2048];
	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, "root:", 5) != 0) {
			continue;
		}
		char *p = strchr(line, ':');
		if (!p) {
			break;
		}
		p++;
		char *q = strchr(p, ':');
		if (!q) {
			break;
		}
		*q = '\\0';

		if (strlen(p) == 0 || strcmp(p, "!") == 0 || strcmp(p, "*") == 0) {
			fprintf(stderr, "su: root password is locked/empty in /etc/shadow\\n");
			fclose(fp);
			return -1;
		}

		if (strlen(p) + 1 > out_len) {
			fprintf(stderr, "su: shadow hash too long\\n");
			fclose(fp);
			return -1;
		}

		strncpy(out, p, out_len);
		out[out_len - 1] = '\\0';
		fclose(fp);
		return 0;
	}

	fclose(fp);
	fprintf(stderr, "su: root entry not found in /etc/shadow\\n");
	return -1;
}

int main(void) {
	if (geteuid() != 0) {
		fprintf(stderr, "su: euid!=0 (setuid bit/owner/nosuid \\xe3\\x82\\x92\\xe7\\xa2\\xba\\xe8\\xaa\\x8d)\\n");
		return 1;
	}

	char shadow_hash[512];
	if (read_shadow_hash_root(shadow_hash, sizeof(shadow_hash)) != 0) {
		return 1;
	}

	char *pw = getpass("Password: ");
	if (!pw) {
		fprintf(stderr, "su: failed to read password\\n");
		return 1;
	}

	errno = 0;
	char *calc = crypt(pw, shadow_hash);
	if (!calc) {
		perror("crypt");
		return 1;
	}

	if (strcmp(calc, shadow_hash) != 0) {
		fprintf(stderr, "su: Authentication failure\\n");
		return 1;
	}

	if (setgid(0) != 0) {
		perror("setgid");
		return 1;
	}
	if (setuid(0) != 0) {
		perror("setuid");
		return 1;
	}

	execl("/bin/sh", "sh", (char *)NULL);
	perror("execl");
	return 1;
}
EOF

cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev/work
gcc -static -Os -s -o umu_su umu_su.c -lcrypt || true
if [ ! -f umu_su ]; then
	gcc -static -Os -s -o umu_su umu_su.c -lxcrypt
fi

sudo cp -f /home/tama/umu/umu_project/UmuOS-0.1.5-dev/work/umu_su /mnt/umuos015/umu_bin/su
sudo chown root:root /mnt/umuos015/umu_bin/su
sudo chmod 4755 /mnt/umuos015/umu_bin/su
```

---

## 8. アンマウント

```bash
sync
sudo umount /mnt/umuos015
```

---

## 9. ISO（grub.cfg + grub-mkrescue）

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

cat > /home/tama/umu/umu_project/UmuOS-0.1.5-dev/iso_root/boot/grub/grub.cfg <<'EOF'
set timeout=20
set default=0

serial --unit=0 --speed=115200 --word=8 --parity=no --stop=1
terminal_input serial console
terminal_output serial console

menuentry "UmuOS-0.1.5-dev" {
insmod gzio

linux /boot/vmlinuz-6.18.1 \
root=UUID=d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15 \
rw \
console=tty0 console=ttyS0,115200n8 \
loglevel=7 \
panic=-1 \
net.ifnames=0 biosdevname=0

initrd /boot/initrd.img-6.18.1
}
EOF

grub-mkrescue -o /home/tama/umu/umu_project/UmuOS-0.1.5-dev/UmuOS-0.1.5-boot.iso \
	/home/tama/umu/umu_project/UmuOS-0.1.5-dev/iso_root
```

---

## 10. Rocky 起動用（qemu.cmdline.txt）

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

cat > /home/tama/umu/umu_project/UmuOS-0.1.5-dev/run/qemu.cmdline.txt <<'EOF'
/usr/libexec/qemu-kvm \
-enable-kvm -cpu host -m 1024 \
-machine q35,accel=kvm \
-nographic \
-serial stdio \
-serial tcp:127.0.0.1:5555,server,nowait,telnet \
-drive file=./disk/disk.img,format=raw,if=virtio \
-cdrom ./UmuOS-0.1.5-boot.iso \
-boot order=d \
-netdev tap,id=net0,ifname=tap-umu,script=no,downscript=no \
-device virtio-net-pci,netdev=net0 \
-monitor none
EOF
```

---

## 11. 異常時だけ見る（切り分けメモ）

- kernel 成果物：`/home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build/arch/x86/boot/bzImage` が無い → `logs/kernel_build_bzImage.log` を確認（途中で中断すると出ない）。
- telnet の root だけ失敗：`/etc/securetty` を最優先。
- DNS 失敗：`/etc/resolv.conf`、`ping -c 1 8.8.8.8`（L3）、`ping -c 1 google.com`（DNS）。
- NTP 失敗：DNS/外界疎通の前提と BusyBox に `ntpd` が入っているか。
- `su` が `euid!=0`：disk.img 側の `chmod 4755` と、ゲスト側 `mount` の `nosuid` を確認。
- FTP 接続不可：ゲストで `ps` と `/run/ftpd.pid`、BusyBox に `tcpsvd`/`ftpd` があるか。
