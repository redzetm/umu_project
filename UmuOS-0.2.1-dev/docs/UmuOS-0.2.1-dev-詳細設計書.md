# UmuOS-0.2.1-dev 詳細設計書（公開版）

この文書は「実装担当が、手順をコピペして進められる粒度」を狙う。

公開版のため、実機固有情報（例：公開IP、ユーザー名、手元PCの絶対パス、rootfs UUID など）は **例示値** に差し替えている。手順を実行する前に、必ず自分の環境の値へ置き換えること。

---

## 0. 事前準備（絶対に最初）

この文書は「UbuntuでUmuOSのrootfs（disk.img）を作る」→「RockyLinux 9.7へ持ち込む」→「Rocky上でQEMU/KVMゲストとして起動する（方式B）」までを **最初から完走**できる形にする。

### 0.1 ゴール（この手順で最終的に成立させること）

- Ubuntu（開発/ビルド環境）で `disk.img`（ext4直置きのrootfs）を生成する
- RockyLinux 9.7（起動環境）に `disk.img` を転送し、**方式B（QEMU/KVM）で起動する**（Rockyを残してSSH復旧しやすい）
	- 0.2.1 では方式Bを採用する
	- 0.2.2 以降では別の起動方式も検討するが、この文書は方式Bに固定する

### 0.2 固定値（この文書で前提にする値）

ここは「あなたの環境の実値」を直書きする前提。
公開版では、以下を **例示値** として載せる（要置換）。

- Ubuntu 作業ディレクトリ（例）：`/home/USERNAME/umu_project/work/umuos021_ubuntu`
- BusyBox ソース（例）：`/home/USERNAME/umu_project/external/busybox-1.36.1`
- 生成する disk.img（例）：`/home/USERNAME/umu_project/work/umuos021_ubuntu/disk.img`
- UmuOS rootfs UUID（例）：`11111111-2222-3333-4444-555555555555`

この文書では「Rockyの公開IP（ホスト）を残し、QEMUゲストへポート転送して入る」を前提にする。

- RockyLinux 9.7（ホスト）の公開IP（例）：`203.0.113.10/24`  ※要置換（RFC 5737 の例示アドレス）
- UmuOS rootfs の `network.conf`（ゲスト内部の static。QEMU user-net=SLIRP想定）：
	- `IFNAME=eth0`
	- `MODE=static`（`static|dhcp`）
	- `IP=10.0.2.15/24`
	- `GW=10.0.2.2`
	- `DNS=8.8.8.8`
	- `/etc/resolv.conf` は `8.8.8.8` と `8.8.4.4`
- Rocky 作業ディレクトリ（例）：`/root/umuos021`
- Rocky kernel version（initramfs生成で使う。例）：`5.14.0-xxx.el9_7.x86_64` ※要置換
- QEMU 実体：`/usr/libexec/qemu-kvm`

### 0.3 復旧経路チェック（詰まないため）

- 管理コンソール/レスキュー/ISO起動の少なくとも1つが使えること
- Rockyが通常起動し、SSHで入れること（この手順はRockyを残す前提）

### 0.4 なぜ `os-release` が必要か（ハマりどころの先回り）

Rocky 9 の initramfs（systemd-initrd）は、`switch_root` 時に `/sysroot` を「OSツリー」と判定し、rootfs 側に

- `/etc/os-release` または
- `/usr/lib/os-release`

が無いと `... does not seem to be an OS` で拒否することがある。

この文書では **Ubuntuでdisk.imgを作る段階で** 最小の `/etc/os-release` を必ず入れて回避する。

---

## 1. Ubuntu（開発/ビルド）：disk.img（UmuOS rootfs）を作る

ここで作る `disk.img` は「ext4が直置き」のイメージ（パーティション無し）を前提とする。

### 1.1 Ubuntu パッケージ（最小）

```bash
sudo apt update
sudo apt install -y \
	build-essential gcc make \
	rsync \
	e2fsprogs util-linux \
	cpio gzip xz-utils \
	openssl
```

### 1.2 作業ディレクトリ

```bash
set -euo pipefail

mkdir -p /home/USERNAME/umu_project/work/umuos021_ubuntu
cd /home/USERNAME/umu_project/work/umuos021_ubuntu

test -f /home/USERNAME/umu_project/external/busybox-1.36.1/Makefile
```

