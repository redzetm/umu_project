# UmuOS-0.2.1-dev 詳細設計書

この方式（QEMU の user-net + hostfwd + virtio-net）は、1 つのグローバル IP でホストとゲストを共存させつつ、公開面と復旧経路をホスト側で管理しやすい点に強みがある。

メリット

- 1 つのグローバル IP で、ホストとゲストの両方にアクセスできる。
- RockyLinux（ホスト）の SSH や管理サービスをそのまま維持できる。
- QEMU の hostfwd 機能により、同じ IP の別ポートを UmuOS ゲストへ転送できる。
	例:
	- `ssh rocky_ip` → RockyLinux にログイン
	- `telnet rocky_ip 23` → UmuOS ゲストの telnetd に接続
	- `ftp rocky_ip 21` → UmuOS ゲストの ftpd に接続
- ファイアウォール制御を RockyLinux 側で一元管理できる。
- RockyLinux の firewalld や iptables により、外部公開ポートを柔軟に制御できる。
- ゲスト側で細かい FW 設定を持たなくても、ホスト側で全体を保護できる。
- トラブル時でも RockyLinux から直接操作できる。
- ゲスト（UmuOS）が不調でも、ホスト（RockyLinux）へ SSH 等で入り、QEMU プロセスの再起動、ログ確認、イメージ修復ができる。
- 万一ゲストがネットワーク的に孤立しても、ホストから直接介入できるため復旧しやすい。
- QEMU の user-net はブリッジ不要で手軽に扱える。
- 物理 NIC やブリッジ設定が不要なため、VPS 環境でも比較的安全に使いやすい。
- 複雑なネットワーク構成を避けたい場合に向いている。

まとめ

1 つのグローバル IP で「ホスト（RockyLinux）」と「ゲスト（UmuOS）」の両方にアクセスできる。
この方式は、セキュリティ、運用、復旧の観点で扱いやすい構成である。


---

## 0. 確定仕様（この文書の最終解釈）

この章を、この文書の最優先仕様とする。以降の手順はすべてこの章を満たすための実装手順である。

### 0.1 採用範囲

- 0.2.1-dev は、`0.1.7-base-stable` の全機能を機械的に丸ごと引き継ぐものではない。
- 0.2.1-dev は、`0.1.7-base-stable` の既知動作を土台として、必要な機能を 0.2.1 向けに再構成して実装する。
- この文書で 0.1.7 からそのまま採用すると確定しているものは、`initramfs/src/init.c` である。
- この文書で確定対象に含める機能は、次の 5 系統だけである。
	- RockyLinux 9.7 上での QEMU 起動
	- telnet 接続
	- FTP ログインと upload/download
	- アクセスログ
	- 永続 rootfs と起動観測
- `0.1.7-base-stable` に存在した他機能（例：`su`、`ll`、その他補助コマンド群）は、この文書では継承確定対象に含めない。必要なら別仕様として後から追加する。

### 0.2 サービス仕様

- ホスト OS は RockyLinux 9.7 とする。
- UmuOS は RockyLinux 上の QEMU ゲストとして起動する。
- 外部から RockyLinux の `23/tcp` に接続した場合、UmuOS ゲストの telnet に到達する。
- 外部から RockyLinux の `21/tcp` に接続した場合、UmuOS ゲストの FTP 制御接続に到達する。
- FTP データ接続は passive mode 前提とし、`21000-21031/tcp` を利用する。
- QEMU の `hostfwd` と RockyLinux の `firewalld` は、`23/tcp`、`21/tcp`、`21000-21031/tcp` を一致して開ける。

### 0.3 FTP 仕様

- FTP ログインは `tama` ユーザーで行う。
- FTP の公開ルートは `/` とする。したがって外部クライアントは `/` 配下を閲覧できる。
- ただしアップロード先は `/home/tama/inbox` に固定する。
- `/umu_bin` へ直接アップロードする運用は、禁止とする。
- FTP の upload/download 成立条件は、BusyBox `ftpd.c` の最小改造で passive port 範囲と PASV 応答 IP を固定することで満たす。

### 0.4 ログ仕様

- `telnet` 接続記録と `login` の認証系ログは `/var/log/access.log` に残す。
- ここでいう認証系ログとは、少なくとも `telnet connect from ...`、`invalid password`、`root login` を含む。
- FTP の接続・転送に関する動作ログは、主に `/var/log/messages` で観測する。
- したがって、この文書における「アクセスログが取れる」とは、`/var/log/access.log` の auth 系記録と `/var/log/messages` の FTP 系記録の両方が観測できる状態を意味する。

### 0.5 成功条件

- RockyLinux 上で QEMU が起動する。
- telnet でログインできる。
- FTP でログインできる。
- FTP で upload/download ができる。
- `/var/log/access.log` と `/var/log/messages` の両方で必要なログを確認できる。


---

## 0.0 事前チェック（ここだけ最初に実行）

目的：この手順は「UbuntuでISO+disk.imgを作る」→「RockyLinux9.7へ3ファイルを置く」→「Rocky上でQEMU起動（user-net＋hostfwd＋virtio-net）」までを、途中で修正を挟まず完走できる粒度にする。

この文書のルール：

- コマンドは必ずコードブロック（```bash）に入れる。
- `rcS` は「テンプレ1本」を作り、disk.imgへは `install` で配置する（rcS二重管理を禁止）。
- `PATH/TZ/NTP/ログ設定` は disk.img 側に統合し、起動のたびに同じ動きをする。
- FTP は `/` を公開ルートにするが、アップロード先は `/home/tama/inbox` に固定する。
- FTP のデータ接続は passive mode 前提とし、`21000-21031/tcp` を固定で使う。
- Rocky 側は `/home/tama/umuos021` に **ISO + disk.img + start.sh の3つだけ**で起動できる形にする。

観測点：ここで詰まるなら、以降のコピペは高確率で失敗する。
理解の狙い：ホスト（Ubuntu/Rocky）側の前提を確定し、失敗原因をOS側に寄せて観測できる状態にする。

```bash
set -e

# Ubuntuで必要（ビルド用）
command -v make gcc >/dev/null
command -v grub-mkrescue xorriso mformat mcopy >/dev/null
command -v cpio gzip mkfs.ext4 mount umount >/dev/null
command -v openssl rsync >/dev/null

