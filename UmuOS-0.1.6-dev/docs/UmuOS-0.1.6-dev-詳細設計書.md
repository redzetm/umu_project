---
title: UmuOS-0.1.6-dev 詳細設計書
date: 2026-02-11
status: clean-reproducible-manual
---

# UmuOS-0.1.6-dev 詳細設計書

目的：UmuOS-0.1.6-dev を **最初のPKGインストールから**、途中で修正を挟まずに最後まで再現できる手順にする。

この文書のルール（今回の反省を踏まえた固定ルール）：

- コマンドは必ずコードブロック（```bash）に入れる。
- `rcS` は「テンプレ1本」を作り、disk.imgへは `install` で配置する（rcS二重管理を禁止）。
- `PATH/TZ/NTP/FTP公開ルート` は disk.img 側に統合し、起動のたびに同じ動きをする。
- Rocky 側は `/root` に **ISO + disk.img + start.sh の3つだけ**で起動できる形にする。

この文書は「ブロック単位でコピペしていけば完走できる」粒度を目標にしている。
しかし、このOSは研究用OSなので、意味を分かってコピペしていただきたい気持ちです。

**環境依存（KVM可否・ブリッジ有無・パッケージ名差）** はゼロにできないため、事前チェックを必ず通す。

設計思想：UmuOS は「使うためのOS」ではなく「理解するためのOS」である。
したがって各ステップの **観測点** は「成功/失敗」の判定だけでなく、どの層（bootloader/kernel/initramfs/rootfs/userspace）が支配しているかを切り分けるために置く。

### 0.0 事前チェック（ここだけ最初に実行）

観測点：ここで詰まるなら、以降のコピペは高確率で失敗する。
理解の狙い：実装手順に入る前に「ホストの前提条件」を確定し、失敗原因をOS側に寄せて観測できる状態にする。

```bash
set -e

# ツールの存在（インストール済みの確認）
command -v make gcc >/dev/null
command -v grub-mkrescue xorriso mformat mcopy >/dev/null
command -v cpio gzip mkfs.ext4 mount umount >/dev/null

# QEMU（起動ホストで起動する場合のみ必要。Rocky/Ubuntuどちらでも）
command -v qemu-system-x86_64 >/dev/null || command -v qemu-kvm >/dev/null

# KVM（使えると高速。無い場合は起動が極端に遅い/失敗しうる）
test -e /dev/kvm || echo "WARN: /dev/kvm が無い（KVM無しでの起動は非推奨）"

# ディスク空き（目安。kernel+busybox+ISO+disk.imgで数GB）
df -h /home || true
```

---

## 0. 固定値（読むだけでOK / コマンドに直書き）

- 作業ルート（Ubuntu）：`/home/tama/umu_project/UmuOS-0.1.6-dev`
- Kernel source：`/home/tama/umu_project/external/linux-6.18.1`
- BusyBox source：`/home/tama/umu_project/external/busybox-1.36.1`

- Kernel version：`6.18.1`
- BusyBox version：`1.36.1`

- ISO：`UmuOS-0.1.6-boot.iso`
- initrd：`initrd.img-6.18.1`
- kernel：`vmlinuz-6.18.1`

- 永続ディスク：`disk/disk.img`（ext4、4GiB、UUID固定）
- rootfs UUID：`d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15`

- ゲストIP：`192.168.0.202/24`
- GW：`192.168.0.1`
- DNS：`8.8.8.8`、`8.8.4.4`

- タイムゾーン：`JST-9`
- NTP サーバ：`time.google.com`

- ttyS1 TCP シリアル（Rocky側）：`127.0.0.1:5555`
- TAP 名（Rocky側）：`tap-umu`
- ブリッジ名（Rocky側）：`br0`

---

## 1. Ubuntu 事前準備（パッケージ）

観測点：`netcat-openbsd` を入れておく（転送/切り分け用）。
理解の狙い：観測・転送の道具を最小セットで固定し、環境差分によるブレを減らす。

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

# `libxcrypt-dev` は環境によっては見つからないので、存在する場合だけ入れる
sudo apt install -y software-properties-common || true
sudo add-apt-repository -y universe || true
sudo apt update || true
apt-cache show libxcrypt-dev >/dev/null 2>&1 && sudo apt install -y libxcrypt-dev || true
```

---

## 2. 作業ディレクトリ（初期化）