### 1.3 BusyBox（static）をビルド（対話なし）

```bash
set -euo pipefail

rm -rf /home/USERNAME/umu_project/work/umuos021_ubuntu/busybox.work
mkdir -p /home/USERNAME/umu_project/work/umuos021_ubuntu/busybox.work
rsync -a --delete /home/USERNAME/umu_project/external/busybox-1.36.1/ /home/USERNAME/umu_project/work/umuos021_ubuntu/busybox.work/

cd /home/USERNAME/umu_project/work/umuos021_ubuntu/busybox.work
make distclean
make defconfig

cat >> .config <<'EOF'
CONFIG_STATIC=y

CONFIG_INIT=y
CONFIG_FEATURE_USE_INITTAB=y
CONFIG_GETTY=y
CONFIG_SWITCH_ROOT=y

CONFIG_LOGIN=y
CONFIG_FEATURE_SHADOWPASSWDS=y

CONFIG_IP=y

CONFIG_TELNETD=y
CONFIG_FEATURE_TELNETD_STANDALONE=y

CONFIG_NTPD=y
CONFIG_TCPSVD=y
CONFIG_FTPD=y

# ビルドが落ちやすい機能は明示的に無効（環境差を減らす）
# CONFIG_TC is not set
# CONFIG_FEATURE_TC_INGRESS is not set
EOF

yes "" | make oldconfig

make -j"$(nproc)"

test -x busybox
file busybox
./busybox --list | grep -E '^(init|getty|login|telnetd|tcpsvd|ftpd|ntpd|ip)$'
```

### 1.4 disk.img を新規作成（ext4直置き + UUID固定）

```bash
set -euo pipefail

cd /home/USERNAME/umu_project/work/umuos021_ubuntu

rm -f /home/USERNAME/umu_project/work/umuos021_ubuntu/disk.img
truncate -s 4G /home/USERNAME/umu_project/work/umuos021_ubuntu/disk.img

# UUID は例示値。あなたの環境の値へ置換すること。
mkfs.ext4 -F -U 11111111-2222-3333-4444-555555555555 /home/USERNAME/umu_project/work/umuos021_ubuntu/disk.img

sudo mkdir -p /mnt/umu_img
sudo mount -o loop /home/USERNAME/umu_project/work/umuos021_ubuntu/disk.img /mnt/umu_img

sudo mkdir -p /mnt/umu_img/{bin,sbin,etc,etc/init.d,etc/umu,proc,sys,dev,dev/pts,run,var,var/run,home,root,tmp,logs,umu_bin}

# BusyBox を配置して /bin, /sbin に applet を展開
sudo cp -f /home/USERNAME/umu_project/work/umuos021_ubuntu/busybox.work/busybox /mnt/umu_img/bin/busybox
sudo chown root:root /mnt/umu_img/bin/busybox
sudo chmod 0755 /mnt/umu_img/bin/busybox
sudo chroot /mnt/umu_img /bin/busybox --install -s /bin
sudo chroot /mnt/umu_img /bin/busybox --install -s /sbin

# init の実体
sudo ln -sf /bin/busybox /mnt/umu_img/sbin/init

# inittab（シリアルを必ず出す）
sudo tee /mnt/umu_img/etc/inittab >/dev/null <<'EOF'
::sysinit:/etc/init.d/rcS

ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100

::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a
EOF

# systemd-initrd がOS判定できるようにする（最小）
sudo tee /mnt/umu_img/etc/os-release >/dev/null <<'EOF'
NAME="UmuOS"
ID=umuos
VERSION="0.2.1-dev"
PRETTY_NAME="UmuOS 0.2.1-dev"
EOF

# ネットワーク設定I/F（rcS側が読む想定）
sudo tee /mnt/umu_img/etc/umu/network.conf >/dev/null <<EOF
IFNAME=eth0
MODE=static
IP=10.0.2.15/24
GW=10.0.2.2
DNS=8.8.8.8
EOF

sudo tee /mnt/umu_img/etc/resolv.conf >/dev/null <<EOF
nameserver 8.8.8.8
nameserver 8.8.4.4
EOF

sudo tee /mnt/umu_img/etc/profile >/dev/null <<'EOF'
export PATH=/umu_bin:/sbin:/bin
export TZ=JST-9
EOF

sudo tee /mnt/umu_img/etc/securetty >/dev/null <<'EOF'
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

# login のための最小ユーザーDB
# 注意：ここでは「初回起動の成功」を優先し、root は空パスワードにする。
# 公開環境では危険なので、起動後ただちに UmuOS 側で `passwd` を実行して設定する。
sudo tee /mnt/umu_img/etc/passwd >/dev/null <<'EOF'
root::0:0:root:/root:/bin/sh
EOF

sudo tee /mnt/umu_img/etc/group >/dev/null <<'EOF'
root:x:0:
EOF

sudo tee /mnt/umu_img/etc/shadow >/dev/null <<'EOF'
root::20000:0:99999:7:::
EOF

sudo chmod 0600 /mnt/umu_img/etc/shadow

sudo mkdir -p /mnt/umu_img/root
sudo chmod 0700 /mnt/umu_img/root

# rcS（最小：mount + ログ + telnet の起動フック）
sudo tee /mnt/umu_img/etc/init.d/rcS >/dev/null <<'EOF'
#!/bin/sh

export PATH=/umu_bin:/sbin:/bin
export TZ=JST-9

mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mount -t devpts devpts /dev/pts 2>/dev/null || true

mkdir -p /logs /run /var/run /umu_bin 2>/dev/null || true
: > /var/run/utmp 2>/dev/null || true

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

# network.conf をI/Fにして static だけ上げる（dhcp は拡張点）
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
EOF
sudo chmod 0755 /mnt/umu_img/etc/init.d/rcS

sync
sudo umount /mnt/umu_img

# 最低限チェック
sudo blkid /home/USERNAME/umu_project/work/umuos021_ubuntu/disk.img
sudo e2fsck -f /home/USERNAME/umu_project/work/umuos021_ubuntu/disk.img || true
sha256sum /home/USERNAME/umu_project/work/umuos021_ubuntu/disk.img > /home/USERNAME/umu_project/work/umuos021_ubuntu/disk.img.sha256
```

