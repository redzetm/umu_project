---
title: UmuOS-0.1.4-base-stable 実装ノート
date: 2026-01-25
related_design: "./UmuOS-0.1.4-base-stable-詳細設計書.md"
status: ongoing
---

# 0. このノートの目的

- 試行錯誤を「再現可能な手順」と「切り分けの根拠（ログ/観測点）」として残す。
- 失敗を成果にする（どこが制約で、どこを変えると成立するかを明確化）。
- 1回の試行で変える点を最小化し、原因同定の速度を上げる。

# 1. ルール（固定）

## 1.1 1試行 = 1エントリ
各試行は「ID（連番）」「変更点（1〜2個）」「観測」「結論」を必ず持つ。

## 1.2 必ず残すログ
可能な範囲で以下を保存・参照する。

- ホスト（Rocky）: `logs/host_qemu.console.log`（`script` で採取）
- ゲスト（UmuOS）: `/logs/boot.log`
- 追加: `run/qemu.cmdline.txt` の実際に実行した内容

## 1.3 変更の粒度
- 1回の試行で触るのは「1〜2点まで」。
- 変えたら、必ず「戻し方（ロールバック）」も書く。

# 2. 実行環境メモ（最初に1回だけ埋める）

- Ubuntu: 24.04（ビルド）
- Rocky: 9.7（起動/受入）
- QEMU 実体: `/usr/libexec/qemu-kvm`
- br0 の有無・IP: （例）`192.168.0.200/24`
- firewalld: active/inactive
- SELinux: Enforcing/Permissive/Disabled

# 3. 合格条件チェック表（毎回◯×を埋める）

## 3.1 0.1.3互換（維持）
- [ ] switch_root 成立（ttyS0ログに `exec: /bin/switch_root /newroot /sbin/init`）
- [ ] ttyS0 root ログイン
- [ ] ttyS0 tama ログイン
- [ ] ttyS1 同時ログイン
- [ ] `/logs/boot.log` 追記

## 3.2 0.1.4追加（telnet）
- [ ] `eth0` に `192.168.0.202/24`
- [ ] default route `via 192.168.0.1`
- [ ] LAN から `192.168.0.202:23` 接続
- [ ] telnet で root ログイン
- [ ] telnet で tama ログイン

## 3.3 追加（nc転送）
- [ ] ゲスト受信（`nc -l -p 12345 > payload.bin`）
- [ ] Ubuntu送信（`nc 192.168.0.202 12345 < payload.bin`）

# 4. 試行ログ（ここに追記していく）

---

## 試行 0001（2026-01-25）

### 変更点（この試行で変えたこと）
- Ubuntu 事前準備の実施（必要パッケージ導入確認、コマンド存在確認）

### 実行手順（実際に打ったコマンド）
- Ubuntu:
  - `sudo apt update`
  - `sudo apt install -y build-essential bc bison flex libssl-dev libelf-dev libncurses-dev dwarves git wget rsync grub-efi-amd64-bin grub-common xorriso mtools cpio gzip xz-utils e2fsprogs musl-tools util-linux openssl telnet netcat-openbsd`
  - `command -v grub-mkrescue` → `/usr/bin/grub-mkrescue`
  - `command -v mkfs.ext4` → `/usr/sbin/mkfs.ext4`
  - `command -v tune2fs` → `/usr/sbin/tune2fs`
  - `command -v musl-gcc` → `/usr/bin/musl-gcc`
  - `command -v cpio` → `/usr/bin/cpio`
  - `command -v gzip` → `/usr/bin/gzip`
  - `cd ~/umu/umu_project/UmuOS-0.1.4-base-stable && ll -a`