観測点：外部ソース（kernel/busybox）が存在する。
理解の狙い：成果物（kernel/busybox/ISO/disk.img）の依存関係を「パスの固定」で見える化する。

```bash
mkdir -p /home/tama/umu_project/UmuOS-0.1.6-dev
cd /home/tama/umu_project/UmuOS-0.1.6-dev

mkdir -p kernel/build \
	initramfs/src initramfs/rootfs \
	initramfs/busybox \
	iso_root/boot/grub \
	disk run logs work tools

test -f /home/tama/umu_project/external/linux-6.18.1/Makefile
test -f /home/tama/umu_project/external/busybox-1.36.1/Makefile
```

---

## 2.1 生成物テンプレ（rcS/差し替えツール/起動スクリプト）

ここは「最初から完成形」を作る。途中で直さない。

### 2.1.1 rcSテンプレ（唯一の正）

観測点：`/logs/boot.log` に `boot_id/time/uptime` が追記される。
理解の狙い：`init` → `inittab` → `rcS` のユーザーランド初期化が実際に走っていることを、永続ログで観測する。

```bash
cat > /home/tama/umu_project/UmuOS-0.1.6-dev/tools/rcS_umuos016.sh <<'EOF'
#!/bin/sh

export PATH=/umu_bin:/sbin:/bin
export TZ=JST-9

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
		echo "[boot_id]"; cat /proc/sys/kernel/random/boot_id 2>/dev/null || true
		echo "[time]"; date -R 2>/dev/null || date 2>/dev/null || true
		echo "[uptime]"; cat /proc/uptime 2>/dev/null || true
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

# 時刻同期（ネットワーク初期化後に1回だけ）
(
	echo "[ntp_sync] before: $(date -R 2>/dev/null || date)" >> /logs/boot.log
	/umu_bin/ntp_sync >> /logs/boot.log 2>&1 || true
	echo "[ntp_sync] after : $(date -R 2>/dev/null || date)" >> /logs/boot.log
) 2>/dev/null || true

( telnetd -p 23 -l /bin/login ) 2>/dev/null || true
( /umu_bin/ftpd_start ) 2>/dev/null || true

echo "[rcS] rcS done" > /dev/console 2>/dev/null || true
EOF

chmod +x /home/tama/umu_project/UmuOS-0.1.6-dev/tools/rcS_umuos016.sh
```

### 2.1.2 既存disk.imgへ rcS 差し替えツール（安全弁）

使うのは「どうしてもやり直したくない時」だけ。基本はこの文書の手順で1発で完成する。

```bash
cat > /home/tama/umu_project/UmuOS-0.1.6-dev/tools/patch_diskimg_rcS.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

say() { echo "[patch_diskimg_rcS] $*"; }
die() { echo "[patch_diskimg_rcS] ERROR: $*" >&2; exit 1; }

usage() {
	cat <<'USAGE'
Usage:
  sudo bash ./tools/patch_diskimg_rcS.sh /path/to/disk.img [path/to/new_rcS]

Defaults:
  new_rcS = ./tools/rcS_umuos016.sh

Environment:
  MNT_DIR=/mnt/umuos016_patch
USAGE
}

DISK_IMG="${1:-}"
NEW_RCS="${2:-}"

if [[ -z "${DISK_IMG}" || "${DISK_IMG}" == "-h" || "${DISK_IMG}" == "--help" ]]; then
	usage
	exit 2
fi

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -z "${NEW_RCS}" ]]; then
	NEW_RCS="${BASE_DIR}/tools/rcS_umuos016.sh"
fi

if [[ ${EUID} -ne 0 ]]; then
	say "Re-running via sudo..."
	exec sudo -E "${BASH_SOURCE[0]}" "$@"
fi

[[ -f "${DISK_IMG}" ]] || die "disk image not found: ${DISK_IMG}"
[[ -f "${NEW_RCS}" ]] || die "rcS file not found: ${NEW_RCS}"

command -v mount >/dev/null 2>&1 || die "missing command: mount"
command -v umount >/dev/null 2>&1 || die "missing command: umount"
command -v mountpoint >/dev/null 2>&1 || die "missing command: mountpoint"

MNT_DIR="${MNT_DIR:-/mnt/umuos016_patch}"

cleanup() {
	sync || true
	if mountpoint -q "${MNT_DIR}"; then
		umount "${MNT_DIR}" || true
	fi
}
trap cleanup EXIT

mkdir -p "${MNT_DIR}"

if mountpoint -q "${MNT_DIR}"; then
	die "already mounted: ${MNT_DIR} (umount it first)"
fi

say "mounting: ${DISK_IMG} -> ${MNT_DIR}"
mount -o loop "${DISK_IMG}" "${MNT_DIR}"

mkdir -p "${MNT_DIR}/etc/init.d"

TS="$(date +%Y%m%d_%H%M%S)"
if [[ -f "${MNT_DIR}/etc/init.d/rcS" ]]; then
	say "backup: /etc/init.d/rcS -> /etc/init.d/rcS.bak.${TS}"
	cp -a "${MNT_DIR}/etc/init.d/rcS" "${MNT_DIR}/etc/init.d/rcS.bak.${TS}"
fi

say "install new rcS"
cp -f "${NEW_RCS}" "${MNT_DIR}/etc/init.d/rcS"
chown root:root "${MNT_DIR}/etc/init.d/rcS" || true
chmod 0755 "${MNT_DIR}/etc/init.d/rcS"

say "done"
EOF

chmod +x /home/tama/umu_project/UmuOS-0.1.6-dev/tools/patch_diskimg_rcS.sh
```