### 1.5 Rockyへ転送（例：scp）

```bash
set -euo pipefail

# Rocky側の作業ディレクトリを先に用意（無いと scp が失敗する）
ssh root@203.0.113.10 "sudo mkdir -p '/root/umuos021' && sudo chmod 0700 '/root/umuos021'"

scp /home/USERNAME/umu_project/work/umuos021_ubuntu/disk.img /home/USERNAME/umu_project/work/umuos021_ubuntu/disk.img.sha256 root@203.0.113.10:/root/umuos021/
```

---

## 2. RockyLinux 9.7（起動環境）：環境構築と起動

### 2.1 Rocky パッケージ（必須）

```bash
sudo dnf -y install \
	e2fsprogs util-linux \
	dracut \
	qemu-kvm qemu-kvm-core qemu-img
```

### 2.2 作業ディレクトリ（root専用）

```bash
set -euo pipefail

sudo mkdir -p /root/umuos021
sudo chmod 0700 /root/umuos021
sudo ls -l /root/umuos021

test -f /root/umuos021/disk.img
sha256sum -c /root/umuos021/disk.img.sha256
```

### 2.3 起動方式（0.2.1は方式Bのみ）

- `disk.img` をそのまま virtio-disk としてQEMUへ渡して起動する
- RockyのSSHを残したまま反復できる（失敗時の復旧が速い）

---

## 3. 方式B（採用）：Rockyを残してQEMU/KVMで起動

0.2.1 は本章の方式で起動する（Rockyを残す）。

### 3.1 /dev/kvm と QEMU 実体確認

```bash
ls -l /dev/kvm || echo "no /dev/kvm"

command -v qemu-system-x86_64 || true
ls -l /usr/libexec/qemu-kvm || true
/usr/libexec/qemu-kvm --version || true
```

### 3.2 QEMU用 initramfs（ext4 + virtio）

```bash
set -euo pipefail

# 例示：KVER はあなたの環境の `uname -r` に置換すること
sudo dracut -f -v /root/initramfs-5.14.0-xxx.el9_7.x86_64-umuos-hostonly-ext4.img 5.14.0-xxx.el9_7.x86_64 \
	--hostonly \
	--add-drivers "ext4 virtio virtio_ring virtio_pci virtio_blk virtio_net"

sudo ls -lh /root/initramfs-5.14.0-xxx.el9_7.x86_64-umuos-hostonly-ext4.img
```