- Kernel（Ubuntu / out-of-tree）:
  - `make -C ~/umu/umu_project/external/linux-6.18.1-kernel O=~/umu/umu_project/UmuOS-0.1.4-base-stable/kernel/build defconfig`
  - `~/umu/umu_project/external/linux-6.18.1-kernel/scripts/config --file ~/umu/umu_project/UmuOS-0.1.4-base-stable/kernel/build/.config -e DEVTMPFS -e DEVTMPFS_MOUNT -e BLK_DEV_INITRD -e EXT4_FS -e VIRTIO -e VIRTIO_PCI -e VIRTIO_BLK -e VIRTIO_NET -e NET -e INET -e SERIAL_8250 -e SERIAL_8250_CONSOLE -e DEVPTS_FS -e UNIX98_PTYS -e RD_GZIP`
  - `make -C ~/umu/umu_project/external/linux-6.18.1-kernel O=~/umu/umu_project/UmuOS-0.1.4-base-stable/kernel/build olddefconfig`
  - `make -C ~/umu/umu_project/external/linux-6.18.1-kernel O=~/umu/umu_project/UmuOS-0.1.4-base-stable/kernel/build -j"$(nproc)"`
  - `cp -f kernel/build/arch/x86/boot/bzImage iso_root/boot/vmlinuz-6.18.1`
  - `cp -f kernel/build/.config iso_root/boot/config-6.18.1`
  - `file iso_root/boot/vmlinuz-6.18.1`
- BusyBox（Ubuntu）:
  - `cd ~/umu/umu_project/UmuOS-0.1.4-base-stable/initramfs/busybox/work`
  - BusyBox 設定の対話（新規項目の質問に回答して進めた）
	- `CONFIG_TC` は `N`（`Networking Utilities` → `tc (8.3 kb)`）にした（`networking/tc.c` で止まる典型回避）
  - `make oldconfig`（新規/追加項目の整合を取る。最後にプロンプトへ復帰）
	- `make -j"$(nproc)"`
	- `file busybox`
- Rocky:
  - 

### 観測（ログ/画面で見えた事実）
- ttyS0:
  - 
- host（Rocky）`logs/host_qemu.console.log`:
  - 
- ゲスト `/logs/boot.log`:
  - 

### 観測（Kernelビルドの完了）
- `Kernel: arch/x86/boot/bzImage is ready  (#1)` を確認
- `file iso_root/boot/vmlinuz-6.18.1` の出力：
  - `Linux kernel x86 boot executable bzImage, version 6.18.1 (tama@umu) #1 SMP PREEMPT_DYNAMIC Sun Jan 25 14:41:02 JST 2026, RO-rootFS, swap_dev 0XD, Normal VGA`

### 観測（BusyBox 設定の完了）
- `initramfs/busybox/work$` で BusyBox の設定が最後まで進み、プロンプトに戻った
- 対話ログ例（抜粋）：`HUSH_*`、`FEATURE_SH_*`、`KLOGD/SYSLOGD/LOGREAD/LOGGER` などの新規項目に対して y/n を選択して進行

### 観測（BusyBox ビルドの完了）
- `LINK    busybox_unstripped` まで進み、`busybox` が生成された
- `file busybox` の出力：
  - `busybox: ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux), statically linked, ... stripped`

### 観測（BusyBox 簡易検査）
- `./busybox --list | grep -E '^(ip|telnetd|login|nc)$'` の出力：
  - `ip`
  - `login`
  - `nc`
  - `telnetd`
- `./busybox ip link/addr/route` が動作し、インターフェース・アドレス・default route が表示された

### 観測（initramfs rootfs 作成：BusyBox 配置と applet 展開）
- `rm -rf initramfs/rootfs` → `mkdir -p initramfs/rootfs/{bin,sbin,etc,proc,sys,dev,dev/pts,run,newroot,tmp}`
- `cp -f initramfs/busybox/work/busybox initramfs/rootfs/bin/busybox` → `chmod 755`
- `sudo chroot initramfs/rootfs /bin/busybox --install -s /bin`
- `sudo chroot initramfs/rootfs /bin/busybox --install -s /sbin`
- 確認：
  - `initramfs/rootfs/bin/switch_root -> /bin/busybox`
  - `initramfs/rootfs/sbin/getty -> /bin/busybox`
  - `initramfs/rootfs/sbin/telnetd -> /bin/busybox`
  - `initramfs/rootfs/bin/login -> /bin/busybox`