# QEMU（Rockyで必要）
command -v qemu-system-x86_64 >/dev/null || command -v qemu-kvm >/dev/null || test -x /usr/libexec/qemu-kvm

# KVM（Rockyで使えると高速）
test -e /dev/kvm || echo "WARN: /dev/kvm が無い（KVM無しは遅い/失敗しうる）"

df -h /home || true
```

---

## 0. 固定値（読むだけでOK / コマンドに直書き）

- 作業ルート（Ubuntu）：`/home/tama/umu_project/work/umuos021`
- Kernel source：`/home/tama/umu_project/external/linux-6.18.1`
- BusyBox source：`/home/tama/umu_project/external/busybox-1.36.1`

- Kernel version：`6.18.1`
- BusyBox version：`1.36.1`

- ISO：`UmuOS-0.2.1-dev-boot.iso`
- initrd：`initrd.img-6.18.1`
- kernel：`vmlinuz-6.18.1`

- 永続ディスク：`disk/disk.img`（ext4、4GiB、UUID固定）
- rootfs UUID：`<固定UUID>`

- ゲストIP（QEMU user-net/SLIRP想定）：`10.0.2.15/24`
- GW：`10.0.2.2`
- DNS：`8.8.8.8`、`8.8.4.4`
- Rocky 公開IP：`<グローバルIP>`

- hostfwd（Rocky→ゲスト）：
	- `23/tcp` → telnet
	- `21/tcp` → FTP制御
	- `21000-21031/tcp` → FTP passive data

- タイムゾーン：`JST-9`
- NTP サーバ：`time.google.com`
- 一般ログ：`/var/log/messages`
- アクセスログ：`/var/log/access.log`
- 起動観測ログ：`/logs/boot.log`
- FTP受け取り場所：`/home/tama/inbox`
- FTP passive range：`21000-21031`

- Rocky 作業ディレクトリ：`/home/tama/umuos021`
- Rockyへ移す成果物：
  - `UmuOS-0.2.1-dev-boot.iso`
  - `disk.img`
  - `UmuOS-0.2.1-dev_start.sh`

---

## 1. Ubuntu 事前準備（パッケージ）

観測点：`grub-mkrescue` が存在する（ISOが作れる）。
理解の狙い：Kernel/BusyBox/ISO作成に必要な道具を最小セットで固定し、環境差分によるブレを減らす。

```bash
sudo apt update
sudo apt install -y \
	build-essential bc bison flex libssl-dev libelf-dev libncurses-dev dwarves \
	git wget rsync \
	grub-efi-amd64-bin grub-common xorriso mtools \
	cpio gzip xz-utils \
	e2fsprogs util-linux \
	musl-tools \
	openssl telnet netcat-openbsd

# 0.2.1 初版でも採用する既知動作構成
# 自作認証系や crypt 依存の切り分けで必要になる可能性（入らなくても後で対処できる）
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
mkdir -p /home/tama/umu_project/work/umuos021
cd /home/tama/umu_project/work/umuos021

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

観測点：`/logs/boot.log` に `boot_id/time/uptime/cmdline` が追記される。
理解の狙い：`init` → `inittab` → `rcS` のユーザーランド初期化が走っていることを、永続ログで観測する。

```bash
cat > /home/tama/umu_project/work/umuos021/tools/rcS_umuos021.sh <<'EOF'
#!/bin/sh

export PATH=/umu_bin:/sbin:/bin
export TZ=JST-9

mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mount -t devpts devpts /dev/pts 2>/dev/null || true

mkdir -p /logs /run /var/run /var/log /umu_bin /home /home/tama /home/tama/inbox 2>/dev/null || true
: > /var/run/utmp 2>/dev/null || true
: > /var/log/messages 2>/dev/null || true
: > /var/log/access.log 2>/dev/null || true

chown root:root /umu_bin 2>/dev/null || true
chmod 0755 /umu_bin 2>/dev/null || true
chown 1000:1000 /home/tama /home/tama/inbox 2>/dev/null || true
chmod 0755 /home/tama 2>/dev/null || true
chmod 0750 /home/tama/inbox 2>/dev/null || true

# 軽量ログ基盤。login/telnetd の syslog を /var/log へ残しつつ、logread 用のIPCバッファも持つ。
syslogd -O /var/log/messages -C16 -s 200 -b 3 2>/dev/null || true
klogd 2>/dev/null || true

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

# FTP は / を読めるままにし、書き込みは /home/tama/inbox へ集約する。
( /umu_bin/ftpd_start ) 2>/dev/null || true

# telnetd は BusyBox 側の最小改造で接続元IPを login -h に渡す
( telnetd -p 23 -l /bin/login ) 2>/dev/null || true

echo "[rcS] rcS done" > /dev/console 2>/dev/null || true
EOF

chmod +x /home/tama/umu_project/work/umuos021/tools/rcS_umuos021.sh
```

### 2.1.2 Rocky起動スクリプト（/home/tama/umuos021に3ファイルだけ）

観測点：hostfwdの指定ミス（ポートや書式）を絶対に入れない。
理解の狙い：ネットワークが壊れたとき「ゲストの設定」ではなく「ホストの起動引数ミス」という層の切り分けを最初に潰す。

