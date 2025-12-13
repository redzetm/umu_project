# UmuOS var0.1 詳細設計

1. 環境準備
1.1 必要パッケージのインストール

sudo apt update
sudo apt install -y build-essential bc bison flex libssl-dev \
  libelf-dev libncurses-dev dwarves git wget \
  grub-efi-amd64-bin grub-common xorriso mtools \
  qemu-system-x86 ovmf \
  cpio gzip busybox-static

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















