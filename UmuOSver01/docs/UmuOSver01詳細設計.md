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

1. 環境準備
1.1 必要パッケージのインストール

sudo apt update
sudo apt install -y build-essential bc bison flex libssl-dev \
  libelf-dev libncurses-dev dwarves git wget \
  grub-efi-amd64-bin grub-common xorriso mtools \
  qemu-system-x86 ovmf \
  cpio gzip xz-utils busybox-static

※現在の環境にすでに導入済みのPKGもあるが、Ubuntuでは、上書きは問題ないので
このコマンドラインでも大丈夫です！

1.2 ディレクトリ作成

mkdir -p ~/umu/UmuOSver01/{kernel,initramfs,iso_root/boot/grub,logs}


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

mkdir -p rootfs/{bin,sbin,etc,proc,sys,dev,home/tama,root}

cp /usr/bin/busybox rootfs/bin/

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










