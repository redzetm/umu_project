---
title: UmuOS-0.1.4-base-stable 実装ノート（0.1.5-dev作業ログ）
date: 2026-02-11
status: log
note: |
  ユーザー指示により、0.1.5-dev の作業記録としてこのファイル名で残す。
  （0.1.4-base-stable の成果手順を 0.1.5-dev で再実行しているログ）
---

# UmuOS-0.1.4-base-stable 実装ノート（0.1.5-dev作業ログ）

## 目的
- 手でコピペ実装した内容を「何を・いつ・どこで」実行したか残す。
- 途中で止めても、ここまでの到達点が分かるようにする。

## 環境
- Host: Ubuntu（ユーザー環境）
- 作業ディレクトリ: `/home/tama/umu/umu_project/UmuOS-0.1.5-dev`

---

## 2026-02-11: Step 1 Ubuntu 事前準備（パッケージ）

### 実行
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
# `libxcrypt-dev` は環境によってはリポジトリ未有効で「見つからない」ことがある。
# その場合でも手順は続行できるように、存在チェックしてから入れる。
sudo apt install -y libcrypt-dev || true

# もし `libxcrypt-dev` が見つからない場合は `universe` が無効な可能性がある。
# ただし必須ではないので、下は全部失敗しても続行する。
sudo apt install -y software-properties-common || true
sudo add-apt-repository -y universe || true
sudo apt update || true

apt-cache show libxcrypt-dev >/dev/null 2>&1 && sudo apt install -y libxcrypt-dev || true
```

### 結果
- ユーザー申告: 「ここまでは成功した」
- 補足: `libxcrypt-dev` は必須ではない（`su` の静的リンクで必要なら後で追加対処）。

---

## 2026-02-11: Step 2 作業ディレクトリ（初期化）

### 実行
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

### 結果
- ユーザー申告: 「ここまでは問題なく成功した」
- 作業用ディレクトリと、kernel/busybox の外部ソース存在確認まで完了。

---

## 2026-02-11: Step 3 Kernel（out-of-tree）

### 実行
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

make -C /home/tama/umu/umu_project/external/linux-6.18.1-kernel \
	O=/home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build -j4 bzImage \
	2>&1 | tee /home/tama/umu/umu_project/UmuOS-0.1.5-dev/logs/kernel_build_bzImage.log

mkdir -p /home/tama/umu/umu_project/UmuOS-0.1.5-dev/iso_root/boot
cp -f /home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build/arch/x86/boot/bzImage \
  /home/tama/umu/umu_project/UmuOS-0.1.5-dev/iso_root/boot/vmlinuz-6.18.1
cp -f /home/tama/umu/umu_project/UmuOS-0.1.5-dev/kernel/build/.config \
  /home/tama/umu/umu_project/UmuOS-0.1.5-dev/iso_root/boot/config-6.18.1
```

### 結果
- ユーザー申告: 「ここまで成功した」
- ログ: `/home/tama/umu/umu_project/UmuOS-0.1.5-dev/logs/kernel_build_bzImage.log`
- 期待成果物（ISO配置）: `/home/tama/umu/umu_project/UmuOS-0.1.5-dev/iso_root/boot/vmlinuz-6.18.1`

---

## 2026-02-11: Step 4 BusyBox（静的リンク、対話なし）

### 実行
```bash
cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev

rm -rf /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work
mkdir -p /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work
rsync -a --delete /home/tama/umu/umu_project/external/busybox-1.36.1/ \
	/home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work/

cd /home/tama/umu/umu_project/UmuOS-0.1.5-dev/initramfs/busybox/work
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

make -j4 2>&1 | tee /home/tama/umu/umu_project/UmuOS-0.1.5-dev/logs/busybox_build.log
```

### 結果
- 一度目の状況: `logs/busybox_build.log` に `networking/tc.c` のコンパイルエラーが出ており、`initramfs/busybox/work/busybox` が生成されていなかった（Step5 の `cp` が失敗）
- 原因: BusyBox の `tc` アプレット（`CONFIG_TC=y`）が、ホスト側のカーネルヘッダ差分でビルド失敗し得る
- 対処: `CONFIG_TC` を明示的に無効化し、合わせて `CONFIG_STATIC=y` を成立させた上で `make oldconfig` → 再ビルド
- 最終結果: `initramfs/busybox/work/busybox` が生成され、`file busybox` で「statically linked」を確認

---

## 次にやること（未実施）
- Step 5 initramfs
- Step 6 disk.img
- Step 7 自作 su
- Step 9 ISO 作成

---

## 2026-02-11: Step 5 initramfs（initrd.img-6.18.1）

### 結果
- ユーザー申告: 「ここまでは上手く行った」
- 成果物: `UmuOS-0.1.5-dev/initramfs/initrd.img-6.18.1` が生成されている
- ISO配置: `UmuOS-0.1.5-dev/iso_root/boot/initrd.img-6.18.1` に配置されている