```bash
cat > /home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev_start.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISO_FILE="${BASE_DIR}/UmuOS-0.2.1-dev-boot.iso"
DISK_FILE="${BASE_DIR}/disk.img"

# hostfwd で 23 / 21 / 21000-21031 をバインドするため、通常 root が必要
TELNET_PORT="${TELNET_PORT:-23}"
FTP_PORT="${FTP_PORT:-21}"
FTP_PASV_MIN="${FTP_PASV_MIN:-21000}"
FTP_PASV_MAX="${FTP_PASV_MAX:-21031}"

say() { echo "[UmuOS-0.2.1-dev_start] $*"; }
die() { echo "[UmuOS-0.2.1-dev_start] ERROR: $*" >&2; exit 1; }

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
	say "Re-running via sudo (need to bind privileged ports via hostfwd)..."
	exec sudo -E "${BASH_SOURCE[0]}" "$@"
fi

need_cmd script
QEMU_BIN="$(find_qemu)" || die "qemu binary not found"

ACCEL_ARGS=()
if [[ -e /dev/kvm ]]; then
	ACCEL_ARGS=( -enable-kvm -machine q35,accel=kvm -cpu host )
else
	# KVM無し（TCG）でも最低限動く形にする
	ACCEL_ARGS=( -machine q35 -cpu qemu64 )
fi

TS="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${BASE_DIR}/host_qemu.console_${TS}.log"
QEMU_NETDEV="user,id=n1,hostfwd=tcp::${TELNET_PORT}-:23,hostfwd=tcp::${FTP_PORT}-:21"

for ((port=FTP_PASV_MIN; port<=FTP_PASV_MAX; port++)); do
	QEMU_NETDEV+=",hostfwd=tcp::${port}-:${port}"
done

QEMU_CMD=(
	"${QEMU_BIN}"
	"${ACCEL_ARGS[@]}" -m 1024
	-smp 2
	-nographic
	-serial stdio
	-monitor none
	-drive "file=${DISK_FILE},format=raw,if=virtio,cache=none,aio=native"
	-cdrom "${ISO_FILE}"
	-boot order=d
	-netdev "${QEMU_NETDEV}"
	-device virtio-net-pci,netdev=n1
)

say "qemu: ${QEMU_BIN}"
say "log : ${LOG_FILE}"
say "iso : ${ISO_FILE}"
say "disk: ${DISK_FILE}"
say "fwd : ${TELNET_PORT}->23, ${FTP_PORT}->21, ${FTP_PASV_MIN}-${FTP_PASV_MAX}->${FTP_PASV_MIN}-${FTP_PASV_MAX}"

CMD_STR="$(printf '%q ' "${QEMU_CMD[@]}")"
exec script -q -f -c "${CMD_STR}" "${LOG_FILE}"
EOF

chmod +x /home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev_start.sh
```

---

## 3. Kernel（out-of-tree）

観測点：ビルドが途中で止まっていないこと（ログにエラーが無い）。
理解の狙い：カーネルは全レイヤの土台なので、失敗を最初に除去して以降の観測を userspace 側に集中させる。

```bash
cd /home/tama/umu_project/work/umuos021

rm -rf /home/tama/umu_project/work/umuos021/kernel/build
mkdir -p /home/tama/umu_project/work/umuos021/kernel/build

# 注意：external配下（ソースツリー）を掃除すると他作業に影響するので、O=（ビルドディレクトリ）だけを掃除する
make -C /home/tama/umu_project/external/linux-6.18.1 \
	O=/home/tama/umu_project/work/umuos021/kernel/build mrproper

make -C /home/tama/umu_project/external/linux-6.18.1 \
	O=/home/tama/umu_project/work/umuos021/kernel/build defconfig

/home/tama/umu_project/external/linux-6.18.1/scripts/config \
	--file /home/tama/umu_project/work/umuos021/kernel/build/.config \
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
	O=/home/tama/umu_project/work/umuos021/kernel/build olddefconfig

make -C /home/tama/umu_project/external/linux-6.18.1 \
	O=/home/tama/umu_project/work/umuos021/kernel/build -j"$(nproc)" bzImage \
	2>&1 | tee /home/tama/umu_project/work/umuos021/logs/kernel_build_bzImage.log

mkdir -p /home/tama/umu_project/work/umuos021/iso_root/boot
cp -f /home/tama/umu_project/work/umuos021/kernel/build/arch/x86/boot/bzImage \
	/home/tama/umu_project/work/umuos021/iso_root/boot/vmlinuz-6.18.1
cp -f /home/tama/umu_project/work/umuos021/kernel/build/.config \
	/home/tama/umu_project/work/umuos021/iso_root/boot/config-6.18.1

test -f /home/tama/umu_project/work/umuos021/iso_root/boot/vmlinuz-6.18.1
```

---

## 4. BusyBox（静的リンク、対話なし）

観測点：`busybox` が static で、`telnetd/login/tcpsvd/ftpd/ntpd/syslogd/logger/logread` が有効。
理解の狙い：UmuOSのユーザーランド機能は BusyBox の設定で成立する（=「何が入っているか」を自分で把握して観測できる）。FTP も同じ BusyBox ソースの上で成立させる。

```bash
cd /home/tama/umu_project/work/umuos021

rm -rf /home/tama/umu_project/work/umuos021/initramfs/busybox/work
mkdir -p /home/tama/umu_project/work/umuos021/initramfs/busybox/work
rsync -a --delete /home/tama/umu_project/external/busybox-1.36.1/ \
	/home/tama/umu_project/work/umuos021/initramfs/busybox/work/

cd /home/tama/umu_project/work/umuos021/initramfs/busybox/work
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
CONFIG_FEATURE_SHADOWPASSWDS=y
CONFIG_TCPSVD=y
CONFIG_FTPD=y
CONFIG_FEATURE_FTPD_WRITE=y
CONFIG_KLOGD=y
CONFIG_LOGGER=y
CONFIG_LOGREAD=y
CONFIG_FEATURE_LOGREAD_REDUCED_LOCKING=y
CONFIG_SYSLOGD=y
CONFIG_FEATURE_ROTATE_LOGFILE=y
CONFIG_FEATURE_SYSLOGD_CFG=y
CONFIG_FEATURE_IPC_SYSLOG=y
CONFIG_FEATURE_IPC_SYSLOG_BUFFER_SIZE=16

CONFIG_IP=y
CONFIG_NC=y

CONFIG_WGET=y
CONFIG_PING=y
CONFIG_NSLOOKUP=y

CONFIG_NTPD=y

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

cp -f .config /home/tama/umu_project/work/umuos021/initramfs/busybox/config-1.36.1
```

### 4.1 telnetd.c を自分で読める形で最小改造する

観測点：`make_new_session()` の中で「接続元IPをどこで取り、どこで login に渡すか」が自分の目で追える。
理解の狙い：`patch` を機械適用するのではなく、BusyBox の元コードを読みながら UmuOS 用の改造点を把握する。

ここで編集するのは `telnet.c` ではない。編集対象は BusyBox ソースツリーの `networking/telnetd.c` である。

この節で実際にやる作業は、次の 4 手順だけである。

1. `networking/telnetd.c` を開く。
2. 既存の `make_new_session()` 関数の場所を特定する。
3. その関数の先頭 `static struct tsession *` から、関数末尾の `}` までを丸ごと選択する。
4. その範囲だけを、この文書のコードで置き換える。

重要：差し替えるのは「関数の一部」ではなく `make_new_session()` 関数全体である。つまり、`networking/telnetd.c` の中で他の関数や include を触ってはいけない。

