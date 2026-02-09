---
title: UmuOS-0.1.5-dev 詳細設計書
date: 2026-02-09
base_design: "./UmuOS-0.1.5-dev-基本設計書.md"
base_procedure: "../../UmuOS-0.1.4-base-stable/docs/UmuOS-0.1.4-base-stable-詳細設計書.md"
notes: "../../UmuOS-0.1.4-base-stable/docs/UmuOS-0.1.4-base-stable-実装ノート.md"
status: draft
---

# UmuOS-0.1.5-dev 詳細設計書（手順：0.1.4踏襲 + DNS/JST/NTP/umu_bin/ll/su/ftpd）

この文書は、UmuOS-0.1.5-dev を「最初から再構築できる」状態にするための作業手順を、コマンド中心でまとめる。

重要方針：

- 0.1.4-base-stable の成立条件を壊さない。
- 0.1.5 の追加機能は、ext4 永続 rootfs 側（`disk.img`）へ確実に統合する（initramfs 側に書かない）。
- 0.1.4 実装ノートで判明したハマりどころ（BusyBox oldconfig、initrd filelist、securetty、nosuid 等）を手順に織り込む。
- コマンドは `/home/tama/...` など絶対パスで記載する（`$HOME` や `~` を使わない）。

---

## 0. 固定パラメータ（この文書で固定する値）

- 作業ルート（ビルド）：`/home/tama/umu/umu_project/UmuOS-0.1.5-dev`
- 配置ルート（起動・受入/Rocky）：`/root/UmuOS-0.1.5-dev`

- Kernel version：`6.18.1`
- BusyBox version：`1.36.1`

- ISO：`UmuOS-0.1.5-boot.iso`
- initrd：`initrd.img-6.18.1`
- kernel：`vmlinuz-6.18.1`

- 永続ディスク：`disk/disk.img`（ext4、4GiB、UUID固定）
- rootfs UUID（固定）：`d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15`

- UmuOS（ゲスト）IP：`192.168.0.202/24`
- GW：`192.168.0.1`
- DNS：`8.8.8.8`、`8.8.4.4`

- タイムゾーン：`/etc/TZ` に `JST-9`
- NTP サーバ：`time.google.com`

- FTP：`tcpsvd` 常駐 + `ftpd`（FTP_ROOT=`/tmp`、ポートは 21）

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

# 自作 su の静的リンクで必要になる可能性
sudo apt install -y libcrypt-dev || true
sudo apt install -y libxcrypt-dev || true
```

### 1.2 コマンド存在確認（観測点）

```bash
command -v grub-mkrescue
command -v mkfs.ext4
command -v musl-gcc
command -v cpio
command -v gzip
```

---

## 2. 作業ディレクトリ作成

```bash
mkdir -p /home/tama/umu/umu_project/UmuOS-0.1.5-dev
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

mkdir -p kernel/build \
         initramfs/src initramfs/rootfs \
         initramfs/busybox \
         iso_root/boot/grub \
         disk run logs docs
```

---

## 3. external の確認（入力ソース）

```bash
test -f /home/tama/umu/umu_project/external/linux-6.18.1-kernel/Makefile && echo OK || echo NG
test -f /home/tama/umu/umu_project/external/busybox-1.36.1/Makefile && echo OK || echo NG
```

`NG` が出た場合は、`/home/tama/umu/umu_project/external/` 配下に取得してから進む。

---

## 4. Kernel（6.18.1）ビルド（out-of-tree）

### 4.1 defconfig

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

make -C /home/tama/umu/umu_project/external/linux-6.18.1-kernel mrproper

rm -rf /home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build
mkdir -p /home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build

make -C /home/tama/umu/umu_project/external/linux-6.18.1-kernel \
  O=/home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build defconfig
```

### 4.2 必須設定（観測点）

