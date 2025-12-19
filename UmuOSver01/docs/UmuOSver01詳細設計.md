# UmuOS var0.1 詳細設計

## 仕様
- 電源投入からUEFI起動し、Linuxカーネル6.18.1(2025年12月時点で最新版)を起動
- 起動経路：UEFI → GRUB（UEFI版） → Linux（bzImage + initramfs）
- 対象アーキテクチャ：x86_64（ arch/x86/boot/bzImage を使用）
- 自作init（C言語で実装）
- コマンドは、Busyboxを利用する
- login IDとpasswordでログイン（ユーザーは、rootユーザー、tamaユーザーの2名）
- 一応はマルチユーザー仕様
- ext4ファイルシステムを搭載し、揮発性RAM環境からの脱却
- 自宅サーバーの仮想マシンマネージャから.isoを読み込み、起動できる仕様
- telnetで接続できるようにする
- suコマンド、poweroffコマンドを利用して起動を停止する

### コンソール/表示の前提（重要）

本設計は virt-manager（QEMU/KVM）上での利用を前提に、以下を満たす。

- **TTY（シリアル）接続は必須**：ホスト側からシリアルコンソールに接続してログインできること（例：`ttyS0`）。
- **画面側（virt-managerの表示）にもログイン手段を用意**：UEFI 環境ではレガシーVGAテキストモードが前提にならないため、
  Linux の fbcon（フレームバッファコンソール）経由で `tty1` を表示先として用いる。

補足：`console=` カーネルパラメータは「カーネルログ出力先」と「/dev/console（PID1の標準入出力の既定先）」に影響する。
TTY必須要件を満たすため、/dev/console はシリアル側に寄せる（`console=ttyS0,...` を最後に指定）。

1. 環境準備
1.1 必要パッケージのインストール

sudo apt update
sudo apt install -y build-essential bc bison flex libssl-dev \
  libelf-dev libncurses-dev dwarves git wget \
  grub-efi-amd64-bin grub-common xorriso mtools \
  qemu-system-x86 ovmf \
  cpio gzip xz-utils busybox-static e2fsprogs

※現在の環境にすでに導入済みのPKGもあるが、Ubuntuでは、上書きは問題ないので
このコマンドラインでも大丈夫です！

1.2 ディレクトリ作成

mkdir -p ~/umu/UmuOSver01/{kernel,initramfs,iso_root/boot/grub,logs}

※永続ストレージ（ext4イメージ）用
mkdir -p ~/umu/UmuOSver01/disk


2. カーネルビルド

2.1 ソース取得

cd ~/umu/UmuOSver01/kernel
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.1.tar.xz
tar -xf linux-6.18.1.tar.xz
cd linux-6.18.1


2.2 カーネル設定

今回はデフォルト設定でビルド。

cd linux-6.18.1
make mrproper        ※カーネルソースツリーを「完全初期化」する
make defconfig       ※Linuxカーネルのビルドにおける 「デフォルト設定ファイル（.config）の生成」 を行う
cp .config ../config-6.18.1     ※バックアップを~/umu/UmuOSver01/kernelに保存

2.3 ビルド

make -j$(nproc)

※make -j$(nproc)は、make defconfigで生成した.configを使いカーネルを
ビルドする。


2.4 成果物コピー

cp arch/x86/boot/bzImage ~/umu/UmuOSver01/iso_root/boot/vmlinuz-6.18.1
※bzImageをブートイメージとしてvmlinuz-6.18.1として~/umu/UmuOSver01/iso_root/boot/にコピー
　vmlinuz-6.18.1は、GRUB（ブートローダー）が読み込む。

cp .config ~/umu/UmuOSver01/iso_root/boot/config-6.18.1
※vmlinuz-6.18.1と同じディレクトリに入れてるけど、もしconfig-6.18.1が無くても
　影響しない。慣例的に同じディレクトリに入れてる


3. initramfs（BusyBox版）

3.1 構造作成

initramfsは、目的は「initramfs（初期RAMファイルシステム）」を構築するための作業場所。
この中で rootfs/ を作り、bin, etc, dev, proc, sys などのディレクトリを配置して、
最小限のLinux環境を再現する。
カーネルが起動直後に展開する「最初のルートファイルシステム」。
本格的なルートファイルシステムに切り替える前に、最低限のコマンドや設定を提供する。
BusyBoxを入れて ls, cat, ps, su などを使えるようにするのが典型的。

cd ~/umu/UmuOSver01/initramfs

mkdir -p rootfs/{bin,sbin,etc,proc,sys,dev,dev/pts,home/tama,root,persist}

cp "$(command -v busybox)" rootfs/bin/

# BusyBoxの所有者をrootにする（su等で権限が必要なため）
sudo chown root:root rootfs/bin/busybox

# su を使う場合は BusyBox を setuid root にする（必要に応じて）
sudo chmod 4755 rootfs/bin/busybox