### 次にやること
- Step 6 disk.img（永続 rootfs）

---

## 2026-02-11: Step 6 disk.img（永続 rootfs）

### 目的
- 永続 rootfs を ext4 の `disk/disk.img` として用意し、`switch_root` 後のユーザーランド（/sbin/init, /etc/inittab, /etc/init.d/rcS, ネットワーク設定、常駐サービス等）を配置する。

### 結果（到達点）
- ユーザー申告: 「Step6 まで正常にできた」
- `disk/disk.img` が存在し、ホスト側 `/mnt/umuos015` にマウントして rootfs が構成されている。

### 配置・設定（確認できたもの）
- `/mnt/umuos015/bin/busybox` が存在（`file` で `statically linked` を確認）
- `/mnt/umuos015/sbin/init -> /bin/busybox` の symlink を確認
- `/mnt/umuos015/etc/inittab` が存在し、`::sysinit:/etc/init.d/rcS` と `ttyS0/ttyS1` getty を設定
- `/mnt/umuos015/etc/init.d/rcS` が存在し、概ね以下を実施する構成になっている
	- `proc` / `sys` / `dev` / `devpts` の mount
	- `logs` / `run` / `var/run` / `umu_bin` 等の作成
	- `/etc/umu/network.conf` を読み込んで `ip link/addr/route` でネットワークを設定
	- `telnetd` 起動、`/umu_bin/ftpd_start` 起動
- `/mnt/umuos015/etc/umu/network.conf` が存在（static 設定）
	- `IFNAME=eth0`
	- `MODE=static`
	- `IP=192.168.0.202/24`
	- `GW=192.168.0.1`
	- `DNS=8.8.8.8`
- `/mnt/umuos015/umu_bin/` に補助スクリプトが存在（`ll`, `ftpd_start`, `ftpd_stop`, `ntp_sync`）

### 補足
- パスワードハッシュは別工程（Step6.1）で作成し、`/etc/shadow` に手で貼り付ける。

---

## 2026-02-11: Step 6.1 パスワード（/etc/shadow 手貼り）

### 実行
- `openssl passwd -6` を 2 回実行し、`root` と `tama` の `$6$...` ハッシュを作成した。
- 作成したハッシュを `sudo tee /mnt/umuos015/etc/shadow` のブロックに貼り付けて反映した。

### 結果
- `/mnt/umuos015/etc/shadow` が作成され、所有者 `root:root`、権限 `600` を確認。
- 注: セキュリティのため、実装ノートにはハッシュ文字列は記載しない。

### 次にやること
- Step7: 自作 `su`
- Step8: アンマウント
- Step9: ISO 作成

---

## 2026-02-11: Step 7 自作 su（/umu_bin/su）

### 実行
- `work/umu_su.c` を作成し、静的リンクで `work/umu_su` をビルドした。
	- `-lcrypt` で失敗する場合に備えて、`-lxcrypt` にフォールバックする手順で実施した。
- `work/umu_su` を `disk.img` 側へ `/mnt/umuos015/umu_bin/su` としてコピーした。
- 所有者を `root:root` にし、`chmod 4755`（setuid root）を設定した。

### 結果
- ユーザー申告: 「上手く行った」
- `/mnt/umuos015/umu_bin/su` が `mode=4755 owner=root:root` であることを確認。
- `file /mnt/umuos015/umu_bin/su` で `setuid` かつ `statically linked` を確認。

### 次にやること
- Step8: アンマウント
- Step9: ISO 作成

---

## 2026-02-11: Step 8 アンマウント

### 実行
- `sync` の後、`sudo umount /mnt/umuos015` を実行した。

### 結果
- 一度 `umount: /mnt/umuos015: 対象は使用中です.` となったが、原因は「別ターミナルが `/mnt/umuos015` 配下を `cd` したまま」だった。
- すべてのターミナルで `/mnt/umuos015` 配下から抜けた後に再実行し、アンマウントできた。

### 次にやること
- Step9: ISO 作成

---

## 2026-02-11: Step 9 ISO（grub.cfg + grub-mkrescue）

### 実行
- `iso_root/boot/grub/grub.cfg` を作成した。
- `grub-mkrescue` を実行し、ISO を生成した。

### 結果
- ユーザー申告: 「無事問題なく終了」
- 成果物: `UmuOS-0.1.5-dev/UmuOS-0.1.5-boot.iso` が生成されている。

### 次にやること
- Step10: Rocky 起動用（qemu.cmdline.txt）

---

## 2026-02-11: Step 10 Rocky 起動用（qemu.cmdline.txt）

### 実行
- `run/qemu.cmdline.txt` を作成した。

### 結果
- ユーザー申告: 「正常終了」
- 成果物: `UmuOS-0.1.5-dev/run/qemu.cmdline.txt` が生成されている。