```bash
grep -E '^(CONFIG_EXT4_FS=|CONFIG_DEVTMPFS=|CONFIG_DEVTMPFS_MOUNT=|CONFIG_BLK_DEV_INITRD=|CONFIG_VIRTIO=|CONFIG_VIRTIO_PCI=|CONFIG_VIRTIO_BLK=|CONFIG_VIRTIO_NET=|CONFIG_NET=|CONFIG_INET=|CONFIG_SERIAL_8250=|CONFIG_SERIAL_8250_CONSOLE=|CONFIG_DEVPTS_FS=|CONFIG_UNIX98_PTYS=|CONFIG_RD_GZIP=)' \
  /home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build/.config
```

不足がある場合のみ `scripts/config` で揃える。

```bash
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
```

### 4.3 ビルド

```bash
make -C /home/tama/umu/umu_project/external/linux-6.18.1-kernel \
  O=/home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build -j4
```

### 4.4 ISO入力へ配置

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev
cp -f kernel/build/arch/x86/boot/bzImage iso_root/boot/vmlinuz-6.18.1
cp -f kernel/build/.config iso_root/boot/config-6.18.1
file iso_root/boot/vmlinuz-6.18.1
```

---

## 5. BusyBox（1.36.1）ビルド（静的リンク）

external は参照のみ。作業用コピーを作ってビルドする。

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

rm -rf initramfs/busybox/work
mkdir -p initramfs/busybox/work
rsync -a --delete /home/tama/umu/umu_project/external/busybox-1.36.1/ initramfs/busybox/work/

cd initramfs/busybox/work
make distclean
make defconfig
make menuconfig
```

### 5.1 必須（0.1.4踏襲 + 0.1.5追加）

0.1.4 の必須：

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

0.1.5 の追加（機能要件）：

- `CONFIG_WGET=y`（HTTP 疎通確認用）
- `CONFIG_PING=y`（疎通確認用）
- `CONFIG_NSLOOKUP=y`（DNS 切り分け用。無い場合は `ping google.com` を代替）
- `CONFIG_NTPD=y`（時刻同期）
- `CONFIG_TCPSVD=y`（ftpd の前段）
- `CONFIG_FTPD=y`（FTP サーバ）

注意（ハマりどころ）：

- BusyBox は `olddefconfig` が無い。整合は `make oldconfig`。
- `tc`（`CONFIG_TC`）は環境によりコンパイルエラーになりやすい。必須でないため、ビルドが止まる場合は無効化する。

保存後：

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work
make oldconfig
cp -f .config /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/config-1.36.1
```

### 5.2 ビルドと簡易検査

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work
make -j4
file busybox

./busybox --list | grep -E '^(ip|telnetd|login|nc|wget|ping|ntpd|tcpsvd|ftpd)$' || echo NG
```

---

## 6. initramfs（initrd.img-6.18.1）生成

### 6.1 initramfs rootfs 作成（BusyBox + applet）

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

rm -rf initramfs/rootfs
mkdir -p initramfs/rootfs/{bin,sbin,etc,proc,sys,dev,dev/pts,run,newroot,tmp}

cp -f initramfs/busybox/work/busybox initramfs/rootfs/bin/busybox
chmod 755 initramfs/rootfs/bin/busybox

sudo chroot initramfs/rootfs /bin/busybox --install -s /bin
sudo chroot initramfs/rootfs /bin/busybox --install -s /sbin

ls -l initramfs/rootfs/bin/switch_root
ls -l initramfs/rootfs/sbin/getty
```

### 6.2 initramfs `/init`（0.1.4 を踏襲）

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

cp -f /home/tama/umu/umu_project/UmuOS-0.1.4-base-stable/initramfs/src/init.c initramfs/src/init.c

musl-gcc -static -O2 -Wall -Wextra -o initramfs/rootfs/init initramfs/src/init.c
chmod 755 initramfs/rootfs/init
file initramfs/rootfs/init
```

### 6.3 initrd 作成（cpio+gzip）

ハマりどころ：

- `find ... > initrd.filelist0` は無音が正常。
- `cpio` は `rootfs` をカレントにして実行する（基準ディレクトリを誤ると `stat` エラーになりやすい）。

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs

rm -f initrd.filelist0 initrd.cpio initrd.cpio.list initrd.img-6.18.1