cd ~/umu/UmuOSver01/initramfs/rootfs/bin


# BusyBoxコマンドを一度インストール
busybox --install -s .

# 全てのシンボリックリンクを相対パスに修正
for cmd in $(ls -1 | grep -v "^busybox$"); do
  rm "$cmd"
  ln -s busybox "$cmd"
done

cd ~/umu/UmuOSver01/initramfs

# ＜重要＞
BusyBox の各コマンドは symlink により提供しているが、
initramfs は実行時に RAM 上へ展開され、ルートディレクトリの
物理的な位置がビルド時とは異なる。

そのため、（環境によっては）BusyBox が生成する絶対パスの symlink をそのまま使用すると、
initramfs 展開後にリンク先が存在せず、コマンド実行に失敗する。

この問題を回避するため、全ての BusyBox コマンド symlink は
同一ディレクトリ内の busybox バイナリを指す相対パスに統一している。
これにより、initramfs がどのパスに展開されても確実に動作する。


3.2 ユーザー構成

/etc/passwd と /etc/shadow を作成する。
※ /etc/passwd はユーザー情報、/etc/shadow はパスワードハッシュを保持する。
※ su による root 昇格を計画通り動作させるため、root のパスワードは必須。
※ パスワードハッシュは openssl や mkpasswd で生成し、ここに埋め込む。

# ~/umu/UmuOSver01/initramfs/rootfs/etc/passwd    パーミッションは644
root:x:0:0:root:/root:/bin/sh        # root ユーザー。ホームは /root、シェルは /bin/sh
tama:x:1000:1000:tama:/home/tama:/bin/sh  # 一般ユーザー tama。ホームは /home/tama、シェルは /bin/sh

# ~/umu/UmuOSver01/initramfs/rootfs/etc/shadow    パーミッションは600
# フォーマット: 
# ユーザー名:パスワードハッシュ:最終変更日:最小日数:最大日数:警告日数:非アクティブ:有効期限
# root のパスワードは必須。tama は任意だが、ログイン時にパスワード入力を求めるなら設定する。
# パスワードハッシュ生成方法:
#   1. SHA-512 方式 (openssl)
#      $ openssl passwd -6  ※-6 は SHA-512 (crypt方式) を使うという指定です。
#      → パスワードを入力すると $6$... 形式のハッシュが出力される
#
#   2. yescrypt 方式 (mkpasswd)
#      $ mkpasswd --method=yescrypt
#      → パスワードを入力すると $y$j9T$... 形式のハッシュが出力される
#
# 生成したハッシュを以下のフィールドに貼り付ける。
# roor    パスワードは実験用として  UmuR1207  とする
# tama    パスワードは実験用として  UmuT1207  とする

root:$6$MJpFJ0jZ26E2H7uo$VTA1fmpPrJz0GRA6eFBzX/fxkW/GbCEOtDm9.MJejBk3FcRH9/dpO8yeGrWMYu0kTgZ/WXdBksggINyUcyjbJ/:19000:0:99999:7:::
tama:$6$tU0FU0qbwV4pzIb1$GiCtGWu6OInLB9sx3StpxLUazZDbnhPidzHzniAYA3GQ3Xdbt0UFvxEw17oYygLiu9478gPrUkB.zkXevM9Lq/:19000:0:99999:7:::

※ローカル環境で安全（一応サンプルのハッシュです）


3.3 init（C言語）作成

ここでは、Step4までの init シェルスクリプトと同じ動きをする init を C言語で実装し、
initramfs の /init として配置する。

動作要件（シェル版と同等）:
- /proc, /sys, /dev をマウント
- /dev/pts（devpts）をマウント（telnet/login の pseudo-tty 用）
- /proc/cmdline に "single" が含まれる場合は /bin/sh を起動
- 通常は /bin/getty -L ttyS0 115200 vt100 を起動

ソース配置先（リポジトリ管理用）:

# ~/umu/UmuOSver01/initramfs/src/init.c

ビルドと配置:

cd ~/umu/UmuOSver01/initramfs

# rootfs 側の /init（PID 1）として配置するため、基本は静的リンクでビルドする
gcc -static -Os -s -o rootfs/init src/init.c

# 実行パーミッション（/init は 755 推奨）
chmod 755 rootfs/init

# initramfs へ格納するファイルの所有者を root に揃えたい場合（任意）
sudo chown root:root rootfs/init


3.4 cpioアーカイブ作成

cd rootfs
find . | cpio -o -H newc | gzip > ../initrd.img-6.18.1
cd ..
cp initrd.img-6.18.1 ../iso_root/boot/


4. GRUB設定

# ~/umu/UmuOSver01/iso_root/boot/grub/grub.cfg
set timeout=20
set default=0

menuentry "Umu Project Linux kernel 6.18.1" {
  linux /boot/vmlinuz-6.18.1 ro console=tty0 console=ttyS0,115200n8
  initrd /boot/initrd.img-6.18.1
}