- 編集対象は `networking/telnetd.c` の `make_new_session()` だけに限定する。
- やりたいことは 3 つだけである。
- `vfork()` の前に接続元IPを文字列化する。
- 親プロセスで `telnet connect from ...` を syslog へ出す。
- 子プロセスで `login -h 接続元IP` を実行する。

```bash
cd /home/tama/umu_project/work/umuos021/initramfs/busybox/work

grep -n 'const char \*login_argv' networking/telnetd.c
grep -n 'fflush_all' networking/telnetd.c
grep -n 'write_new_utmp' networking/telnetd.c
grep -n 'login_argv\[0\] = G.loginpath' networking/telnetd.c

cp -f networking/telnetd.c /home/tama/umu_project/work/umuos021/work/telnetd.c.before_accesslog
```

上の `grep` は「どこを置き換えるか」を見失わないための目印である。

- `const char *login_argv` が見えたら、この関数の login 実行部に入っている。
- `fflush_all` が見えたら、`vfork()` の直前付近まで来ている。
- `write_new_utmp` が見えたら、子プロセス側の初期化部分に入っている。
- `login_argv[0] = G.loginpath` が見えたら、関数末尾の exec 部分に近い。

つまり、これら 4 つの目印が全部 1 つの関数の中に入っていることを確認してから、その関数全体を差し替える。

`networking/telnetd.c` の `make_new_session()` を、関数全体ごと以下の内容で置き換える。

```c
static struct tsession *
make_new_session(
		IF_FEATURE_TELNETD_STANDALONE(int sock)
		IF_NOT_FEATURE_TELNETD_STANDALONE(void)
) {
#if !ENABLE_FEATURE_TELNETD_STANDALONE
	enum { sock = 0 };
#endif
	const char *login_argv[4];
	struct termios termbuf;
	int fd, pid;
	char tty_name[GETPTY_BUFSIZE];
	char *hostname = NULL;
	struct tsession *ts = xzalloc(sizeof(struct tsession) + BUFSIZE * 2);

	/*ts->buf1 = (char *)(ts + 1);*/
	/*ts->buf2 = ts->buf1 + BUFSIZE;*/

	/* Got a new connection, set up a tty */
	fd = xgetpty(tty_name);
	if (fd > G.maxfd)
		G.maxfd = fd;
	ts->ptyfd = fd;
	ndelay_on(fd);
	close_on_exec_on(fd);

	/* SO_KEEPALIVE by popular demand */
	setsockopt_keepalive(sock);
#if ENABLE_FEATURE_TELNETD_STANDALONE
	ts->sockfd_read = sock;
	ndelay_on(sock);
	if (sock == 0) {
		sock++;
		ndelay_on(sock);
	}
	ts->sockfd_write = sock;
	if (sock > G.maxfd)
		G.maxfd = sock;
#else
	/* ts->sockfd_read = 0; - done by xzalloc */
	ts->sockfd_write = 1;
	ndelay_on(0);
	ndelay_on(1);
#endif

	/* Make the telnet client understand we will echo characters so it
	 * should not do it locally. We don't tell the client to run linemode,
	 * because we want to handle line editing and tab completion and other
	 * stuff that requires char-by-char support. */
	{
		static const char iacs_to_send[] ALIGN1 = {
			IAC, DO, TELOPT_ECHO,
			IAC, DO, TELOPT_NAWS,
			/*IAC, DO, TELOPT_LFLOW,*/
			IAC, WILL, TELOPT_ECHO,
			IAC, WILL, TELOPT_SGA
		};
#if ENABLE_FEATURE_TELNETD_STANDALONE
		safe_write(sock, iacs_to_send, sizeof(iacs_to_send));
#else
		safe_write(1, iacs_to_send, sizeof(iacs_to_send));
#endif
	}

	fflush_all();

	/* vfork 前に接続元IPを確定しておく。 */
	{
		len_and_sockaddr *lsa = get_peer_lsa(sock);
		if (lsa) {
			hostname = xmalloc_sockaddr2dotted(&lsa->u.sa);
			free(lsa);
		}
	}

	pid = vfork(); /* NOMMU-friendly */
	if (pid < 0) {
		free(hostname);
		free(ts);
		close(fd);
		/* sock will be closed by caller */
		bb_simple_perror_msg("vfork");
		return NULL;
	}
	if (pid > 0) {
		/* Parent */
		syslog(LOG_AUTH | LOG_INFO, "telnet connect from '%s' on '%s'",
				hostname ? hostname : "unknown", tty_name);
		free(hostname);
		ts->shell_pid = pid;
		return ts;
	}

	/* Child */
	/* Careful - we are after vfork! */

	/* Restore default signal handling ASAP */
	bb_signals((1 << SIGCHLD) + (1 << SIGPIPE), SIG_DFL);

	pid = getpid();

	if (ENABLE_FEATURE_UTMP) {
		write_new_utmp(pid, LOGIN_PROCESS, tty_name, /*username:*/ "LOGIN", hostname);
	}

	/* Make new session and process group */
	setsid();

	/* Open the child's side of the tty */
	/* NB: setsid() disconnects from any previous ctty's. Therefore
	 * we must open child's side of the tty AFTER setsid! */
	close(0);
	xopen(tty_name, O_RDWR); /* becomes our ctty */
	xdup2(0, 1);
	xdup2(0, 2);
	tcsetpgrp(0, pid); /* switch this tty's process group to us */

	/* The pseudo-terminal allocated to the client is configured to operate
	 * in cooked mode, and with XTABS CRMOD enabled (see tty(4)) */
	tcgetattr(0, &termbuf);
	termbuf.c_lflag |= ECHO; /* if we use readline we dont want this */
	termbuf.c_oflag |= ONLCR | XTABS;
	termbuf.c_iflag |= ICRNL;
	termbuf.c_iflag &= ~IXOFF;
	/*termbuf.c_lflag &= ~ICANON;*/
	tcsetattr_stdin_TCSANOW(&termbuf);

	/* Uses FILE-based I/O to stdout, but does fflush_all(),
	 * so should be safe with vfork.
	 * I fear, though, that some users will have ridiculously big
	 * issue files, and they may block writing to fd 1,
	 * (parent is supposed to read it, but parent waits
	 * for vforked child to exec!) */
	print_login_issue(G.issuefile, tty_name);

	/* Exec shell / login / whatever */
	login_argv[0] = G.loginpath;
	if (hostname) {
		login_argv[1] = "-h";
		login_argv[2] = hostname;
		login_argv[3] = NULL;
	} else {
		login_argv[1] = NULL;
	}
	/* exec busybox applet (if PREFER_APPLETS=y), if that fails,
	 * exec external program.
	 * NB: sock is either 0 or has CLOEXEC set on it.
	 * fd has CLOEXEC set on it too. These two fds will be closed here.
	 */
	BB_EXECVP(G.loginpath, (char **)login_argv);
	_exit(EXIT_FAILURE); /*bb_perror_msg_and_die("execv %s", G.loginpath);*/
}
```