### 2.1.3 Rocky起動スクリプト（/rootに3ファイルだけ）

観測点：`ifname=` のtypoを絶対に入れない（以前の事故ポイント）。
理解の狙い：ネットワークが壊れたとき「ゲストの設定」ではなく「ホストの起動引数ミス」という層の切り分けを最初に潰す。

```bash
cat > /home/tama/umu_project/UmuOS-0.1.6-dev/UmuOS-0.1.6-dev_start.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISO_FILE="${BASE_DIR}/UmuOS-0.1.6-boot.iso"
DISK_FILE="${BASE_DIR}/disk.img"

TTYS1_PORT="${TTYS1_PORT:-5555}"
TAP_IF="${TAP_IF:-tap-umu}"
BRIDGE="${BRIDGE:-br0}"
NET_MODE="${NET_MODE:-tap}"

say() { echo "[UmuOS-0.1.6-dev_start] $*"; }
die() { echo "[UmuOS-0.1.6-dev_start] ERROR: $*" >&2; exit 1; }

[[ -f "${ISO_FILE}" ]] || die "file not found: ${ISO_FILE}"
[[ -f "${DISK_FILE}" ]] || die "file not found: ${DISK_FILE}"

need_cmd() { command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }
find_qemu() {
	if [[ -x /usr/libexec/qemu-kvm ]]; then echo /usr/libexec/qemu-kvm; return 0; fi
	if command -v qemu-kvm >/dev/null 2>&1; then command -v qemu-kvm; return 0; fi
	if command -v qemu-system-x86_64 >/dev/null 2>&1; then command -v qemu-system-x86_64; return 0; fi
	return 1
}

if [[ ${EUID} -ne 0 ]]; then
	say "This script usually needs root (KVM/TAP). Re-running via sudo..."
	exec sudo -E "${BASH_SOURCE[0]}" "$@"
fi

need_cmd ip
need_cmd script

QEMU_BIN="$(find_qemu)" || die "qemu binary not found"

if [[ "${NET_MODE}" == "tap" ]]; then
	if ! ip link show "${BRIDGE}" >/dev/null 2>&1; then
		die "bridge '${BRIDGE}' not found (or set NET_MODE=none)"
	fi
	if ! ip link show "${TAP_IF}" >/dev/null 2>&1; then
		ip link del dev "${TAP_IF}" >/dev/null 2>&1 || true
		ip tuntap add dev "${TAP_IF}" mode tap
		ip link set dev "${TAP_IF}" master "${BRIDGE}"
		ip link set dev "${TAP_IF}" up
		say "created tap '${TAP_IF}' and attached to '${BRIDGE}'"
	else
		ip link set dev "${TAP_IF}" master "${BRIDGE}" >/dev/null 2>&1 || true
		ip link set dev "${TAP_IF}" up >/dev/null 2>&1 || true
	fi
elif [[ "${NET_MODE}" == "none" ]]; then
	:
else
	die "invalid NET_MODE='${NET_MODE}' (expected: tap|none)"
fi

TS="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${BASE_DIR}/host_qemu.console_${TS}.log"

QEMU_CMD=(
	"${QEMU_BIN}"
	-enable-kvm -cpu host -m 1024
	-machine q35,accel=kvm
	-nographic
	-serial stdio
	-serial "tcp:127.0.0.1:${TTYS1_PORT},server,nowait,telnet"
	-drive "file=${DISK_FILE},format=raw,if=virtio"
	-cdrom "${ISO_FILE}"
	-boot order=d
	-monitor none
)

if [[ "${NET_MODE}" == "tap" ]]; then
	QEMU_CMD+=(
		-netdev "tap,id=net0,ifname=${TAP_IF},script=no,downscript=no"
		-device virtio-net-pci,netdev=net0
	)
fi

say "qemu: ${QEMU_BIN}"
say "log: ${LOG_FILE}"
say "iso: ${ISO_FILE}"
say "disk: ${DISK_FILE}"
say "net: ${NET_MODE}"

CMD_STR="$(printf '%q ' "${QEMU_CMD[@]}")"
exec script -q -f -c "${CMD_STR}" "${LOG_FILE}"
EOF

chmod +x /home/tama/umu_project/UmuOS-0.1.6-dev/UmuOS-0.1.6-dev_start.sh
```