menuentry "Umu Project rescue 6.18.1" {
  linux /boot/vmlinuz-6.18.1 ro single console=tty0 console=ttyS0,115200n8
  initrd /boot/initrd.img-6.18.1
}

### virt-manager 側の要件（TTY接続）

- VM に **Serial** デバイス（例：`pty`）と **Console** を追加する。
- ホスト側は virsh console <vmname> または割り当てられた pts へ screen <pts> 115200 等で接続し、ttyS0 の getty が表示されること。


5. ISOイメージ作成

cd ~/umu/UmuOSver01
grub-mkrescue -o UmuOSver01-boot.iso iso_root


6. 永続ストレージ（ext4）

どのタイミングで ext4 を使えるようにするか：
- ver0.1：root は initramfs のまま維持し、/home 等を ext4 に載せ替えて永続化する（推奨）
- ver0.2以降：ext4 を /（ルート）にして switch_root/pivot_root する（発展）

6.1 ext4イメージ作成（ホスト側で作成）

cd ~/umu/UmuOSver01/disk
truncate -s 2G umuos.ext4.img
mkfs.ext4 -F -L UMU_PERSIST umuos.ext4.img

※パーティションを切らず「ディスク全体を ext4」として使う（手順を簡単化）。

6.2 virt-manager へ永続ディスクを追加

- VM に Storage を追加し、既存イメージとして umuos.ext4.img を指定
- バスは VirtIO 推奨（ゲスト側は多くの場合 /dev/vda）

6.3 init（C言語）で ext4 をマウントして永続領域を使う

マウントのタイミング：/proc, /sys, /dev（devtmpfs）が揃い、/dev 配下にブロックデバイスが出現した後。

注意：initramfs だけで動かす構成のため、ext4 と virtio-blk は「モジュール」ではなくカーネルに built-in（=y）で入っている必要がある。
mount -t ext4 が unknown filesystem になる場合は、.config で以下を確認する。
- CONFIG_EXT4_FS=y
- CONFIG_VIRTIO=y / CONFIG_VIRTIO_PCI=y / CONFIG_VIRTIO_BLK=y

起動後に実行されるコマンド相当（検証用）：
- mkdir -p /persist
- mount -t ext4 /dev/vda /persist
- mkdir -p /persist/home
- mount --bind /persist/home /home

初回起動の補足（推奨）：
- /persist/home/tama が無い場合は作成し、所有者を 1000:1000（tama）にする

仕様として明記すること（ver0.1）：
- 永続化対象：/home（少なくとも /home/tama）
- 永続ディスクが無い/マウント失敗時も起動は継続（ログに残す）


7. telnet 接続

目的：ホスト（自宅サーバー）からゲスト UmuOS に telnet 接続してログインできるようにする。

7.1 ネットワーク設定（DHCP）

getty 起動前にネットワークを上げる（BusyBox想定）。

起動後に実行されるコマンド相当：
- ip link set dev eth0 up   （環境により eth0/ens3 など。最初は eth0 を想定）
- udhcpc -i eth0

7.2 telnetd 起動

BusyBox の telnetd を利用し、ログインプログラムは /bin/login を使用する。

事前確認（ホスト側で BusyBox の機能を確認）：
- busybox | grep -E "(telnetd|login|udhcpc|ip)"

起動コマンド例：
- telnetd -l /bin/login

7.3 ホストから接続

- ゲスト側で ip addr でIP確認
- ホストから telnet <guest-ip>
- root / UmuR1207, tama / UmuT1207 でログインできること

7.4 検証（telnet）

目的：telnet接続が「ログインまで」成立していることを確認する。

ゲスト側（コンソール/シリアル）：
- ps で telnetd が起動していること
- ip addr / ip route でIPが付与されていること（udhcpc成功）

ホスト側：
- telnet <guest-ip> で接続し、root/tama それぞれでログインできること
- tama でログイン後、whoami が tama になること

7.5 検証（マルチユーザー）

目的：同時ログインが成立することを確認する。

- ホストでターミナルを2つ用意し、それぞれ telnet <guest-ip>
- 片方で root、もう片方で tama でログインできること


8. 検証（ext4永続化）

8.1 マウント状態確認（ゲスト側）

- mount で /persist が ext4 としてマウントされていること
- mount で /home が /persist/home の bind mount になっていること
- ls -ld /home/tama の所有者が tama (1000) になっていること

8.2 永続化確認（再起動）

手順：
- tama でログインし、/home/tama にファイルを作成する
  - 例：echo "persist-test" > /home/tama/persist.txt
- poweroff で停止 → 再起動
- 再ログイン後、/home/tama/persist.txt が残っていること

8.3 ディスク未接続時の挙動

手順：
- virt-manager で ext4ディスク（umuos.ext4.img）を外して起動
- ログインまで起動が継続すること（永続化はされない）