find rootfs -mindepth 1 -printf '%P\0' > initrd.filelist0

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
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev/disk
rm -f disk.img
truncate -s 4G disk.img
mkfs.ext4 -F -U d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15 disk.img
sudo blkid -p -o value -s UUID disk.img
```

### 7.2 マウント（loop）

```bash
sudo mkdir -p /mnt/umuos015
sudo mount -o loop /home/tama/umu/umu_project/UmuOS-0.1.5-dev/disk/disk.img /mnt/umuos015
findmnt /mnt/umuos015
```

### 7.3 最小 rootfs（0.1.4踏襲）

```bash
sudo mkdir -p /mnt/umuos015/{bin,sbin,etc,proc,sys,dev,dev/pts,run,var,var/run,home,root,tmp,logs,etc/init.d,etc/umu,umu_bin}
```

### 7.4 BusyBox 配置（ext4側）

```bash
sudo cp -f /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work/busybox /mnt/umuos015/bin/busybox
sudo chown root:root /mnt/umuos015/bin/busybox
sudo chmod 755 /mnt/umuos015/bin/busybox

sudo chroot /mnt/umuos015 /bin/busybox --install -s /bin
sudo chroot /mnt/umuos015 /bin/busybox --install -s /sbin
sudo ln -sf /bin/busybox /mnt/umuos015/sbin/init
sudo ls -l /mnt/umuos015/sbin/init
```

### 7.5 inittab（ttyS0/ttyS1）

```bash
sudo tee /mnt/umuos015/etc/inittab >/dev/null <<'EOF'
::sysinit:/etc/init.d/rcS

ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
ttyS1::respawn:/sbin/getty -L 115200 ttyS1 vt100

::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a
EOF
```

### 7.6 network.conf（固定IP）

```bash
sudo tee /mnt/umuos015/etc/umu/network.conf >/dev/null <<'EOF'
IFNAME=eth0
MODE=static
IP=192.168.0.202/24
GW=192.168.0.1
DNS=8.8.8.8
EOF
```

### 7.7 DNS（/etc/resolv.conf）

```bash
sudo tee /mnt/umuos015/etc/resolv.conf >/dev/null <<'EOF'
nameserver 8.8.8.8
nameserver 8.8.4.4
EOF
```

### 7.8 JST（/etc/TZ）

```bash
sudo tee /mnt/umuos015/etc/TZ >/dev/null <<'EOF'
JST-9
EOF
```

### 7.9 securetty（telnet で root が落ちる典型対策）

```bash
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
```

### 7.10 `/umu_bin`（権限固定）

```bash
sudo chown root:root /mnt/umuos015/umu_bin
sudo chmod 0755 /mnt/umuos015/umu_bin
sudo ls -ld /mnt/umuos015/umu_bin
```

### 7.11 `ll`（/umu_bin/ll）

```bash
sudo tee /mnt/umuos015/umu_bin/ll >/dev/null <<'EOF'
#!/bin/sh
exec ls -lF "$@"
EOF
sudo chown root:root /mnt/umuos015/umu_bin/ll
sudo chmod 0755 /mnt/umuos015/umu_bin/ll
```

### 7.12 ftpd start/stop（/umu_bin）

```bash
sudo tee /mnt/umuos015/umu_bin/ftpd_start >/dev/null <<'EOF'
#!/bin/sh

mkdir -p /run

# 二重起動防止
if [ -f /run/ftpd.pid ] && kill -0 "$(cat /run/ftpd.pid)" 2>/dev/null; then
  exit 0
fi

# LAN内限定にしたい場合は 0.0.0.0 を 192.168.0.202 に変える
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
```

### 7.13 ntpd（手動同期スクリプト：/umu_bin/ntp_sync）

起動時にブートを遅くしないため、rcS で常駐同期はしない（手動で同期する）。

```bash
sudo tee /mnt/umuos015/umu_bin/ntp_sync >/dev/null <<'EOF'
#!/bin/sh