### 観測（initramfs `/init` の生成）
- `cp -f ~/umu/umu_project/UmuOS-0.1.3/initramfs/src/init.c initramfs/src/init.c`
- `musl-gcc -static -O2 -Wall -Wextra -o initramfs/rootfs/init initramfs/src/init.c`
- `chmod 755 initramfs/rootfs/init`
- `file initramfs/rootfs/init` の出力：
  - `ELF 64-bit LSB executable, x86-64, ... statically linked, with debug_info, not stripped`

### 観測（initrd 用 filelist 生成）
- `cd initramfs/rootfs`
- `find . -mindepth 1 -printf '%P\0' > ../initrd.filelist0` は無音が正常（ファイルへ書き込みのみ）
- 途中で ^C した場合は `rm -f ../initrd.filelist0` して作り直した
- 作り直し後：
  - `time find ... > ../initrd.filelist0` → `real 0m0.006s`
  - `echo $?` → `rc=0`
  - `ls -l ../initrd.filelist0` → `9200 bytes`
  - `tr '\0' '\n' < ../initrd.filelist0 | head` で先頭が `newroot` 等になっていることを確認

### 観測（initrd.img-6.18.1 生成と ISO 配置）
- `cpio --null -ov --format=newc ... > initrd.cpio` が完走し、`5141 ブロック` を出力した
  - 途中の標準出力で `proc/tmp/etc` などが列挙された（verbose）
- `cpio -t < initrd.cpio > initrd.cpio.list` → `5141 ブロック`
- `grep -E '^(init|bin/switch_root)$' initrd.cpio.list` の出力：
  - `bin/switch_root`
  - `init`
- `gzip -9 -c initrd.cpio > initrd.img-6.18.1`
- `cp -f initrd.img-6.18.1 ../iso_root/boot/initrd.img-6.18.1`

### 観測（永続ディスク disk.img：作成と最小 rootfs 投入）
- `disk/disk.img` を作成：
  - `truncate -s 4G disk.img`
  - `mkfs.ext4 -F -U 9f5a1e4f-19b2-4d1f-9a6e-0d2a59e2a0d4 disk.img`
  - `sudo blkid -p -o value -s UUID disk.img` → `9f5a1e4f-19b2-4d1f-9a6e-0d2a59e2a0d4`
- loop mount：
  - `sudo mount -o loop .../disk/disk.img /mnt/umuos014`
  - `findmnt /mnt/umuos014` → `/dev/loop14 ext4 rw,relatime`
- ext4 側に最小ディレクトリを作成：
  - `/mnt/umuos014/{bin,sbin,etc,proc,sys,dev,dev/pts,run,var,var/run,home,root,tmp,logs,etc/init.d,etc/umu}`
- ext4 側へ BusyBox 配置：
  - `sudo cp -f .../initramfs/busybox/work/busybox /mnt/umuos014/bin/busybox`
  - `sudo chroot /mnt/umuos014 /bin/busybox --install -s /bin`
  - `sudo chroot /mnt/umuos014 /bin/busybox --install -s /sbin`
  - `sudo ln -sf /bin/busybox /mnt/umuos014/sbin/init`
  - `sudo ls -l /mnt/umuos014/sbin/init` → `/bin/busybox` への symlink を確認

### 観測（永続ディスク：初期設定ファイル投入）
- `sudo tee /mnt/umuos014/etc/inittab`：
  - `::sysinit:/etc/init.d/rcS`
  - `ttyS0`/`ttyS1` の respawn（`/sbin/getty -L 115200 ...`）
- `sudo tee /mnt/umuos014/etc/umu/network.conf`：
  - `IFNAME=eth0` / `MODE=static`
  - `IP=192.168.0.202/24` / `GW=192.168.0.1` / `DNS=192.168.0.1`