この節の要点を短く言うと、次の理解でよい。

- `telnet.c` を触るのではなく `networking/telnetd.c` を触る。
- `make_new_session()` という 1 関数だけを丸ごと差し替える。
- 差し替え後の意味は「親が接続元IPを記録し、子が `login -h <接続元IP>` を起動する」である。

### 4.2 ftpd.c を自分で読める形で最小改造する

観測点：FTP の passive port が常に `21000-21031` の範囲に入り、PASV 応答のIPが `133.18.181.45` になる。
理解の狙い：`21/tcp` を開けるだけでは外部 upload/download は安定しない。`ftpd.c` のどこで passive port を決め、どこでクライアントへ返すIPを組み立てるかを自分の目で追えるようにする。

- 編集対象は `networking/ftpd.c` の passive mode 周辺だけに限定する。
- やりたいことは 3 つだけである。
- passive data port を `21000-21031` に固定する。
- PASV 応答で返すIPを `UMU_FTP_PASV_ADDR` から取る。
- rcS から渡した環境変数だけで設定値を変えられるようにする。

```bash
cd /home/tama/umu_project/work/umuos021/initramfs/busybox/work

grep -n 'bind_for_passive_mode' networking/ftpd.c
grep -n 'handle_pasv' networking/ftpd.c
grep -n 'handle_epsv' networking/ftpd.c

cp -f networking/ftpd.c /home/tama/umu_project/work/umuos021/work/ftpd.c.before_pasv_fix
```

`networking/ftpd.c` の `bind_for_passive_mode()` の直前へ、以下の補助関数群を追加する。

```c
static unsigned
umu_ftp_pasv_min_port(void)
{
	const char *text = getenv("UMU_FTP_PASV_MIN");
	unsigned value;

	errno = 0;
	value = text ? bb_strtou(text, NULL, 10) : 21000;
	if (errno || value == 0 || value > 65535)
		return 21000;
	return value;
}

static unsigned
umu_ftp_pasv_max_port(void)
{
	const char *text = getenv("UMU_FTP_PASV_MAX");
	unsigned value;

	errno = 0;
	value = text ? bb_strtou(text, NULL, 10) : 21031;
	if (errno || value == 0 || value > 65535)
		return 21031;
	return value;
}

static char *
umu_ftp_pasv_addr(void)
{
	const char *text = getenv("UMU_FTP_PASV_ADDR");

	if (text && text[0] != '\0')
		return xstrdup(text);
	if (G.local_addr->u.sa.sa_family == AF_INET)
		return xmalloc_sockaddr2dotted_noport(&G.local_addr->u.sa);
	return xstrdup("0.0.0.0");
}
```

`bind_for_passive_mode()` を、関数全体ごと以下の内容で置き換える。

```c
static unsigned
bind_for_passive_mode(void)
{
	static unsigned next_port;
	unsigned min_port;
	unsigned max_port;
	unsigned tries;
	int fd;

	port_pasv_cleanup();

	G.pasv_listen_fd = fd = xsocket(G.local_addr->u.sa.sa_family, SOCK_STREAM, 0);
	setsockopt_reuseaddr(fd);

	min_port = umu_ftp_pasv_min_port();
	max_port = umu_ftp_pasv_max_port();
	if (max_port < min_port)
		max_port = min_port;
	if (next_port < min_port || next_port > max_port)
		next_port = min_port;

	tries = max_port - min_port + 1;
	while (tries--) {
		unsigned current_port = next_port;

		next_port++;
		if (next_port > max_port)
			next_port = min_port;

		set_nport(&G.local_addr->u.sa, htons(current_port));
		if (bind(fd, &G.local_addr->u.sa, G.local_addr->len) == 0) {
			xlisten(fd, 1);
			getsockname(fd, &G.local_addr->u.sa, &G.local_addr->len);
			return current_port;
		}
	}

	bb_simple_perror_msg_and_die("bind passive port");
}
```

`handle_pasv()` を、関数全体ごと以下の内容で置き換える。

```c
static void
handle_pasv(void)
{
	unsigned port;
	char *addr, *response;

	port = bind_for_passive_mode();
	addr = umu_ftp_pasv_addr();
	replace_char(addr, '.', ',');

	response = xasprintf(STR(FTP_PASVOK)" PASV ok (%s,%u,%u)\r\n",
			addr, (int)(port >> 8), (int)(port & 255));
	free(addr);
	cmdio_write_raw(response);
	free(response);
}
```

`handle_epsv()` は、関数全体はそのままでよい。`bind_for_passive_mode()` が固定ポートを返すようになるので、EPSV の応答も同じ範囲に収まる。

保存後に BusyBox をビルドする。

```bash
cd /home/tama/umu_project/work/umuos021/initramfs/busybox/work
make -j"$(nproc)" 2>&1 | tee /home/tama/umu_project/work/umuos021/logs/busybox_build.log

test -x /home/tama/umu_project/work/umuos021/initramfs/busybox/work/busybox
/home/tama/umu_project/work/umuos021/initramfs/busybox/work/busybox --list | grep -E '^(telnetd|login|tcpsvd|ftpd|ntpd|syslogd|logger|logread)$'
```

---

## 5. initramfs（initrd.img-6.18.1）

観測点：`initrd.img-6.18.1` が生成され、`/bin/switch_root` を含んだ最小 initramfs になる。
理解の狙い：GRUB → kernel → initrd の“つなぎ目”はファイル配置で決まるため、成果物の存在を物で確認する。

0.2.1 初版では、`initramfs/src/init.c` は 0.1.7 の実績版を土台として採用する。これは本設計の確定方針とする。

ただし `log_printf()` の `write(2, ...)` だけは、`warn_unused_result` の警告を消しつつ短い書き込みや `EINTR` を吸収するため、戻り値を確認する最小修正を入れてよい。ここは 0.2.1 側での許容差分とする。