# まず疎通（失敗しても OS を止めない）
ping -c 1 8.8.8.8 >/dev/null 2>&1 || exit 1

# BusyBox 実装差があるため、-q が通るなら one-shot。
# -q が無い場合は foreground で実行し、必要なら Ctrl+C で止める。
ntpd -n -q -p time.google.com >/dev/null 2>&1 && exit 0
ntpd -n -p time.google.com
EOF
sudo chown root:root /mnt/umuos015/umu_bin/ntp_sync
sudo chmod 0755 /mnt/umuos015/umu_bin/ntp_sync
```

### 7.14 rcS（mount / boot.log / network / PATH / telnetd / ftpd）

`/etc/init.d/rcS` の `PATH` は `/umu_bin` 優先に固定する。

```bash
sudo tee /mnt/umuos015/etc/init.d/rcS >/dev/null <<'EOF'
#!/bin/sh

export PATH=/umu_bin:/sbin:/bin

mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mount -t devpts devpts /dev/pts 2>/dev/null || true

mkdir -p /logs /run /var/run /umu_bin 2>/dev/null || true
: > /var/run/utmp 2>/dev/null || true

# /umu_bin の権限を固定（念のため）
chown root:root /umu_bin 2>/dev/null || true
chmod 0755 /umu_bin 2>/dev/null || true

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

# ftpd（tcpsvd 常駐）
( /umu_bin/ftpd_start ) 2>/dev/null || true

echo "[rcS] rcS done" > /dev/console 2>/dev/null || true
EOF

sudo chmod 755 /mnt/umuos015/etc/init.d/rcS
```

### 7.15 ユーザー（root / tama）

```bash
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
```

パスワードは手動でハッシュ生成して `/etc/shadow` に貼る。

```bash
openssl passwd -6
openssl passwd -6
```

```bash
sudo tee /mnt/umuos015/etc/shadow >/dev/null <<'EOF'
root:<rootの$6$...を貼る>:20000:0:99999:7:::
tama:<tamaの$6$...を貼る>:20000:0:99999:7:::
EOF

sudo chown root:root /mnt/umuos015/etc/shadow
sudo chmod 600 /mnt/umuos015/etc/shadow
```

### 7.16 自作 su（/umu_bin/su）

BusyBox `su` の setuid 問題に依存せず、最小の `su` を自作して配置する。

#### 7.16.1 Ubuntu 側でソース作成

`/home/tama/umu/umu_project/UmuOS-0.1.5-dev/work/umu_su.c` を作成する。

```bash
mkdir -p /home/tama/umu/umu_project/UmuOS-0.1.5-dev/work
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
		*q = '\0';

		if (strlen(p) == 0 || strcmp(p, "!") == 0 || strcmp(p, "*") == 0) {
			fprintf(stderr, "su: root password is locked/empty in /etc/shadow\n");
			fclose(fp);
			return -1;
		}

		if (strlen(p) + 1 > out_len) {
			fprintf(stderr, "su: shadow hash too long\n");
			fclose(fp);
			return -1;
		}

		strncpy(out, p, out_len);
		out[out_len - 1] = '\0';
		fclose(fp);
		return 0;
	}

	fclose(fp);
	fprintf(stderr, "su: root entry not found in /etc/shadow\n");
	return -1;
}