---

## 3. Kernel（out-of-tree）

観測点：ビルドが途中で止まっていないこと（ログにエラーが無い）。
理解の狙い：カーネルは全レイヤの土台なので、失敗を最初に除去して以降の観測を userspace 側に集中させる。

```bash
cd /home/tama/umu_project/UmuOS-0.1.6-dev

make -C /home/tama/umu_project/external/linux-6.18.1 mrproper

rm -rf /home/tama/umu_project/UmuOS-0.1.6-dev/kernel/build
mkdir -p /home/tama/umu_project/UmuOS-0.1.6-dev/kernel/build

make -C /home/tama/umu_project/external/linux-6.18.1 \
	O=/home/tama/umu_project/UmuOS-0.1.6-dev/kernel/build defconfig

/home/tama/umu_project/external/linux-6.18.1/scripts/config \
	--file /home/tama/umu_project/UmuOS-0.1.6-dev/kernel/build/.config \
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

make -C /home/tama/umu_project/external/linux-6.18.1 \
	O=/home/tama/umu_project/UmuOS-0.1.6-dev/kernel/build olddefconfig

make -C /home/tama/umu_project/external/linux-6.18.1 \
	O=/home/tama/umu_project/UmuOS-0.1.6-dev/kernel/build -j4 bzImage \
	2>&1 | tee /home/tama/umu_project/UmuOS-0.1.6-dev/logs/kernel_build_bzImage.log

mkdir -p /home/tama/umu_project/UmuOS-0.1.6-dev/iso_root/boot
cp -f /home/tama/umu_project/UmuOS-0.1.6-dev/kernel/build/arch/x86/boot/bzImage \
	/home/tama/umu_project/UmuOS-0.1.6-dev/iso_root/boot/vmlinuz-6.18.1
cp -f /home/tama/umu_project/UmuOS-0.1.6-dev/kernel/build/.config \
	/home/tama/umu_project/UmuOS-0.1.6-dev/iso_root/boot/config-6.18.1

test -f /home/tama/umu_project/UmuOS-0.1.6-dev/iso_root/boot/vmlinuz-6.18.1
```

---

## 4. BusyBox（静的リンク、対話なし）

観測点：`busybox` が static で、`ntpd/tcpsvd/ftpd` が有効。
理解の狙い：UmuOSのユーザーランド機能は BusyBox の設定で成立する（=「何が入っているか」を自分で把握して観測できる）ことを確認する。