```bash
rm -rf /home/tama/umu_project/work/umuos021/initramfs/rootfs
mkdir -p /home/tama/umu_project/work/umuos021/initramfs/rootfs/{bin,sbin,etc,proc,sys,dev,dev/pts,run,newroot,tmp}

cp -f /home/tama/umu_project/work/umuos021/initramfs/busybox/work/busybox \
	/home/tama/umu_project/work/umuos021/initramfs/rootfs/bin/busybox
chmod 755 /home/tama/umu_project/work/umuos021/initramfs/rootfs/bin/busybox

ln -sf busybox /home/tama/umu_project/work/umuos021/initramfs/rootfs/bin/sh
ln -sf busybox /home/tama/umu_project/work/umuos021/initramfs/rootfs/bin/mount
ln -sf busybox /home/tama/umu_project/work/umuos021/initramfs/rootfs/bin/umount
ln -sf busybox /home/tama/umu_project/work/umuos021/initramfs/rootfs/bin/switch_root

cp -f /home/tama/umu_project/UmuOS-0.1.7-base-stable/initramfs/src/init.c \
	/home/tama/umu_project/work/umuos021/initramfs/src/init.c

cc -Os -static -s \
	-o /home/tama/umu_project/work/umuos021/initramfs/rootfs/init \
	/home/tama/umu_project/work/umuos021/initramfs/src/init.c
chmod 755 /home/tama/umu_project/work/umuos021/initramfs/rootfs/init

cd /home/tama/umu_project/work/umuos021/initramfs
rm -f initrd.filelist0 initrd.cpio initrd.img-6.18.1
find rootfs -mindepth 1 -printf '%P\0' > initrd.filelist0
cd rootfs
cpio --null -ov --format=newc < ../initrd.filelist0 > ../initrd.cpio
cd ..
gzip -9 -c initrd.cpio > initrd.img-6.18.1

mkdir -p /home/tama/umu_project/work/umuos021/iso_root/boot
cp -f initrd.img-6.18.1 /home/tama/umu_project/work/umuos021/iso_root/boot/initrd.img-6.18.1

test -f /home/tama/umu_project/work/umuos021/iso_root/boot/initrd.img-6.18.1
```

---

## 6. ISO（UmuOS-0.2.1-dev-boot.iso）

観測点：ISOファイルが生成され、QEMUに `-cdrom` で渡せる。
理解の狙い：Rockyへ持ち込む成果物を「ISO+disk.img+start.sh」の3点に固定し、起動ホスト側の依存を最小化する。

```bash
cd /home/tama/umu_project/work/umuos021

cat > /home/tama/umu_project/work/umuos021/iso_root/boot/grub/grub.cfg <<'EOF'
set timeout=20
set default=0

menuentry "UmuOS 0.2.1-dev" {
	linux /boot/vmlinuz-6.18.1 console=ttyS0,115200n8 \
		root=UUID=d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15 rootfstype=ext4 rw \
		net.ifnames=0 biosdevname=0 \
		loglevel=3 panic=-1
	initrd /boot/initrd.img-6.18.1
}
EOF

grub-mkrescue -o /home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev-boot.iso /home/tama/umu_project/work/umuos021/iso_root \
	2>&1 | tee /home/tama/umu_project/work/umuos021/logs/grub_mkrescue.log

test -f /home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev-boot.iso
```

---

## 7. disk.img（永続 rootfs）

観測点：`/etc/profile` で `PATH` と `TZ` が固定され、`rcS` がテンプレ版になっている。
理解の狙い：`switch_root` 後の永続 rootfs が「毎回の起動挙動」を決めることを観測する（initramfs と責務を分離）。