- `sudo tee /mnt/umuos014/etc/securetty`：`ttyS0`/`ttyS1` と `pts/0..9`
- `sudo tee /mnt/umuos014/etc/init.d/rcS`：
  - `proc/sys/devtmpfs/devpts` を mount
  - `/var/run/utmp` を作成
  - `/logs/boot.log` に cmdline/mount/ip の永続ログを追記
  - `/etc/umu/network.conf` を読んで `ip addr`/default route を設定
  - `telnetd -p 23 -l /bin/login` を起動
  - `echo "[rcS] rcS done" > /dev/console`
- `sudo chmod 755 /mnt/umuos014/etc/init.d/rcS`

### 観測（永続ディスク：ユーザー設定とアンマウント）
- `sudo tee /mnt/umuos014/etc/passwd`：`root` と `tama(uid=1000,gid=1000)` を定義
- `sudo tee /mnt/umuos014/etc/group`：`root`/`users`/`tama` を定義
- `sudo mkdir -p /mnt/umuos014/root`、`sudo mkdir -p /mnt/umuos014/home/tama`
- `sudo chown 1000:1000 /mnt/umuos014/home/tama`
- `openssl passwd -6`：
  - 1回目は `Verify failure`（2回の入力が不一致）
  - 再実行して `root` 用と `tama` 用の `$6$...` を取得
- `sudo tee /mnt/umuos014/etc/shadow` へ `$6$...` を貼り付け
- `sudo chown root:root /mnt/umuos014/etc/shadow` / `sudo chmod 600 /mnt/umuos014/etc/shadow`
- `sync` → `sudo umount /mnt/umuos014`

### 観測（BusyBox: oldconfig の対話が出た例）
- `System Logging Utilities` の質問が出た（抜粋）：
  - `klogd (KLOGD) [Y/n/?] y`
  - `Use the klogctl() interface (FEATURE_KLOGD_KLOGCTL) [Y/n/?] y`
  - `logger (LOGGER) [Y/n/?] y`
  - `logread (LOGREAD) [Y/n/?] y`
  - `syslogd (SYSLOGD) [Y/n/?] y`

メモ：BusyBox は kernel のような `olddefconfig` ターゲットが無い（`make olddefconfig` は失敗する）ため、整合は `make oldconfig` で行う。

### メモ（BusyBox menuconfig での場所）
この段階で「どこを触っているか」が分からないと設定できないため、`CONFIG_...` の場所を記録する。

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

探し方：`make menuconfig` 中に `/`（検索）を押して、`STATIC` のように `CONFIG_` なしのシンボル名で検索する。

### 観測（Ubuntu：作業ルート直下の構成）
- `disk/ docs/ initramfs/ iso_root/ kernel/ logs/ run/` が存在

### 判定（この節の結論）
- Ubuntu 事前準備：OK（必要ツールが PATH 上に存在）
- Kernel 6.18.1 ビルド：OK（bzImage 生成、iso_root/boot へ配置まで完了）
- BusyBox 設定：OK（対話設定が完了し、作業ディレクトリのプロンプトに復帰）

更新：

- BusyBox ビルド：OK（`CONFIG_TC=n` で `busybox` 生成、`statically linked` を確認）

更新：

- initrd：OK（`initrd.img-6.18.1` 生成、`iso_root/boot/` へ配置）

更新：

- disk.img：OK（ext4 作成、loop mount、最小 rootfs と BusyBox 配置まで完了）

更新：

- disk.img：OK（inittab/rcS/network/passwd/shadow まで投入し、umount まで完了）

更新：

- ISO：OK（grub.cfg 作成、`grub-mkrescue` で UmuOS-0.1.4-boot.iso 生成、`ls -lh` で 27M を確認）

更新：

- run：OK（`run/qemu.cmdline.txt` を作成し、`sed -n '1,120p'` で内容を確認）

### 判定（チェック表の◯×と理由）
- 0.1.3互換:
  - 
- telnet:
  - 
- nc:
  - 

### 仮説（なぜこうなったか）
- 

### 次の最小変更（次試行で変える1点）
- Rocky 側で QEMU を起動し、ttyS0/ttyS1/telnet の受入（ログインと疎通）を確認する

### ロールバック（元に戻す方法）
- 

---

## 試行 0002（YYYY-MM-DD）
（追記）