### 3.3 QEMU起動（成功しやすい形）

```bash
set -euo pipefail

sudo /usr/libexec/qemu-kvm \
	-accel kvm \
	-machine q35 \
	-cpu host \
	-smp 2 \
	-m 1024M \
	-nographic \
	-serial stdio \
	-monitor none \
	-kernel /boot/vmlinuz-5.14.0-xxx.el9_7.x86_64 \
	-initrd /root/initramfs-5.14.0-xxx.el9_7.x86_64-umuos-hostonly-ext4.img \
	-drive file=/root/umuos021/disk.img,if=virtio,format=raw,cache=none,aio=native \
	-netdev user,id=n1,hostfwd=tcp::23-:23 \
	-device virtio-net-pci,netdev=n1 \
	-append "console=ttyS0,115200n8 root=UUID=11111111-2222-3333-4444-555555555555 rootfstype=ext4 rw loglevel=3 panic=-1"
```

重要：`-cpu host`

- これが無いと `Fatal glibc error: CPU does not support x86-64-v2` が出て init が即死し、panic になることがある。

---

## 4. 成功確認（最小）

### 4.1 管理コンソール（またはQEMUシリアル）での確認

- initramfs が rootfs を `/sysroot` にマウントできたログが出る
- `switch_root` が実行され、UmuOS の `login:` まで到達する

初回ログインできたら、すぐにパスワードを設定して `sync` する。

```sh
passwd
sync
```

### 4.2 外部から（telnet）

```bash
telnet 203.0.113.10 23
```

`login:` が出れば到達性とユーザーランド起動が成立。

---

## 5. トラブルシュート（最小セット）

### 5.1 initramfsでrootが見つからない

- `root=UUID=11111111-2222-3333-4444-555555555555` が正しいか（要置換）
- initramfs（dracut emergency shell）で `blkid` を実行し、ext4 のUUIDが該当のデバイスが見えているか
	- QEMU/virtio-disk なら、典型的には `/dev/vda` として見える

### 5.2 initramfsで `/sysroot` の mount に失敗し `unknown filesystem type 'ext4'` が出る

症状（例）：

- `mount: /sysroot: unknown filesystem type 'ext4'.`

原因：

- 使っている initramfs（dracut）が **hostonly** で作られており、ホストOSのrootfsがxfs等の場合に、
	ext4 のカーネルモジュールが initramfs に含まれないことがある。
	この場合、ext4 rootfs を「マウントしてから」モジュールを読み込むことができないため詰む。

対処（ホストOS上で、ext4入りのinitramfsを作り直す）：

1) 対象カーネルバージョンを確認

```bash
uname -r
```

2) ext4 + virtio を入れた initramfs を生成（no-hostonly 推奨）

```bash
sudo dracut -f /boot/initramfs-5.14.0-xxx.el9_7.x86_64-umuos-ext4.img 5.14.0-xxx.el9_7.x86_64 \
	--no-hostonly \
	--add-drivers "ext4 virtio virtio_ring virtio_pci virtio_blk"

ls -lh /boot/initramfs-5.14.0-xxx.el9_7.x86_64-umuos-ext4.img
```

3) QEMUの `-initrd` を上で作った `...-umuos-ext4.img` に差し替えて再試行する。

### 5.3 telnetで `login:` が出ない

典型：ネットワークが上がっていない。

- `net.ifnames=0 biosdevname=0` が効いておらずIF名がズレている
- `network.conf` のIP/GW/DNSがズレている
- 上流ACL/FWで23/TCPが遮断されている

管理コンソールで入れた場合：

```sh
ip a
ip r
ps w | grep telnetd | grep -v grep || true
cat /etc/umu/network.conf
```

### 5.3.1 `ip a` が `lo` しか出ない（`eth0` 等が存在しない）

症状（例）：

- `ip a` で `lo` しか表示されない
- `cat /etc/umu/network.conf` には `IFNAME=eth0` などが書かれているが、そもそもそのIFが存在しない

原因（典型）：

- UmuOS の rootfs には `/lib/modules/$(uname -r)` が無く、カーネルモジュール（NICドライバ等）を rootfs から追加ロードできない
- initramfs（dracut）側でも NIC ドライバが読み込まれていない
	- rootfs のマウントに必要な最小ドライバ（virtio_blk/ext4等）だけが入っていると、ネットワークは上がらないことがある