```bash
cd /home/tama/umu_project/work/umuos021

rm -f /home/tama/umu_project/work/umuos021/disk/disk.img
truncate -s 4G /home/tama/umu_project/work/umuos021/disk/disk.img
mkfs.ext4 -F -U d2c0b3c3-0b5e-4d24-8c91-09b3a4fb0c15 /home/tama/umu_project/work/umuos021/disk/disk.img

sudo mkdir -p /mnt/umuos021
sudo mount -o loop /home/tama/umu_project/work/umuos021/disk/disk.img /mnt/umuos021

sudo mkdir -p /mnt/umuos021/{bin,sbin,etc,proc,sys,dev,dev/pts,run,var,var/run,var/log,home,home/tama,home/tama/inbox,root,tmp,logs,etc/init.d,etc/umu,umu_bin}

sudo cp -f /home/tama/umu_project/work/umuos021/initramfs/busybox/work/busybox /mnt/umuos021/bin/busybox
sudo chown root:root /mnt/umuos021/bin/busybox
sudo chmod 755 /mnt/umuos021/bin/busybox
sudo chroot /mnt/umuos021 /bin/busybox --install -s /bin
sudo chroot /mnt/umuos021 /bin/busybox --install -s /sbin
sudo ln -sf /bin/busybox /mnt/umuos021/sbin/init

sudo tee /mnt/umuos021/etc/inittab >/dev/null <<'EOF'
::sysinit:/etc/init.d/rcS

ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100

::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a
EOF

sudo tee /mnt/umuos021/etc/umu/network.conf >/dev/null <<'EOF'
IFNAME=eth0
MODE=static
IP=10.0.2.15/24
GW=10.0.2.2
DNS=8.8.8.8
EOF

sudo tee /mnt/umuos021/etc/resolv.conf >/dev/null <<'EOF'
nameserver 8.8.8.8
nameserver 8.8.4.4
EOF

sudo tee /mnt/umuos021/etc/TZ >/dev/null <<'EOF'
JST-9
EOF

sudo tee /mnt/umuos021/etc/securetty >/dev/null <<'EOF'
ttyS0
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

sudo tee /mnt/umuos021/etc/profile >/dev/null <<'EOF'
export PATH=/umu_bin:/sbin:/bin
export TZ=JST-9
EOF

sudo tee /mnt/umuos021/etc/syslog.conf >/dev/null <<'EOF'
auth.* /var/log/access.log
*.*;auth.none /var/log/messages
EOF

sudo tee /mnt/umuos021/etc/passwd >/dev/null <<'EOF'
root:x:0:0:root:/root:/bin/sh
tama:x:1000:1000:tama:/home/tama:/bin/sh
EOF

sudo tee /mnt/umuos021/etc/group >/dev/null <<'EOF'
root:x:0:
users:x:100:
tama:x:1000:
EOF

sudo mkdir -p /mnt/umuos021/root
sudo mkdir -p /mnt/umuos021/home/tama
sudo chown 1000:1000 /mnt/umuos021/home/tama
sudo chown 1000:1000 /mnt/umuos021/home/tama/inbox
sudo chmod 0755 /mnt/umuos021/home/tama
sudo chmod 0750 /mnt/umuos021/home/tama/inbox

sudo chown root:root /mnt/umuos021/umu_bin
sudo chmod 0755 /mnt/umuos021/umu_bin

sudo tee /mnt/umuos021/umu_bin/ntp_sync >/dev/null <<'EOF'
#!/bin/sh
ping -c 1 8.8.8.8 >/dev/null 2>&1 || exit 1
ntpd -n -q -p time.google.com >/dev/null 2>&1 && exit 0
ntpd -n -p time.google.com
EOF

sudo chown root:root /mnt/umuos021/umu_bin/ntp_sync
sudo chmod 0755 /mnt/umuos021/umu_bin/ntp_sync

sudo tee /mnt/umuos021/umu_bin/ftpd_start >/dev/null <<'EOF'
#!/bin/sh
if [ -f /run/ftpd.pid ] && kill -0 "$(cat /run/ftpd.pid)" 2>/dev/null; then
	exit 0
fi

export UMU_FTP_PASV_ADDR=<グローバルIP>
export UMU_FTP_PASV_MIN=21000
export UMU_FTP_PASV_MAX=21031

busybox tcpsvd -vE 0.0.0.0 21 busybox ftpd -w -S / >> /var/log/messages 2>&1 &
echo $! > /run/ftpd.pid
EOF

sudo chown root:root /mnt/umuos021/umu_bin/ftpd_start
sudo chmod 0755 /mnt/umuos021/umu_bin/ftpd_start

sudo tee /mnt/umuos021/umu_bin/ftpd_stop >/dev/null <<'EOF'
#!/bin/sh
if [ -f /run/ftpd.pid ]; then
	kill "$(cat /run/ftpd.pid)" 2>/dev/null || true
	rm -f /run/ftpd.pid
fi
EOF

sudo chown root:root /mnt/umuos021/umu_bin/ftpd_stop
sudo chmod 0755 /mnt/umuos021/umu_bin/ftpd_stop

# systemd-initrd 等のOS判定を回避するため最小の os-release を入れる
sudo tee /mnt/umuos021/etc/os-release >/dev/null <<'EOF'
NAME="UmuOS"
ID=umuos
VERSION="0.2.1-dev"
PRETTY_NAME="UmuOS 0.2.1-dev"
EOF

sudo touch /mnt/umuos021/var/log/messages
sudo touch /mnt/umuos021/var/log/access.log
sudo chown root:root /mnt/umuos021/var/log/messages /mnt/umuos021/var/log/access.log
sudo chmod 0644 /mnt/umuos021/var/log/messages
sudo chmod 0600 /mnt/umuos021/var/log/access.log

# rcS はテンプレ1本からインストール（この文書の完成形）
sudo install -m 0755 /home/tama/umu_project/work/umuos021/tools/rcS_umuos021.sh /mnt/umuos021/etc/init.d/rcS
```

### 7.1 パスワード（手で貼る）

観測点：このブロックだけは手入力が必要（ハッシュ）。
理解の狙い：認証は /etc/shadow の内容に直結する（=成果物へ埋め込むデータ）ため、手作業箇所を明示して再現の揺れをここに隔離する。

```bash
openssl passwd -6
openssl passwd -6
```

```bash
sudo tee /mnt/umuos021/etc/shadow >/dev/null <<'EOF'
root:<rootの$6$...を貼る>:20000:0:99999:7:::
tama:<tamaの$6$...を貼る>:20000:0:99999:7:::
EOF

sudo chown root:root /mnt/umuos021/etc/shadow
sudo chmod 600 /mnt/umuos021/etc/shadow

sync
sudo umount /mnt/umuos021

sudo blkid /home/tama/umu_project/work/umuos021/disk/disk.img
sudo e2fsck -f /home/tama/umu_project/work/umuos021/disk/disk.img || true
sha256sum /home/tama/umu_project/work/umuos021/disk/disk.img > /home/tama/umu_project/work/umuos021/disk/disk.img.sha256
sha256sum /home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev-boot.iso > /home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev-boot.iso.sha256
sha256sum /home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev_start.sh > /home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev_start.sh.sha256
```

---

## 8. 成果物（Rockyへ移す3ファイル）

- `/home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev-boot.iso`
- `/home/tama/umu_project/work/umuos021/disk/disk.img`
- `/home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev_start.sh`

---

## 9. RockyLinux9.7 へ転送（例：scp）

観測点：Rocky側でsha256検証が通る。
理解の狙い：転送破損や取り違えを「起動前」に確実に潰す。

```bash
set -euo pipefail

# Rocky側の作業ディレクトリ
ssh root@<グローバルIP> "sudo mkdir -p '/home/tama/umuos021' && sudo chown -R tama:tama '/home/tama/umuos021' && sudo chmod 0755 '/home/tama/umuos021'"

scp \
	/home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev-boot.iso \
	/home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev-boot.iso.sha256 \
	/home/tama/umu_project/work/umuos021/disk/disk.img \
	/home/tama/umu_project/work/umuos021/disk/disk.img.sha256 \
	/home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev_start.sh \
	/home/tama/umu_project/work/umuos021/UmuOS-0.2.1-dev_start.sh.sha256 \
	root@<グローバルIP>:/home/tama/umuos021/
```

---

## 10. RockyLinux9.7（起動環境）：環境構築と起動

### 10.1 RockyLinux9.7 での QEMU 実行環境を作る

観測点：QEMUバイナリが存在し、`/dev/kvm` の有無が起動前に分かる。
理解の狙い：この文書は RockyLinux9.7 上で UmuOS を動かすためのものなので、起動失敗を「ゲストの問題」ではなく「Rocky 側の QEMU 実行条件不足」と切り分けられるようにする。