```bash
cd /home/tama/umu_project/UmuOS-0.1.6-dev

rm -rf /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/busybox/work
mkdir -p /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/busybox/work
rsync -a --delete /home/tama/umu_project/external/busybox-1.36.1/ \
	/home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/busybox/work/

cd /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/busybox/work
make distclean
make defconfig

cat >> .config <<'EOF'
CONFIG_STATIC=y
CONFIG_INIT=y
CONFIG_FEATURE_USE_INITTAB=y
CONFIG_GETTY=y
CONFIG_SWITCH_ROOT=y
CONFIG_TELNETD=y
CONFIG_FEATURE_TELNETD_STANDALONE=y
CONFIG_LOGIN=y
# /etc/shadow を使う（ここが無いと login/su 周りの挙動が環境差になりやすい）
CONFIG_FEATURE_SHADOWPASSWDS=y
CONFIG_IP=y
CONFIG_NC=y

CONFIG_WGET=y
CONFIG_PING=y
CONFIG_NSLOOKUP=y
CONFIG_NTPD=y
CONFIG_TCPSVD=y
CONFIG_FTPD=y

# `tc` は環境差分でビルドが落ちることがあるため明示的に無効化
# CONFIG_TC is not set
# CONFIG_FEATURE_TC_INGRESS is not set
EOF

yes "" | make oldconfig

if egrep -q '^(# CONFIG_STATIC is not set|CONFIG_TC=y|CONFIG_FEATURE_TC_INGRESS=y)$' .config; then
	sed -i \
		-e 's/^# CONFIG_STATIC is not set$/CONFIG_STATIC=y/' \
		-e 's/^CONFIG_TC=y$/# CONFIG_TC is not set/' \
		-e 's/^CONFIG_FEATURE_TC_INGRESS=y$/# CONFIG_FEATURE_TC_INGRESS is not set/' \
		.config
	yes "" | make oldconfig
fi

cp -f .config /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/busybox/config-1.36.1
make -j4 2>&1 | tee /home/tama/umu_project/UmuOS-0.1.6-dev/logs/busybox_build.log

ls -l busybox
file busybox
./busybox --list | egrep '^(ntpd|tcpsvd|ftpd)$'
```

---

## 5. initramfs（initrd.img-6.18.1）

観測点：`initrd.img-6.18.1` が `iso_root/boot/` にコピーされている。
理解の狙い：GRUB → kernel → initrd の“つなぎ目”はファイル配置で決まるため、成果物の存在を物で確認する。

```bash
cd /home/tama/umu_project/UmuOS-0.1.6-dev

rm -rf /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/rootfs
mkdir -p /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/rootfs/{bin,sbin,etc,proc,sys,dev,dev/pts,run,newroot,tmp}

cp -f /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/busybox/work/busybox \
	/home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/rootfs/bin/busybox
chmod 755 /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/rootfs/bin/busybox

sudo chroot /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/rootfs /bin/busybox --install -s /bin
sudo chroot /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/rootfs /bin/busybox --install -s /sbin

cp -f /home/tama/umu_project/UmuOS-0.1.4-base-stable/initramfs/src/init.c \
	/home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/src/init.c
musl-gcc -static -O2 -Wall -Wextra \
	-o /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/rootfs/init \
	/home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/src/init.c
chmod 755 /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/rootfs/init

cd /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs
rm -f initrd.filelist0 initrd.cpio initrd.img-6.18.1
find rootfs -mindepth 1 -printf '%P\0' > initrd.filelist0
cd rootfs
cpio --null -ov --format=newc < ../initrd.filelist0 > ../initrd.cpio
cd ..
gzip -9 -c initrd.cpio > initrd.img-6.18.1

mkdir -p /home/tama/umu_project/UmuOS-0.1.6-dev/iso_root/boot
cp -f initrd.img-6.18.1 /home/tama/umu_project/UmuOS-0.1.6-dev/iso_root/boot/initrd.img-6.18.1

test -f /home/tama/umu_project/UmuOS-0.1.6-dev/iso_root/boot/initrd.img-6.18.1
```

---

## 6. disk.img（永続 rootfs）

観測点：`/etc/profile` で `PATH` と `TZ` が固定され、`rcS` がテンプレ版になっている。
理解の狙い：`switch_root` 後の永続 rootfs が「毎回の起動挙動」を決めることを観測する（initramfs と責務を分離）。