確認（UmuOS側）：

```sh
ls -l /sys/class/net
```

対処（ホストRocky側で initramfs を作り直し、NICドライバを追加する）：

QEMUなら virtio-net を使うのが最短。

```bash
sudo dracut -f /boot/initramfs-5.14.0-xxx.el9_7.x86_64-umuos-ext4.img 5.14.0-xxx.el9_7.x86_64 \
	--no-hostonly \
	--add-drivers "ext4 virtio virtio_ring virtio_pci virtio_blk virtio_net"
```

補足（virtioではなく e1000 を使う場合）：

```bash
sudo dracut -f /boot/initramfs-5.14.0-xxx.el9_7.x86_64-umuos-ext4.img 5.14.0-xxx.el9_7.x86_64 \
	--no-hostonly \
	--add-drivers "ext4 virtio virtio_ring virtio_pci virtio_blk e1000"
```

その後、QEMU の `-initrd` をこの `/boot/initramfs-...-umuos-ext4.img` に差し替えて再起動し、`ip a` に `eth0` 等が出ることを確認する。

### 5.4 initramfs が `/sysroot` をマウントできるのに `Failed to switch root: ... does not seem to be an OS` で止まる

症状（例）：

- initramfs（dracut emergency shell）で `/dev/vda on /sysroot type ext4` など **rootfs のマウントは成功**している
- しかし `rdsosreport.txt` に以下のようなログが出て switch_root が拒否される
	- `Failed to switch root: Specified switch root path '/sysroot' does not seem to be an OS`

原因：

- initramfs の `/init` が systemd（systemd-initrd）になっている場合、`/sysroot` が「OSツリー」かどうか判定する。
- 判定は主に `os-release` の有無で行われ、rootfs 側に
	- `/sysroot/etc/os-release` または
	- `/sysroot/usr/lib/os-release`
	が無いと「OSではない」とみなされ、switch_root が拒否される。

対処A（推奨：rootfs を最小限だけ OS と認識させる）：

rootfs に最小の `os-release` を作る。

```sh
mkdir -p /sysroot/etc
cat > /sysroot/etc/os-release <<'EOF'
NAME="UmuOS"
ID=umuos
VERSION="0.2.1-dev"
PRETTY_NAME="UmuOS 0.2.1-dev"
EOF
```

その後、initramfs 側で switch_root を再実行する（systemd-initrd の場合）。

```sh
systemctl switch-root /sysroot /sbin/init
```

対処B（systemd-initrd を使わず dracut の従来経路に寄せる）：

- 起動パラメータに `rd.systemd=0` を追加して再起動する。
	- systemd-initrd が OS 判定をして止まる系のトラブルを回避しやすい。
	- その代わり、ログの出方や手順が変わるため切り分け中のみ推奨。

---

## 6. 運用チェックリスト（公開版）

- 手順中の例示値（IP/UUID/パス/カーネルバージョン）を自分の環境の値に置換した
- 初回起動後に `passwd` 済み（放置していない）
- `disk.img.sha256` で転送破損を検知できている

---

## 付録A. QEMUでのUmuOS起動（補助テンプレ）

目的：本編（方式B）の検証や切り分けで使うQEMUコマンドのテンプレ集。

この付録は、以下が揃っている場合に有効。

- UmuOS用の `VMLINUX`（例：bzImage/vmlinuz）
- UmuOS用の `initramfs`（/init を含む）

### A.1 直起動テンプレ（kernel+initramfs）

```bash
sudo /usr/libexec/qemu-kvm \
	-accel kvm \
	-machine q35 \
	-cpu host \
	-m 1024M \
	-nographic \
	-serial mon:stdio \
	-kernel /boot/vmlinuz-5.14.0-xxx.el9_7.x86_64 \
	-initrd /boot/initramfs-5.14.0-xxx.el9_7.x86_64-umuos-ext4.img \
	-append "console=ttyS0,115200n8 loglevel=7 panic=-1"
```

### A.2 よくある症状：No bootable device / iPXE に落ちる

以下のような表示が出る場合、QEMUに「起動媒体」を渡していないことが原因のことが多い。

- `Booting from Hard Disk... Boot failed`
- `iPXE ... Nothing to boot`
- `No bootable device.`

対策：

- 本付録のように `-kernel` と `-initrd` と `-append` を指定して起動する