```bash
sudo dnf -y install \
	qemu-kvm qemu-kvm-core qemu-img \
	util-linux

command -v qemu-system-x86_64 || true
command -v qemu-kvm || true
ls -l /usr/libexec/qemu-kvm || true

ls -l /dev/kvm || true

# `script` は起動ログ保存に使う
command -v script
```

補足：

- `/dev/kvm` がある場合は、起動スクリプトが `-enable-kvm -cpu host` を使う。
- `/dev/kvm` が無い場合でも、起動スクリプトは自動で TCG（`-machine q35 -cpu qemu64`）へフォールバックする。
- ただし VPS 環境で KVM が使えない場合、起動はかなり遅くなることがある。

### 10.2 作業ディレクトリ（ここで起動する）

```bash
set -euo pipefail

cd /home/tama/umuos021
ls -l

sha256sum -c UmuOS-0.2.1-dev-boot.iso.sha256
sha256sum -c disk.img.sha256
sha256sum -c UmuOS-0.2.1-dev_start.sh.sha256
```
それぞれのパーミッション
UmuOS-0.2.1-dev-boot.iso　644
disk.img　644
UmuOS-0.2.1-dev_start.sh　755

### 10.3 Rocky側ファイアウォール（firewalld 使用前提）

```bash
sudo firewall-cmd --add-port=23/tcp --permanent
sudo firewall-cmd --add-port=21/tcp --permanent
sudo firewall-cmd --add-port=21000-21031/tcp --permanent
sudo firewall-cmd --reload
```

### 10.4 起動

```bash
cd /home/tama/umuos021

sudo bash ./UmuOS-0.2.1-dev_start.sh
```

---

## 11. 成功確認（最小）

### 11.1 QEMUコンソールでの確認

- `login:` が出る
- ログイン後に `cat /logs/boot.log` で起動ログが追記されている
- ログイン失敗を1回試すと `cat /var/log/access.log` に `invalid password` が残る
- root でログインすると `cat /var/log/access.log` に `root login` が残る
- `ps w | grep -E 'telnetd|tcpsvd|ftpd'` で telnet/FTP の両方がいる
- `ls -ld /home/tama/inbox` が `tama:tama` になっている

```bash
cat /var/log/access.log
cat /var/log/messages
ps w | grep -E 'telnetd|tcpsvd|ftpd' | grep -v grep || true
ls -ld /home/tama/inbox
```

### 11.2 外部から（telnet）

```bash
telnet <グローバルIP> 23
```

### 11.3 外部から（FTP, passive mode 前提）

- FTP のログインユーザーは `tama` を使う。
- `/` は読めるが、アップロード先は `/home/tama/inbox` に固定する。

```bash
curl --ftp-pasv --user tama:'<tamaのパスワード>' ftp://<グローバルIP>/

curl --ftp-pasv \
	--user tama:'<tamaのパスワード>' \
	-T ./local_test.bin \
	ftp://<グローバルIP>/home/tama/inbox/

curl --ftp-pasv \
	--user tama:'<tamaのパスワード>' \
	-o ./downloaded_os-release \
	ftp://<グローバルIP>/etc/os-release
```

観測点：`put/get` が両方通る。
理解の狙い：この設計では FTP は passive mode と固定 passive range で成立していることを、外部クライアント側から確認する。

## 12. トラブルシュート（最小セット）

### 12.1 hostfwdでポートがbindできない

- 23は特権ポートなので、QEMU起動がrootでないと失敗する。
- 本書の起動スクリプトは自動で `sudo` 再実行する。

### 12.2 `login:` が出ない

```sh
cat /logs/boot.log || true
ps w
```

- `rcS` が走っていない場合：`/etc/inittab` と `/etc/init.d/rcS` を確認する。

### 12.3 telnetで `login:` が出ない

典型：`telnetd` が起動していない／ネットワークが上がっていない。

```sh
ip a
ip r
ps w | grep telnetd | grep -v grep || true
cat /etc/umu/network.conf
```

### 12.4 FTPでログインはできるが `put/get` が失敗する

典型：`21/tcp` だけ通っていて passive range (`21000-21031/tcp`) が Rocky 側で閉じている／QEMU `hostfwd` に passive range が入っていない。

```sh
cat /var/log/messages | tail -n 50
ps w | grep -E 'tcpsvd|ftpd' | grep -v grep || true
cat /run/ftpd.pid 2>/dev/null || true
```

- Rocky 側で `21/tcp` と `21000-21031/tcp` の両方が `firewall-cmd` に入っているか確認する。
- 起動スクリプトの `QEMU_NETDEV` に `hostfwd=tcp::21-:21` と `hostfwd=tcp::21000-:21000` から `hostfwd=tcp::21031-:21031` までが入っているか確認する。
- クライアント側は passive mode で試す。`curl --ftp-pasv` を基準コマンドにする。

### 12.5 `ip a` が `lo` しか出ない

原因（典型）：QEMUのNICが無い（起動引数ミス）か、カーネルがvirtio-netを含んでいない。

- 起動スクリプトの `-device virtio-net-pci` が入っているか確認
- カーネルconfigで `VIRTIO_NET` が有効か確認（`iso_root/boot/config-6.18.1`）

### 12.6 QEMU起動直後に落ちる（CPU機能不足）

- `-cpu host` が無いと、環境によってはCPU機能不足で落ちることがある。
- 本書の起動スクリプトは `-cpu host` を含む。

### 12.7 `/var/log/access.log` が増えない

典型：`syslogd` が起動していない／`/etc/syslog.conf` が入っていない／BusyBox の `telnetd.c` 自前改造が未反映。

```sh
ps w | grep -E 'syslogd|klogd|telnetd' | grep -v grep || true
cat /etc/syslog.conf
logread || true
cat /var/log/access.log || true
cat /var/log/messages || true
```

- `invalid password` は `login` が出す `auth` ログで、`/var/log/access.log` に分離される想定。
- `telnet connect from ...` が一切出ない場合は、BusyBox の `networking/telnetd.c` で `make_new_session()` を書き換えた内容がビルド後の `busybox` に入っているかを疑う。

### 12.8 FTP では見えるが `/home/tama/inbox` に書けない

典型：`/home/tama/inbox` の所有者やパーミッションが崩れている。

```sh
ls -ld /home /home/tama /home/tama/inbox
id tama
```

- `/home/tama/inbox` は `tama:tama`、`0750` を基準にする。
- FTP の書き込み先は `/home/tama/inbox` だけを使い、`/umu_bin` へ直接 put しない。