```bash
cd /home/tama/umu_project/UmuOS-0.1.6-dev

rm -f /home/tama/umu_project/UmuOS-0.1.6-dev/disk/disk.img
truncate -s 4G /home/tama/umu_project/UmuOS-0.1.6-dev/disk/disk.img
mkfs.ext4 -F -U d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15 /home/tama/umu_project/UmuOS-0.1.6-dev/disk/disk.img

sudo mkdir -p /mnt/umuos016
sudo mount -o loop /home/tama/umu_project/UmuOS-0.1.6-dev/disk/disk.img /mnt/umuos016

sudo mkdir -p /mnt/umuos016/{bin,sbin,etc,proc,sys,dev,dev/pts,run,var,var/run,home,root,tmp,logs,etc/init.d,etc/umu,umu_bin}

sudo cp -f /home/tama/umu_project/UmuOS-0.1.6-dev/initramfs/busybox/work/busybox /mnt/umuos016/bin/busybox
sudo chown root:root /mnt/umuos016/bin/busybox
sudo chmod 755 /mnt/umuos016/bin/busybox
sudo chroot /mnt/umuos016 /bin/busybox --install -s /bin
sudo chroot /mnt/umuos016 /bin/busybox --install -s /sbin
sudo ln -sf /bin/busybox /mnt/umuos016/sbin/init

sudo tee /mnt/umuos016/etc/inittab >/dev/null <<'EOF'
::sysinit:/etc/init.d/rcS

ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
ttyS1::respawn:/sbin/getty -L 115200 ttyS1 vt100

::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a
EOF

sudo tee /mnt/umuos016/etc/umu/network.conf >/dev/null <<'EOF'
IFNAME=eth0
MODE=static
IP=192.168.0.202/24
GW=192.168.0.1
DNS=8.8.8.8
EOF

sudo tee /mnt/umuos016/etc/resolv.conf >/dev/null <<'EOF'
nameserver 8.8.8.8
nameserver 8.8.4.4
EOF

sudo tee /mnt/umuos016/etc/TZ >/dev/null <<'EOF'
JST-9
EOF

sudo tee /mnt/umuos016/etc/securetty >/dev/null <<'EOF'
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

sudo tee /mnt/umuos016/etc/profile >/dev/null <<'EOF'
export PATH=/umu_bin:/sbin:/bin
export TZ=JST-9
EOF

sudo tee /mnt/umuos016/etc/passwd >/dev/null <<'EOF'
root:x:0:0:root:/root:/bin/sh
tama:x:1000:1000:tama:/home/tama:/bin/sh
EOF

sudo tee /mnt/umuos016/etc/group >/dev/null <<'EOF'
root:x:0:
users:x:100:
tama:x:1000:
EOF

sudo mkdir -p /mnt/umuos016/root
sudo mkdir -p /mnt/umuos016/home/tama
sudo chown 1000:1000 /mnt/umuos016/home/tama

sudo chown root:root /mnt/umuos016/umu_bin
sudo chmod 0755 /mnt/umuos016/umu_bin

sudo tee /mnt/umuos016/umu_bin/ll >/dev/null <<'EOF'
#!/bin/sh
exec ls -l "$@"
EOF
sudo chown root:root /mnt/umuos016/umu_bin/ll
sudo chmod 0755 /mnt/umuos016/umu_bin/ll

sudo tee /mnt/umuos016/umu_bin/ftpd_start >/dev/null <<'EOF'
#!/bin/sh

mkdir -p /run

if [ -f /run/ftpd.pid ] && kill -0 "$(cat /run/ftpd.pid)" 2>/dev/null; then
	exit 0
fi

# FTP の公開ルート（/ にすると全ディレクトリが見える）
busybox tcpsvd -vE 0.0.0.0 21 busybox ftpd / &
echo $! > /run/ftpd.pid
EOF
sudo chown root:root /mnt/umuos016/umu_bin/ftpd_start
sudo chmod 0755 /mnt/umuos016/umu_bin/ftpd_start

sudo tee /mnt/umuos016/umu_bin/ftpd_stop >/dev/null <<'EOF'
#!/bin/sh

if [ -f /run/ftpd.pid ]; then
	kill "$(cat /run/ftpd.pid)" 2>/dev/null || true
	rm -f /run/ftpd.pid
fi
EOF
sudo chown root:root /mnt/umuos016/umu_bin/ftpd_stop
sudo chmod 0755 /mnt/umuos016/umu_bin/ftpd_stop

sudo tee /mnt/umuos016/umu_bin/ntp_sync >/dev/null <<'EOF'
#!/bin/sh
ping -c 1 8.8.8.8 >/dev/null 2>&1 || exit 1
ntpd -n -q -p time.google.com >/dev/null 2>&1 && exit 0
ntpd -n -p time.google.com
EOF
sudo chown root:root /mnt/umuos016/umu_bin/ntp_sync
sudo chmod 0755 /mnt/umuos016/umu_bin/ntp_sync

# rcS はテンプレ1本からインストール（この文書の完成形）
sudo install -m 0755 /home/tama/umu_project/UmuOS-0.1.6-dev/tools/rcS_umuos016.sh /mnt/umuos016/etc/init.d/rcS
```

### 6.1 パスワード（手で貼る）