補足：

- `-nodefaults` を付けると、ストレージ等が最小構成になり「何もブートできない」状態になりやすい。
	切り分け段階では付けない方が安全。

### A.3 よくある症状：`Fatal glibc error: CPU does not support x86-64-v2`

症状（例）：

- 起動直後に `Fatal glibc error: CPU does not support x86-64-v2` が出て init が即死する

原因：

- QEMUがゲストに見せるCPU機能が古い（もしくはデフォルトCPUモデル）だと、Rocky 9 系の glibc が要求する x86-64-v2 を満たせないことがある。

対処：

- QEMU起動に `-cpu host` を付ける（本書のテンプレに含めている）

### A.4 目的：自宅から telnet/FTP で直アクセス（Rockyを残して安全に公開）

この節は「VPS上のRockyは生かしたまま、UmuOSをQEMUゲストとして動かし、外部から入る」ための最短ルート。

狙い：

- 外部 → VPS(Rocky) の TCPポート → QEMUゲスト(UmuOS) の telnet/FTP へ転送
- RockyへのSSH(22/tcp)は残し、事故っても復旧しやすい

前提：

- QEMUは `sudo` で起動する（23/21などの特権ポートをバインドするため）
- guest 側は QEMUの `virtio-net` を使う（推奨）

#### A.4.1 initramfsに NIC ドライバを入れる（loしか出ない対策）

UmuOSのrootfsには `kernel modules` が無い前提になりがちなので、NICドライバは initramfs 側で読み込ませる。

```bash
sudo dracut -f /boot/initramfs-5.14.0-xxx.el9_7.x86_64-umuos-ext4-net.img 5.14.0-xxx.el9_7.x86_64 \
	--no-hostonly \
	--add-drivers "ext4 virtio virtio_ring virtio_pci virtio_blk virtio_net"

ls -lh /boot/initramfs-5.14.0-xxx.el9_7.x86_64-umuos-ext4-net.img
```

#### A.4.2 QEMUを「外部から入れる」形で起動する（hostfwd）

ポイント：

- `-netdev user` を使うと、ブリッジ等を作らずにホスト(Rocky)で受けたTCPをゲストへ転送できる
- telnet/FTP は平文なので、まずは動作確認としてポートを開ける（本番はアクセス制限推奨）

```bash
sudo /usr/libexec/qemu-kvm \
	-accel kvm \
	-machine q35 \
	-cpu host \
	-m 1024M \
	-nographic \
	-serial mon:stdio \
	-kernel /boot/vmlinuz-5.14.0-xxx.el9_7.x86_64 \
	-initrd /boot/initramfs-5.14.0-xxx.el9_7.x86_64-umuos-ext4-net.img \
	-drive file=/root/umuos021/disk.img,if=virtio,format=raw \
	-netdev user,id=n1,hostfwd=tcp::23-:23,hostfwd=tcp::21-:21 \
	-device virtio-net-pci,netdev=n1 \
	-append "console=ttyS0,115200n8 root=UUID=11111111-2222-3333-4444-555555555555 rootfstype=ext4 rw loglevel=7 panic=-1"
```

補足：

- まずは telnet(23) と FTP制御(21) の「入口」だけを開ける。
- FTPのデータ接続（20番やPASVの複数ポート）は追加で設計が必要になることが多い（次節参照）。

#### A.4.3 Rocky側のファイアウォールを開ける（必要な場合）

```bash
sudo firewall-cmd --state || true

sudo firewall-cmd --add-port=23/tcp --permanent
sudo firewall-cmd --add-port=21/tcp --permanent
sudo firewall-cmd --reload
```

#### A.4.4 UmuOS側でサービスを起動する（最小）

UmuOSがbusybox前提の場合、機能有無はビルド構成に依存するため、まずコマンド存在確認をする。

```sh
busybox | grep -E 'telnetd|ftpd|inetd' || true
```

最低限のtelnetサーバ（例）：

```sh
# 例：busybox telnetd がある場合
telnetd -F
```

FTPについて：

- FTPは「制御(21)」以外にデータ接続が必要で、NAT/転送と相性が悪い。
- まずは「telnetで入れる」ことを先に確定し、その後
	- PASVの固定ポート範囲を決める
	- QEMUの `hostfwd` をその範囲ぶん追加する
	の順で詰めるのが安全。