int main(void) {
	if (geteuid() != 0) {
		fprintf(stderr, "su: euid!=0 (setuid bit/owner/nosuid を確認)\n");
		return 1;
	}

	char shadow_hash[512];
	if (read_shadow_hash_root(shadow_hash, sizeof(shadow_hash)) != 0) {
		return 1;
	}

	char *pw = getpass("Password: ");
	if (!pw) {
		fprintf(stderr, "su: failed to read password\n");
		return 1;
	}

	errno = 0;
	char *calc = crypt(pw, shadow_hash);
	if (!calc) {
		perror("crypt");
		return 1;
	}

	if (strcmp(calc, shadow_hash) != 0) {
		fprintf(stderr, "su: Authentication failure\n");
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
```

#### 7.16.2 静的リンクでビルド

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev/work

# まずは -lcrypt を試す
gcc -static -Os -s -o umu_su umu_su.c -lcrypt || true

# 失敗した場合のみ -lxcrypt を試す
if [ ! -f umu_su ]; then
  gcc -static -Os -s -o umu_su umu_su.c -lxcrypt
fi

file umu_su
```

#### 7.16.3 disk.img へ配置（setuid）

```bash
sudo cp -f /home/tama/umu/umu_project/UmuOS-0.1.5-dev/work/umu_su /mnt/umuos015/umu_bin/su
sudo chown root:root /mnt/umuos015/umu_bin/su
sudo chmod 4755 /mnt/umuos015/umu_bin/su
sudo ls -l /mnt/umuos015/umu_bin/su
```

注意（ハマりどころ）：

- `/` が `nosuid` でマウントされていると setuid は効かない。ゲストの `mount` で切り分ける。

### 7.17 アンマウント

```bash
sync
sudo umount /mnt/umuos015
```

---

## 8. ISO（UmuOS-0.1.5-boot.iso）生成

### 8.1 grub.cfg

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

cat > iso_root/boot/grub/grub.cfg <<'EOF'
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
```

### 8.2 ISO生成

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev
grub-mkrescue -o UmuOS-0.1.5-boot.iso iso_root
ls -lh UmuOS-0.1.5-boot.iso
```

---

## 9. run/（Rocky 側の起動コマンド）

```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

cat > run/qemu.cmdline.txt <<'EOF'
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

sed -n '1,120p' run/qemu.cmdline.txt
```

---

## 10. Rocky（起動・受入）

### 10.1 必要パッケージ（Rocky）

```bash
dnf -y install qemu-kvm qemu-img tmux util-linux iproute
test -x /usr/libexec/qemu-kvm
```

### 10.2 配置（Ubuntu → Rocky）

配置先（Rocky）：`/root/UmuOS-0.1.5-dev/`

コピー対象（最低限）：

- `/home/tama/umu/umu_project/UmuOS-0.1.5-dev/UmuOS-0.1.5-boot.iso`
- `/home/tama/umu/umu_project/UmuOS-0.1.5-dev/disk/disk.img`
- `/home/tama/umu/umu_project/UmuOS-0.1.5-dev/run/qemu.cmdline.txt`

起動補助スクリプトは 0.1.4-base-stable を踏襲し、0.1.5-dev 側にも用意する（実装は別タスク）。

- `umuOSstart.sh`（QEMU起動、ttyS0ログ採取）
- `connect_ttyS1.sh`（ttyS1接続、ログ採取）
- `run/tap_up.sh`、`run/tap_down.sh`（tap作成/削除）

### 10.3 受入（合格条件）

0.1.4互換：

- ttyS0/ttyS1 で `root`/`tama` ログイン
- `/logs/boot.log` 追記
- telnet（TCP/23）で `root`/`tama` ログイン

0.1.5追加：

- DNS：`ping -c 1 google.com` が成功
- JST：`cat /etc/TZ` が `JST-9`
- NTP：`/umu_bin/ntp_sync` を実行し、`date` が更新される
- `/umu_bin`：`which ll` が `/umu_bin/ll` を指す
- `su`：`tama` でログイン後 `/umu_bin/su` で root へ切替
- FTP：Ubuntu から FTP 接続し `binary` で `get/put`

---

## 11. トラブルシュート（短縮）

- telnet の root だけ失敗：`/etc/securetty` を最優先
- DNS が失敗：`/etc/resolv.conf`、`ping -c 1 8.8.8.8`（L3）、`ping -c 1 google.com`（DNS）
- NTP が失敗：ネットワーク/DNS 前提を満たすか、BusyBox に `ntpd` が入っているか
- `su` が `euid!=0`：`chmod 4755`、所有者、`mount` の `nosuid` を確認
- FTP 接続不可：ゲストで `busybox --list` に `tcpsvd`/`ftpd` があるか、`/run/ftpd.pid`、ポート21の bind に失敗していないか