観測点：このブロックだけは手入力が必要（ハッシュ）。
理解の狙い：認証は /etc/shadow の内容に直結する（=成果物へ埋め込むデータ）ため、手作業箇所を明示して再現の揺れをここに隔離する。

```bash
openssl passwd -6
openssl passwd -6
```

```bash
sudo tee /mnt/umuos016/etc/shadow >/dev/null <<'EOF'
root:<rootの$6$...を貼る>:20000:0:99999:7:::
tama:<tamaの$6$...を貼る>:20000:0:99999:7:::
EOF

sudo chown root:root /mnt/umuos016/etc/shadow
sudo chmod 600 /mnt/umuos016/etc/shadow
```

---

## 7. 自作 su（/umu_bin/su）

観測点：`chmod 4755` を必ず通す（root切替の成立条件）。
理解の狙い：setuid による euid 変化（権限昇格の成立条件）を自作実装で観測できるようにする。

```bash
cd /home/tama/umu_project/UmuOS-0.1.6-dev

cat > /home/tama/umu_project/UmuOS-0.1.6-dev/work/umu_su.c <<'EOF'
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
		if (strncmp(line, "root:", 5) != 0) continue;
		char *p = strchr(line, ':'); if (!p) break;
		p++;
		char *q = strchr(p, ':'); if (!q) break;
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
	if (read_shadow_hash_root(shadow_hash, sizeof(shadow_hash)) != 0) return 1;

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

	if (setgid(0) != 0) { perror("setgid"); return 1; }
	if (setuid(0) != 0) { perror("setuid"); return 1; }

	execl("/bin/sh", "sh", (char *)NULL);
	perror("execl");
	return 1;
}
EOF

cd /home/tama/umu_project/UmuOS-0.1.6-dev/work
gcc -static -Os -s -o umu_su umu_su.c -lcrypt || true
if [ ! -f umu_su ]; then
	gcc -static -Os -s -o umu_su umu_su.c -lxcrypt
fi

sudo cp -f /home/tama/umu_project/UmuOS-0.1.6-dev/work/umu_su /mnt/umuos016/umu_bin/su
sudo chown root:root /mnt/umuos016/umu_bin/su
sudo chmod 4755 /mnt/umuos016/umu_bin/su
```

---

## 8. アンマウント

観測点：`/mnt/umuos016` 配下にいない状態で実行する（busy回避）。
理解の狙い：ホストの loop mount は「ファイル=ディスク」を扱う基本なので、状態管理（busy/umount）がどう失敗するかを体験しつつ避ける。

```bash
cd /
sync
sudo umount /mnt/umuos016
```

---

## 9. ISO（grub.cfg + grub-mkrescue）

観測点：ISOが生成され、ファイルサイズが0でない。
理解の狙い：ブートの入口（GRUB設定+kernel引数+initrd指定）が「ISOという1ファイル」に閉じることを確認する。

```bash
cd /home/tama/umu_project/UmuOS-0.1.6-dev

cat > /home/tama/umu_project/UmuOS-0.1.6-dev/iso_root/boot/grub/grub.cfg <<'EOF'
set timeout=20
set default=0

serial --unit=0 --speed=115200 --word=8 --parity=no --stop=1
terminal_input serial console
terminal_output serial console

menuentry "UmuOS-0.1.6-dev" {
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

grub-mkrescue -o /home/tama/umu_project/UmuOS-0.1.6-dev/UmuOS-0.1.6-boot.iso \
	/home/tama/umu_project/UmuOS-0.1.6-dev/iso_root

ls -l /home/tama/umu_project/UmuOS-0.1.6-dev/UmuOS-0.1.6-boot.iso
```

---

## 10. Rocky 起動（/rootに3ファイルだけ）

この章は Rocky を例に書くが、起動スクリプトは **QEMU/KVM が使えるホスト**であれば同様に動く。

- `NET_MODE=tap`：ブリッジ `br0` が存在すること（無い環境では失敗する）
- `NET_MODE=none`：ブリッジ無しでも起動できる（ネット機能の検証はできない）

Rocky の `/root` に置く：

- `UmuOS-0.1.6-boot.iso`
- `disk.img`（Ubuntuで作った `disk/disk.img` を `disk.img` にして置く）
- `UmuOS-0.1.6-dev_start.sh`

### 10.0 Rocky へ 3ファイルを転送（例）

観測点：Rocky の `/root` に **3ファイルが揃っている**。
理解の狙い：「OSと開発環境の分離」を守り、実行環境に持ち込む最小単位を固定する。

この文書の標準は `nc`（netcat）で転送する（SSH/鍵/権限で詰まらないようにするため）。
ここは **1ポートで順番に送る**（端末を増やさない）。
`ROCKY_HOST` は自分の環境に合わせて置き換える。

事前（Rocky側）：

```bash
command -v nc >/dev/null || sudo dnf install -y nmap-ncat

# 受信用ポートを開ける（環境によって不要）
sudo firewall-cmd --add-port=12345/tcp --permanent || true
sudo firewall-cmd --reload || true
```

```bash
# Rocky（受信側）
cd /root

# 1本ずつ受ける（受信コマンドを叩く→送信→完了したら次へ）
nc -4 -l 12345 > UmuOS-0.1.6-boot.iso
nc -4 -l 12345 > disk.img
nc -4 -l 12345 > UmuOS-0.1.6-dev_start.sh
```

```bash
# Ubuntu（送信側）
cd /home/tama/umu_project/UmuOS-0.1.6-dev

ROCKY_HOST=192.168.0.200

nc -4 ${ROCKY_HOST} 12345 < UmuOS-0.1.6-boot.iso
nc -4 ${ROCKY_HOST} 12345 < disk/disk.img
nc -4 ${ROCKY_HOST} 12345 < UmuOS-0.1.6-dev_start.sh
```

```bash
# Rocky（受信側）
cd /root
ls -l UmuOS-0.1.6-boot.iso disk.img UmuOS-0.1.6-dev_start.sh
```

起動：

```bash
cd /root
sudo ./UmuOS-0.1.6-dev_start.sh
```

### 10.1 Tera Term 接続（ttyS1）

目的：ゲストの `ttyS1` を **TCP で取っている** ので、Tera Term から観測/操作できる。

- 接続先：`127.0.0.1`
- ポート：`5555`
- プロトコル：`Telnet`

CLI で代替する場合（Rocky側）：

```bash
telnet 127.0.0.1 5555
```

ログイン後の観測点：

理解の狙い：シリアル経由の getty/login の経路が成立していることを確認し、以降の観測（ネット/rcS/log）が Tera Term だけで完結できるようにする。

- `login:` が出る（`/etc/inittab` の `ttyS1::respawn:/sbin/getty ...`）
- `root` / `tama` のどちらでもログインできる（`/etc/passwd` と `/etc/shadow`）

ネット無し起動（切り分け用）：

```bash
cd /root
sudo NET_MODE=none ./UmuOS-0.1.6-dev_start.sh
```

---

## 11. 成功判定（この通りなら「1発で行けた」）

ゲストで確認：

- `/logs/boot.log` に `boot_id` と `[ntp_sync] before/after` がある
- `echo $PATH` の先頭が `/umu_bin`
- `date` がJSTで出る（NTPが通れば時刻も合う）
- FTP が起動している（`/run/ftpd.pid` がある）

Tera Term でそのまま貼れる確認コマンド：

```sh
echo "[whoami]"; whoami
echo "[PATH]"; echo "$PATH"
echo "[TZ]"; cat /etc/TZ 2>/dev/null || true
echo "[date]"; date -R 2>/dev/null || date

echo "[boot.log tail]"; tail -n 80 /logs/boot.log 2>/dev/null || true

echo "[telnetd]"; ps w | grep -E 'telnetd|\[telnetd\]' | grep -v grep || true
echo "[ftpd pid]"; ls -l /run/ftpd.pid 2>/dev/null || true
echo "[ftpd ps]"; ps w | grep -E 'tcpsvd|ftpd|\[tcpsvd\]|\[ftpd\]' | grep -v grep || true
```

---

## 12. 異常時だけ見る（切り分けメモ）

- kernel 成果物が無い：`logs/kernel_build_bzImage.log` を確認（途中中断で出ない）。
- `boot.log` が増えない：`/etc/inittab` の `::sysinit:/etc/init.d/rcS` を確認。
- NTPが失敗：外界疎通（`ping -c 1 8.8.8.8`）とDNS（`/etc/resolv.conf`）を確認。
- `su` が `euid!=0`：`chmod 4755` と `nosuid` マウント有無。

### Rocky へ転送（nc メモ / 任意・統一ポート12345）

転送は 10章の `nc` 手順を使う（ISO/disk.img/start.sh の3本を揃える）。
